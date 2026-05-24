/*
 * NRF54L15CryptoEngine — CRACEN hardware-accelerated crypto for Meshtastic.
 *
 * All crypto runs in hardware via the nRF54L15 CRACEN module:
 *   - AES-ECB/CTR (channel encryption) — BA411E CryptoMaster DMA
 *   - AES-CCM (PKI message auth) — AES-ECB blocks + software CCM framing
 *   - X25519 ECDH (PKI shared secret) — BA414EP PKE
 *   - SHA-256 (key derivation) — BA413 hash engine via CryptoMaster DMA
 *
 * AES-CTR and AES-CCM are built on AES-ECB because CRACEN's native CTR
 * mode has a DMA timeout issue on the nRF54L15.
 *
 * No software Curve25519, SHA256, AESSmall256, or aes-ccm.cpp needed.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "CryptoEngine.h"
#include "configuration.h"
#include "meshUtils.h"

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/random/random.h>
#include <string.h>

extern "C" {
#include <nrfx.h>
#include <hal/nrf_cracen.h>
#include <hal/nrf_cracen_cm.h>
#include <helpers/nrf_cracen_cm_dma.h>
#include "cracen_ed25519.h"
#include "cracen_cm.h"
}

#define CRACEN_TIMEOUT_ITERS 10000
#define AES_BLOCK_SIZE 16

/* Serialize CRACEN access without locking out IRQs. */
K_MUTEX_DEFINE(cracen_mutex);

/* ------------------------------------------------------------------ */
/*  Low-level CRACEN helpers                                          */
/* ------------------------------------------------------------------ */

static int cracen_cm_wait(void)
{
    for (int i = 0; i < CRACEN_TIMEOUT_ITERS; i++) {
        uint32_t errs = nrf_cracen_cm_int_pending_get(NRF_CRACENCORE);
        if (errs & (NRF_CRACEN_CM_INT_FETCH_ERROR_MASK | NRF_CRACEN_CM_INT_PUSH_ERROR_MASK)) {
            return -1;
        }
        uint32_t busy = nrf_cracen_cm_status_get(NRF_CRACENCORE,
            NRF_CRACEN_CM_STATUS_BUSY_FETCH_MASK |
            NRF_CRACEN_CM_STATUS_BUSY_PUSH_MASK  |
            NRF_CRACEN_CM_STATUS_PUSH_WAITING_MASK);
        if (!busy) {
            return 0;
        }
    }
    return -2;
}

static int cracen_cm_execute(struct nrf_cracen_cm_dma_desc *in_descs,
                             struct nrf_cracen_cm_dma_desc *out_desc)
{
    nrf_cracen_module_enable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);

    nrf_cracen_cm_fetch_addr_set(NRF_CRACENCORE, (void *)in_descs);
    nrf_cracen_cm_push_addr_set(NRF_CRACENCORE, (void *)out_desc);

    nrf_cracen_cm_config_indirect_set(NRF_CRACENCORE,
        (nrf_cracen_cm_config_indirect_mask_t)(
            NRF_CRACEN_CM_CONFIG_INDIRECT_FETCH_MASK |
            NRF_CRACEN_CM_CONFIG_INDIRECT_PUSH_MASK));

    __DMB();

    nrf_cracen_cm_start(NRF_CRACENCORE);

    int ret = cracen_cm_wait();

    nrf_cracen_cm_softreset(NRF_CRACENCORE);
    nrf_cracen_module_disable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);

    return ret;
}

