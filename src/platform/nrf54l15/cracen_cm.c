/*
 * cracen_cm.c — CRACEN CryptoMaster hardware SHA/AES
 *
 * Uses the DMA-based CryptoMaster interface to hardware-accelerate:
 * - SHA-256/SHA-512 via BA413 hash engine
 * - AES-256-CBC via AES engine
 *
 * Based on nrfx_cracen.c AES-ECB implementation (BSD-3-Clause nrfx HAL)
 * and hash engine configuration from sdk-nrf sxsymcrypt (Nordic-5-Clause).
 */

#include "cracen_cm.h"
#include <zephyr/kernel.h>
#include <hal/nrf_cracen.h>
#include <hal/nrf_cracen_cm.h>
#include <helpers/nrf_cracen_cm_dma.h>
#include <string.h>

/* ---- BA413 Hash Engine Config ---- */
#define HASH_HW_PAD     (1 << 9)   /* Hardware does SHA padding */
#define HASH_FINAL      (1 << 10)  /* This is the final block */
#define HASH_SHA256     (0x08 | HASH_HW_PAD | HASH_FINAL)
#define HASH_SHA512     (0x20 | HASH_HW_PAD | HASH_FINAL)

/* BA413 DMA tags */
#define TAG_HASH_CONFIG  (NRF_CRACEN_CM_DMA_TAG_ENGINE_HASH | NRF_CRACEN_CM_DMA_TAG_CONFIG)
#define TAG_HASH_DATA    (NRF_CRACEN_CM_DMA_TAG_ENGINE_HASH | NRF_CRACEN_CM_DMA_TAG_DATATYPE_HASH_MESSAGE)
#define TAG_HASH_LAST    (TAG_HASH_DATA | NRF_CRACEN_CM_DMA_TAG_LAST)

/* AES DMA config words.
 * NRF_CRACEN_CM_AES_CONFIG(mode, key_sel, context_save, context_load, decrypt) */
#define AES_CBC_ENCRYPT  NRF_CRACEN_CM_AES_CONFIG(NRF_CRACEN_CM_AES_CONFIG_MODE_CBC, \
                         NRF_CRACEN_CM_AES_CONFIG_KEY_SW_PROGRAMMED, \
                         false, false, false)
#define AES_CBC_DECRYPT  NRF_CRACEN_CM_AES_CONFIG(NRF_CRACEN_CM_AES_CONFIG_MODE_CBC, \
                         NRF_CRACEN_CM_AES_CONFIG_KEY_SW_PROGRAMMED, \
                         false, false, true)

static bool cm_initialized;

/* Wait for CryptoMaster completion */
static int cm_wait(void)
{
    for (int i = 0; i < 10000; i++) {
        uint32_t pending = nrf_cracen_cm_int_pending_get(NRF_CRACENCORE);
        if (pending & (NRF_CRACEN_CM_INT_FETCH_ERROR_MASK |
                       NRF_CRACEN_CM_INT_PUSH_ERROR_MASK)) {
            return -1;
        }
        uint32_t busy = nrf_cracen_cm_status_get(NRF_CRACENCORE,
            NRF_CRACEN_CM_STATUS_BUSY_FETCH_MASK |
            NRF_CRACEN_CM_STATUS_BUSY_PUSH_MASK |
            NRF_CRACEN_CM_STATUS_PUSH_WAITING_MASK);
        if (!busy) return 0;
        k_busy_wait(1);
    }
    return -1;
}

/* Run a CryptoMaster DMA operation */
static int cm_run(struct nrf_cracen_cm_dma_desc *fetch,
                  struct nrf_cracen_cm_dma_desc *push)
{
    nrf_cracen_module_enable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);

    nrf_cracen_cm_fetch_addr_set(NRF_CRACENCORE, fetch);
    nrf_cracen_cm_push_addr_set(NRF_CRACENCORE, push);
    nrf_cracen_cm_config_indirect_set(NRF_CRACENCORE,
        (nrf_cracen_cm_config_indirect_mask_t)
        (NRF_CRACEN_CM_CONFIG_INDIRECT_FETCH_MASK |
         NRF_CRACEN_CM_CONFIG_INDIRECT_PUSH_MASK));

    __DMB();
    nrf_cracen_cm_start(NRF_CRACENCORE);

    int ret = cm_wait();

    nrf_cracen_cm_softreset(NRF_CRACENCORE);
    nrf_cracen_module_disable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);

    return ret;
}

int cracen_cm_init(void)
{
    cm_initialized = true;
    return 0;
}

int cracen_cm_available(void)
{
    return cm_initialized ? 1 : 0;
}

/* ---- SHA-256 ---- */
int cracen_cm_sha256(const uint8_t *data, size_t len, uint8_t *hash)
{
    if (!cm_initialized) return -1;

    uint32_t config = HASH_SHA256;

    struct nrf_cracen_cm_dma_desc in_descs[2];

    /* Descriptor 0: hash config word */
    in_descs[0].p_addr = (uint8_t *)&config;
    in_descs[0].length = 4 | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[0].dmatag = TAG_HASH_CONFIG;
    in_descs[0].p_next = &in_descs[1];

    /* Descriptor 1: message data — align to 4 bytes with IGN for padding bytes
     * (sdk-nrf: SET_LAST_DESC_IGN with CMDMA_BA413_BUS_MSK=3) */
    size_t aligned_len = (len + 3) & ~3;  /* round up to 4 bytes */
    size_t ign_bytes = aligned_len - len;
    in_descs[1].p_addr = (uint8_t *)(uintptr_t)data;
    in_descs[1].length = aligned_len | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[1].dmatag = TAG_HASH_LAST | (ign_bytes << 8);  /* IGN in bits 12:8 */
    in_descs[1].p_next = NRF_CRACEN_CM_DMA_DESC_STOP;

    /* Output: 32-byte digest */
    struct nrf_cracen_cm_dma_desc out_desc;
    out_desc.p_addr = hash;
    out_desc.length = 32;
    out_desc.dmatag = NRF_CRACEN_CM_DMA_TAG_LAST;
    out_desc.p_next = NRF_CRACEN_CM_DMA_DESC_STOP;

    return cm_run(in_descs, &out_desc);
}