/* AES-ECB single-block encrypt. Caller must hold cracen_mutex. */
static int cracen_aes_ecb_block(const uint8_t *key, size_t key_len,
                                uint8_t *input, uint8_t *output)
{
    static const uint32_t ecb_config = NRF_CRACEN_CM_AES_CONFIG(
        NRF_CRACEN_CM_AES_CONFIG_MODE_ECB,
        NRF_CRACEN_CM_AES_CONFIG_KEY_SW_PROGRAMMED,
        false, false, false);

    struct nrf_cracen_cm_dma_desc in_descs[3];

    in_descs[0].p_addr = (uint8_t *)&ecb_config;
    in_descs[0].length = sizeof(ecb_config) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[0].dmatag = NRF_CRACEN_CM_DMA_TAG_AES_CONFIG(NRF_CRACEN_CM_AES_REG_OFFSET_CONFIG);
    in_descs[0].p_next = &in_descs[1];

    in_descs[1].p_addr = (uint8_t *)key;
    in_descs[1].length = (uint32_t)key_len | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[1].dmatag = NRF_CRACEN_CM_DMA_TAG_AES_CONFIG(NRF_CRACEN_CM_AES_REG_OFFSET_KEY);
    in_descs[1].p_next = &in_descs[2];

    in_descs[2].p_addr = input;
    in_descs[2].length = 16 | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[2].dmatag = NRF_CRACEN_CM_DMA_TAG_LAST
                       | NRF_CRACEN_CM_DMA_TAG_ENGINE_AES
                       | NRF_CRACEN_CM_DMA_TAG_DATATYPE_AES_PAYLOAD;
    in_descs[2].p_next = NRF_CRACEN_CM_DMA_DESC_STOP;

    struct nrf_cracen_cm_dma_desc out_desc;
    out_desc.p_addr = output;
    out_desc.length = 16 | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    out_desc.p_next = NRF_CRACEN_CM_DMA_DESC_STOP;
    out_desc.dmatag = NRF_CRACEN_CM_DMA_TAG_LAST;

    return cracen_cm_execute(in_descs, &out_desc);
}

/* ------------------------------------------------------------------ */
/*  AES-CCM via CRACEN AES-ECB                                        */
/*  Adapted from upstream aes-ccm.cpp (WPA/BSD license, Jouni Malinen)*/
/*  Uses cracen_aes_ecb_block() directly instead of virtual dispatch. */
/* ------------------------------------------------------------------ */

static void put_be16(uint8_t *a, uint16_t val)
{
    a[0] = val >> 8;
    a[1] = val & 0xff;
}

static void xor_block(uint8_t *dst, const uint8_t *src)
{
    for (int i = 0; i < AES_BLOCK_SIZE; i++)
        dst[i] ^= src[i];
}

/* CBC-MAC auth start: builds B_0, processes AAD. */
static int ccm_auth_start(const uint8_t *key, size_t key_len,
                          size_t M, size_t L, const uint8_t *nonce,
                          const uint8_t *aad, size_t aad_len,
                          size_t plain_len, uint8_t *x)
{
    uint8_t aad_buf[2 * AES_BLOCK_SIZE];
    uint8_t b[AES_BLOCK_SIZE];

    b[0] = aad_len ? 0x40 : 0;
    b[0] |= (((M - 2) / 2) << 3);
    b[0] |= (L - 1);
    memcpy(&b[1], nonce, 15 - L);
    put_be16(&b[AES_BLOCK_SIZE - L], plain_len);
    int ret = cracen_aes_ecb_block(key, key_len, b, x);
    if (ret) return ret;

    if (!aad_len) return 0;
    put_be16(aad_buf, aad_len);
    memcpy(aad_buf + 2, aad, aad_len);
    memset(aad_buf + 2 + aad_len, 0, sizeof(aad_buf) - 2 - aad_len);
    xor_block(aad_buf, x);
    ret = cracen_aes_ecb_block(key, key_len, aad_buf, x);
    if (ret) return ret;

    if (aad_len > AES_BLOCK_SIZE - 2) {
        xor_block(&aad_buf[AES_BLOCK_SIZE], x);
        ret = cracen_aes_ecb_block(key, key_len, &aad_buf[AES_BLOCK_SIZE], x);
        if (ret) return ret;
    }
    return 0;
}

/* CBC-MAC auth: process data blocks. */
static int ccm_auth(const uint8_t *key, size_t key_len,
                    const uint8_t *data, size_t len, uint8_t *x)
{
    size_t last = len % AES_BLOCK_SIZE;
    for (size_t i = 0; i < len / AES_BLOCK_SIZE; i++) {
        xor_block(x, data);
        data += AES_BLOCK_SIZE;
        int ret = cracen_aes_ecb_block(key, key_len, x, x);
        if (ret) return ret;
    }
    if (last) {
        for (size_t i = 0; i < last; i++)
            x[i] ^= *data++;
        int ret = cracen_aes_ecb_block(key, key_len, x, x);
        if (ret) return ret;
    }
    return 0;
}

/* CTR encryption of payload. */
static int ccm_encr(const uint8_t *key, size_t key_len,
                    size_t L, const uint8_t *in, size_t len,
                    uint8_t *out, uint8_t *a)
{
    size_t last = len % AES_BLOCK_SIZE;
    size_t i;
    for (i = 1; i <= len / AES_BLOCK_SIZE; i++) {
        put_be16(&a[AES_BLOCK_SIZE - 2], i);
        int ret = cracen_aes_ecb_block(key, key_len, a, out);
        if (ret) return ret;
        xor_block(out, in);
        out += AES_BLOCK_SIZE;
        in += AES_BLOCK_SIZE;
    }
    if (last) {
        put_be16(&a[AES_BLOCK_SIZE - 2], i);
        int ret = cracen_aes_ecb_block(key, key_len, a, out);
        if (ret) return ret;
        for (size_t j = 0; j < last; j++)
            *out++ ^= *in++;
    }
    return 0;
}

/* Encrypt auth tag (U = T XOR S_0). */
static int ccm_encr_auth(const uint8_t *key, size_t key_len,
                         size_t M, const uint8_t *x, uint8_t *a, uint8_t *auth)
{
    uint8_t tmp[AES_BLOCK_SIZE];
    put_be16(&a[AES_BLOCK_SIZE - 2], 0);
    int ret = cracen_aes_ecb_block(key, key_len, a, tmp);
    if (ret) return ret;
    for (size_t i = 0; i < M; i++)
        auth[i] = x[i] ^ tmp[i];
    return 0;
}

/* Decrypt auth tag. */
static int ccm_decr_auth(const uint8_t *key, size_t key_len,
                         size_t M, uint8_t *a, const uint8_t *auth, uint8_t *t)
{
    uint8_t tmp[AES_BLOCK_SIZE];
    put_be16(&a[AES_BLOCK_SIZE - 2], 0);
    int ret = cracen_aes_ecb_block(key, key_len, a, tmp);
    if (ret) return ret;
    for (size_t i = 0; i < M; i++)
        t[i] = auth[i] ^ tmp[i];
    return 0;
}

static int constant_time_compare(const void *a_, const void *b_, size_t len)
{
    const volatile uint8_t *volatile a = (const volatile uint8_t *volatile)a_;
    const volatile uint8_t *volatile b = (const volatile uint8_t *volatile)b_;
    if (len == 0) return 0;
    if (a == NULL || b == NULL) return -1;
    volatile uint8_t d = 0U;
    for (size_t i = 0; i < len; i++)
        d |= (a[i] ^ b[i]);
    return (1 & (((unsigned int)d - 1) >> 8)) - 1;
}

/* AES-CCM encrypt. Caller must hold cracen_mutex. L=2 fixed. */
static int cracen_aes_ccm_ae(const uint8_t *key, size_t key_len,
                             const uint8_t *nonce, size_t M,
                             const uint8_t *plain, size_t plain_len,
                             const uint8_t *aad, size_t aad_len,
                             uint8_t *crypt, uint8_t *auth)
{
    const size_t L = 2;
    uint8_t x[AES_BLOCK_SIZE], a[AES_BLOCK_SIZE];
    if (aad_len > 30 || M > AES_BLOCK_SIZE) return -1;

    int ret = ccm_auth_start(key, key_len, M, L, nonce, aad, aad_len, plain_len, x);
    if (ret) return ret;
    ret = ccm_auth(key, key_len, plain, plain_len, x);
    if (ret) return ret;

    a[0] = L - 1;
    memcpy(&a[1], nonce, 15 - L);
    memset(&a[14], 0, 2);

    ret = ccm_encr(key, key_len, L, plain, plain_len, crypt, a);
    if (ret) return ret;
    return ccm_encr_auth(key, key_len, M, x, a, auth);
}

/* AES-CCM decrypt + verify. Caller must hold cracen_mutex. L=2 fixed. */
static bool cracen_aes_ccm_ad(const uint8_t *key, size_t key_len,
                              const uint8_t *nonce, size_t M,
                              const uint8_t *crypt, size_t crypt_len,
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *auth, uint8_t *plain)
{
    const size_t L = 2;
    uint8_t x[AES_BLOCK_SIZE], a[AES_BLOCK_SIZE];
    uint8_t t[AES_BLOCK_SIZE];
    if (aad_len > 30 || M > AES_BLOCK_SIZE) return false;

    a[0] = L - 1;
    memcpy(&a[1], nonce, 15 - L);
    memset(&a[14], 0, 2);

    int ret = ccm_decr_auth(key, key_len, M, a, auth, t);
    if (ret) return false;
    ret = ccm_encr(key, key_len, L, crypt, crypt_len, plain, a);
    if (ret) return false;
    ret = ccm_auth_start(key, key_len, M, L, nonce, aad, aad_len, crypt_len, x);
    if (ret) return false;
    ret = ccm_auth(key, key_len, plain, crypt_len, x);
    if (ret) return false;

    return constant_time_compare(x, t, M) == 0;
}