/* ---- SHA-512 ---- */
int cracen_cm_sha512(const uint8_t *data, size_t len, uint8_t *hash)
{
    if (!cm_initialized) return -1;

    uint32_t config = HASH_SHA512;

    struct nrf_cracen_cm_dma_desc in_descs[2];

    in_descs[0].p_addr = (uint8_t *)&config;
    in_descs[0].length = 4 | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[0].dmatag = TAG_HASH_CONFIG;
    in_descs[0].p_next = &in_descs[1];

    /* Align to 4 bytes with IGN for padding */
    size_t aligned_len = (len + 3) & ~3;
    size_t ign_bytes = aligned_len - len;
    in_descs[1].p_addr = (uint8_t *)(uintptr_t)data;
    in_descs[1].length = aligned_len | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[1].dmatag = TAG_HASH_LAST | (ign_bytes << 8);
    in_descs[1].p_next = NRF_CRACEN_CM_DMA_DESC_STOP;

    struct nrf_cracen_cm_dma_desc out_desc;
    out_desc.p_addr = hash;
    out_desc.length = 64;
    out_desc.dmatag = NRF_CRACEN_CM_DMA_TAG_LAST;
    out_desc.p_next = NRF_CRACEN_CM_DMA_DESC_STOP;

    return cm_run(in_descs, &out_desc);
}

/* ---- AES-CBC (128/192/256) ----
 *
 * Matches the nrfx_cracen.c cm_aes_ecb reference: config + key + IV
 * + payload (with DMATAG_LAST) -> one output descriptor (also LAST
 * with REALIGN). No PRNG mask descriptor — the BA411 engine does not
 * require one for basic CBC operation. */
static int aes_cbc_op(const uint8_t *key, size_t key_len,
                      const uint8_t *iv,
                      const uint8_t *input, size_t len,
                      uint8_t *output, uint32_t config)
{
    if (!cm_initialized || len == 0 || len % 16 != 0) return -1;
    if (key_len != 16 && key_len != 24 && key_len != 32) return -1;

    struct nrf_cracen_cm_dma_desc in_descs[4];

    /* 0: Config (CBC mode + decrypt flag) */
    in_descs[0].p_addr = (uint8_t *)&config;
    in_descs[0].length = sizeof(config) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[0].dmatag = NRF_CRACEN_CM_DMA_TAG_AES_CONFIG(NRF_CRACEN_CM_AES_REG_OFFSET_CONFIG);
    in_descs[0].p_next = &in_descs[1];

    /* 1: Key (16/24/32 bytes for AES-128/192/256) */
    in_descs[1].p_addr = (uint8_t *)(uintptr_t)key;
    in_descs[1].length = key_len | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[1].dmatag = NRF_CRACEN_CM_DMA_TAG_AES_CONFIG(NRF_CRACEN_CM_AES_REG_OFFSET_KEY);
    in_descs[1].p_next = &in_descs[2];

    /* 2: IV (16 bytes) */
    in_descs[2].p_addr = (uint8_t *)(uintptr_t)iv;
    in_descs[2].length = 16 | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[2].dmatag = NRF_CRACEN_CM_DMA_TAG_AES_CONFIG(NRF_CRACEN_CM_AES_REG_OFFSET_IV);
    in_descs[2].p_next = &in_descs[3];

    /* 3: Payload with DMATAG_LAST — matches nrfx_cracen reference. */
    in_descs[3].p_addr = (uint8_t *)(uintptr_t)input;
    in_descs[3].length = len | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    in_descs[3].dmatag = NRF_CRACEN_CM_DMA_TAG_LAST
                         | NRF_CRACEN_CM_DMA_TAG_ENGINE_AES
                         | NRF_CRACEN_CM_DMA_TAG_DATATYPE_AES_PAYLOAD;
    in_descs[3].p_next = NRF_CRACEN_CM_DMA_DESC_STOP;

    struct nrf_cracen_cm_dma_desc out_desc;
    out_desc.p_addr = output;
    out_desc.length = len | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
    out_desc.dmatag = NRF_CRACEN_CM_DMA_TAG_LAST;
    out_desc.p_next = NRF_CRACEN_CM_DMA_DESC_STOP;

    return cm_run(in_descs, &out_desc);
}

int cracen_cm_aes_cbc_encrypt(const uint8_t *key, size_t key_len,
                               const uint8_t *iv,
                               const uint8_t *plaintext, size_t len,
                               uint8_t *ciphertext)
{
    return aes_cbc_op(key, key_len, iv, plaintext, len, ciphertext, AES_CBC_ENCRYPT);
}

int cracen_cm_aes_cbc_decrypt(const uint8_t *key, size_t key_len,
                               const uint8_t *iv,
                               const uint8_t *ciphertext, size_t len,
                               uint8_t *plaintext)
{
    return aes_cbc_op(key, key_len, iv, ciphertext, len, plaintext, AES_CBC_DECRYPT);
}