/* ------------------------------------------------------------------ */
/*  NRF54L15CryptoEngine                                              */
/* ------------------------------------------------------------------ */

class NRF54L15CryptoEngine : public CryptoEngine {
  public:
    NRF54L15CryptoEngine() {}
    ~NRF54L15CryptoEngine() override {}

    void encryptAESCtr(CryptoKey _key, uint8_t *_nonce,
                       size_t numBytes, uint8_t *bytes) override
    {
        if (_key.length <= 0 || numBytes == 0) {
            return;
        }
        if (numBytes > MAX_BLOCKSIZE) {
            LOG_ERROR("CRACEN: packet too large (%u)", (unsigned)numBytes);
            return;
        }

        k_mutex_lock(&cracen_mutex, K_FOREVER);

        uint8_t ctr_block[16];
        memcpy(ctr_block, _nonce, 16);

        size_t offset = 0;
        while (offset < numBytes) {
            uint8_t keystream[16];
            int ret = cracen_aes_ecb_block(_key.bytes, _key.length, ctr_block, keystream);
            if (ret != 0) {
                LOG_ERROR("CRACEN AES-ECB failed in CTR mode (%d) at offset %u", ret, (unsigned)offset);
                break;
            }

            size_t chunk = numBytes - offset;
            if (chunk > 16) chunk = 16;
            for (size_t i = 0; i < chunk; i++) {
                bytes[offset + i] ^= keystream[i];
            }

            offset += chunk;

            for (int i = 15; i >= 12; i--) {
                if (++ctr_block[i] != 0) break;
            }
        }

        k_mutex_unlock(&cracen_mutex);
    }

#if !(MESHTASTIC_EXCLUDE_PKI)
    void aesSetKey(const uint8_t *key_data, size_t key_len) override
    {
        if (key_len > sizeof(ecb_key)) {
            key_len = sizeof(ecb_key);
        }
        memcpy(ecb_key, key_data, key_len);
        ecb_key_len = key_len;
    }

    void aesEncrypt(uint8_t *in, uint8_t *out) override
    {
        k_mutex_lock(&cracen_mutex, K_FOREVER);

        int ret = cracen_aes_ecb_block(ecb_key, ecb_key_len, in, out);
        if (ret != 0) {
            LOG_ERROR("CRACEN AES-ECB DMA error (%d)", ret);
        }

        k_mutex_unlock(&cracen_mutex);
    }

    bool encryptCurve25519(uint32_t toNode, uint32_t fromNode,
                           meshtastic_UserLite_public_key_t remotePublic,
                           uint64_t packetNum, size_t numBytes,
                           const uint8_t *bytes, uint8_t *bytesOut) override
    {
        uint8_t *auth = bytesOut + numBytes;
        long extraNonceTmp = random();
        memcpy((uint8_t *)(auth + 8), &extraNonceTmp, sizeof(uint32_t));
        LOG_DEBUG("Random nonce value: %d", extraNonceTmp);

        if (remotePublic.size == 0) {
            LOG_DEBUG("Node %d or their public_key not found", toNode);
            return false;
        }
        if (!setDHPublicKey(remotePublic.bytes))
            return false;
        hash(shared_key, 32);
        initNonce(fromNode, packetNum, extraNonceTmp);

        printBytes("Attempt encrypt with nonce: ", nonce, 13);
        printBytes("Attempt encrypt with shared_key starting with: ", shared_key, 8);

        k_mutex_lock(&cracen_mutex, K_FOREVER);
        int ret = cracen_aes_ccm_ae(shared_key, 32, nonce, 8,
                                    bytes, numBytes, nullptr, 0,
                                    bytesOut, auth);
        k_mutex_unlock(&cracen_mutex);

        memcpy((uint8_t *)(auth + 8), &extraNonceTmp, sizeof(uint32_t));

        if (ret != 0) {
            LOG_ERROR("CRACEN AES-CCM encrypt failed (%d)", ret);
            return false;
        }
        return true;
    }

    bool decryptCurve25519(uint32_t fromNode,
                           meshtastic_UserLite_public_key_t remotePublic,
                           uint64_t packetNum, size_t numBytes,
                           const uint8_t *bytes, uint8_t *bytesOut) override
    {
        const uint8_t *auth = bytes + numBytes - 12;
        uint32_t extraNonce;
        memcpy(&extraNonce, auth + 8, sizeof(uint32_t));
        LOG_INFO("Random nonce value: %d", extraNonce);

        if (remotePublic.size == 0) {
            LOG_DEBUG("Node or its public key not found in database");
            return false;
        }

        if (!setDHPublicKey(remotePublic.bytes))
            return false;
        hash(shared_key, 32);
        initNonce(fromNode, packetNum, extraNonce);

        printBytes("Attempt decrypt with nonce: ", nonce, 13);
        printBytes("Attempt decrypt with shared_key starting with: ", shared_key, 8);

        k_mutex_lock(&cracen_mutex, K_FOREVER);
        bool ok = cracen_aes_ccm_ad(shared_key, 32, nonce, 8,
                                    bytes, numBytes - 12, nullptr, 0,
                                    auth, bytesOut);
        k_mutex_unlock(&cracen_mutex);

        return ok;
    }

    bool setDHPublicKey(uint8_t *pubKey) override
    {
        k_mutex_lock(&cracen_mutex, K_FOREVER);

        int ret = cracen_x25519_scalarmult(private_key, pubKey, shared_key);

        k_mutex_unlock(&cracen_mutex);

        if (ret != 0) {
            LOG_WARN("CRACEN X25519 scalarmult failed (%d)", ret);
            return false;
        }
        return true;
    }

    void hash(uint8_t *bytes, size_t numBytes) override
    {
        k_mutex_lock(&cracen_mutex, K_FOREVER);

        uint8_t digest[32];
        int ret = cracen_cm_sha256(bytes, numBytes, digest);

        k_mutex_unlock(&cracen_mutex);

        if (ret != 0) {
            LOG_ERROR("CRACEN SHA-256 failed (%d)", ret);
            return;
        }
        memcpy(bytes, digest, 32);
    }

#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN)
    void generateKeyPair(uint8_t *pubKey, uint8_t *privKey) override
    {
        static const uint8_t x25519_basepoint[32] = {9};

        k_mutex_lock(&cracen_mutex, K_FOREVER);

        do {
            sys_rand_get(private_key, 32);
            private_key[0] &= 0xF8;
            private_key[31] = (private_key[31] & 0x7F) | 0x40;
            int ret = cracen_x25519_scalarmult(private_key, x25519_basepoint, public_key);
            if (ret != 0) {
                continue;
            }
        } while (memfll(public_key, 0, 32));

        k_mutex_unlock(&cracen_mutex);

        memcpy(pubKey, public_key, 32);
        memcpy(privKey, private_key, 32);
        LOG_DEBUG("CRACEN X25519 keypair generated");
    }

    bool regeneratePublicKey(uint8_t *pubKey, uint8_t *privKey) override
    {
        static const uint8_t x25519_basepoint[32] = {9};

        if (memfll(privKey, 0, 32)) {
            LOG_WARN("X25519 key generation failed due to blank private key");
            return false;
        }

        k_mutex_lock(&cracen_mutex, K_FOREVER);

        int ret = cracen_x25519_scalarmult(privKey, x25519_basepoint, pubKey);

        k_mutex_unlock(&cracen_mutex);

        if (ret != 0 || memfll(pubKey, 0, 32)) {
            LOG_ERROR("CRACEN X25519 pubkey generation failed");
            memset(pubKey, 0, 32);
            return false;
        }

        memcpy(private_key, privKey, 32);
        memcpy(public_key, pubKey, 32);
        return true;
    }
#endif /* !MESHTASTIC_EXCLUDE_PKI_KEYGEN */

  private:
    uint8_t ecb_key[32] = {0};
    size_t ecb_key_len = 0;
#endif /* !MESHTASTIC_EXCLUDE_PKI */
};

CryptoEngine *crypto = new NRF54L15CryptoEngine();
