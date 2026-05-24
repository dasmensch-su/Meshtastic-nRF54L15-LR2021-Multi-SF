/*
 * cracen_ed25519.c — Hardware Ed25519 via CRACEN BA414EP PKE
 *
 * Implements Ed25519 keygen/sign/verify using the BA414EP public key
 * engine on nRF54L15. Microcode loaded from compiled-in binary.
 *
 * The PKE handles ECC point operations (scalar*G, (r+k*s) mod L, verify).
 * SHA-512 hashing goes through the CRACEN CryptoMaster BA413 engine
 * (cracen_cm_sha512) so this file has no PSA / Mbed TLS dependency.
 *
 * Register interface reverse-engineered from Nordic sdk-nrf source
 * (silexpk driver) + hardware probing. No proprietary binary blobs.
 */

#include "cracen_ed25519.h"
#include "cracen_cm.h"
#include "microcode_binary.h"
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <string.h>

/* ---- CRACEN registers ---- */
#define CRACEN_ENABLE     (*(volatile uint32_t *)0x50048400UL)
#define PK_CONFIG         (*(volatile uint32_t *)0x51802000UL)
#define PK_COMMAND        (*(volatile uint32_t *)0x51802004UL)
#define PK_CONTROL        (*(volatile uint32_t *)0x51802008UL)
#define PK_STATUS         (*(volatile uint32_t *)0x5180200CUL)
#define PK_OPSIZE         (*(volatile uint32_t *)0x5180201CUL)
#define CRYPTORAM         ((volatile uint8_t *)0x51808000UL)
#define CRYPTORAM32       ((volatile uint32_t *)0x51808000UL)
#define UCODE_ADDR        ((volatile uint32_t *)0x5180C000UL)

#define SLOT_SZ           512
#define CRYPTORAM_WORDS   (16384 / 4)

/* PKE opcodes (bare — no SELCURVE flags, manual curve params) */
#define CMD_ED_PTMUL      (0x3B | (31 << 8) | (1U << 31))  /* EdDSA ptmul + RESQUARE_R */
#define CMD_ED_SIGN       (0x3C | (31 << 8) | (1U << 31))  /* EdDSA sign */
#define CMD_ED_VERIFY     (0x3D | (31 << 8) | (1U << 31))  /* EdDSA verify */
#define CMD_FLAG_AX_LSB   (1U << 29)
#define CMD_FLAG_RX_LSB   (1U << 30)
#define CMD_MG_PTMUL      (0x28 | (31 << 8) | (1U << 31))  /* Montgomery ptmul + RESQUARE_R */

/* ---- Ed25519 curve constants (LE, verified against Python) ---- */
static const uint8_t ED_P[32]  = {0xed,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f};
static const uint8_t ED_L[32]  = {0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10};
static const uint8_t ED_GX[32] = {0x1a,0xd5,0x25,0x8f,0x60,0x2d,0x56,0xc9,0xb2,0xa7,0x25,0x95,0x60,0xc7,0x2c,0x69,0x5c,0xdc,0xd6,0xfd,0x31,0xe2,0xa4,0xc0,0xfe,0x53,0x6e,0xcd,0xd3,0x36,0x69,0x21};
static const uint8_t ED_GY[32] = {0x58,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66};
static const uint8_t ED_D[32]  = {0xa3,0x78,0x59,0x13,0xca,0x4d,0xeb,0x75,0xab,0xd8,0x41,0x41,0x4d,0x0a,0x70,0x00,0x98,0xe8,0x79,0x77,0x79,0x40,0xc7,0x8c,0x73,0xfe,0x6f,0x2b,0xee,0x6c,0x03,0x52};
static const uint8_t ED_I[32]  = {0xb0,0xa0,0x0e,0x4a,0x27,0x1b,0xee,0xc4,0x78,0xe4,0x2f,0xad,0x06,0x18,0x43,0x2f,0xa7,0xd7,0xfb,0x3d,0x99,0x00,0x4d,0x2b,0x0b,0xdf,0xc1,0x4f,0x80,0x24,0x83,0x2b};

/* ---- Curve25519 Montgomery params (for X25519 ECDH) ---- */
static const uint8_t MG_A[32]  = {0x06,0x6d,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

static bool cracen_initialized;

/* ---- Low-level helpers ---- */

static void slot_write(int slot, const uint8_t *data, int len)
{
    int off = slot * SLOT_SZ;
    for (int i = 0; i < len && i < 32; i++)
        CRYPTORAM[off + i] = data[i];
}

static void slot_read(int slot, uint8_t *data)
{
    int off = slot * SLOT_SZ;
    for (int i = 0; i < 32; i++)
        data[i] = CRYPTORAM[off + i];
}

static void load_curve_params(void)
{
    slot_write(0, ED_P, 32);
    slot_write(1, ED_L, 32);
    slot_write(2, ED_GX, 32);
    slot_write(3, ED_GY, 32);
    slot_write(4, ED_D, 32);
    slot_write(5, ED_I, 32);
}

static void clear_cryptoram(void)
{
    for (int i = 0; i < CRYPTORAM_WORDS; i++)
        CRYPTORAM32[i] = 0;
}

static int pk_run(uint32_t cmd)
{
    PK_OPSIZE = 0x200;
    PK_COMMAND = cmd;
    __DMB();
    PK_CONTROL = 3; /* START + CLEAR_IRQ */

    for (int i = 0; i < 5000; i++) {
        uint32_t s = PK_STATUS;
        if (!(s & 0x00010000)) {
            return (s >> 4) & 0xFFF; /* error flags */
        }
        k_usleep(100);
    }
    return -1; /* timeout */
}

static int sha512(const uint8_t *data, size_t len, uint8_t *hash)
{
    return cracen_cm_sha512(data, len, hash);
}

static void ed25519_clamp(uint8_t *scalar)
{
    scalar[0] &= 248;
    scalar[31] &= 127;
    scalar[31] |= 64;
}

/* ---- Public API ---- */

int cracen_ed25519_init(void)
{
    /* Enable CRACEN PKE/IKG module (bit 2) — don't touch CryptoMaster/RNG
     * which may be managed by Mbed TLS/entropy drivers */
    CRACEN_ENABLE |= 4; /* PKEIKG */
    k_msleep(5);

    /* Load microcode into PKE code memory */
    for (size_t i = 0; i < BA414EP_UCODE_SIZE; i++)
        UCODE_ADDR[i] = ba414ep_ucode[i];

    cracen_initialized = true;
    return 0;
}

int cracen_ed25519_available(void)
{
    return cracen_initialized ? 1 : 0;
}

int cracen_ed25519_pubkey(const uint8_t *private_key, uint8_t *public_key)
{
    if (!cracen_initialized) return -1;

    /* Re-enable CRACEN PKE only (bit 2 = PKEIKG), don't touch CryptoMaster */
    CRACEN_ENABLE |= 4; /* PKEIKG only */

    /* h = SHA-512(seed) */
    uint8_t h[64];
    if (sha512(private_key, 32, h)) return -1;

    /* Clamp scalar */
    uint8_t scalar[32];
    memcpy(scalar, h, 32);
    ed25519_clamp(scalar);

    /* PTMUL: scalar * G */
    clear_cryptoram();
    load_curve_params();
    slot_write(8, scalar, 32);

    int err = pk_run(CMD_ED_PTMUL);
    memset(scalar, 0, 32);
    memset(h, 0, 64);
    if (err) return -1;

    /* Read result and encode: Ry with x-sign bit */
    uint8_t Rx[32], Ry[32];
    slot_read(10, Rx);
    slot_read(11, Ry);
    memcpy(public_key, Ry, 32);
    public_key[31] |= (Rx[0] & 1) << 7;

    return 0;
}

int cracen_ed25519_generate(uint8_t *private_key, uint8_t *public_key)
{
    sys_rand_get(private_key, 32);
    return cracen_ed25519_pubkey(private_key, public_key);
}

int cracen_ed25519_sign(const uint8_t *private_key,
                        const uint8_t *public_key,
                        const uint8_t *message, size_t msg_len,
                        uint8_t *signature)
{
    if (!cracen_initialized) return -1;

    /* h = SHA-512(seed) */
    uint8_t h[64];
    if (sha512(private_key, 32, h)) return -1;

    uint8_t scalar[32];
    memcpy(scalar, h, 32);
    ed25519_clamp(scalar);
    uint8_t *prefix = h + 32; /* second half of SHA-512(seed) */

    /* r = SHA-512(prefix || message) — deterministic nonce */
    uint8_t *r_input = k_malloc(32 + msg_len);
    if (!r_input) { memset(h, 0, 64); return -1; }
    memcpy(r_input, prefix, 32);
    memcpy(r_input + 32, message, msg_len);
    uint8_t r_full[64];
    int ret = sha512(r_input, 32 + msg_len, r_full);
    k_free(r_input);
    if (ret) { memset(h, 0, 64); return -1; }

    /* R = r * G (point multiply) */
    clear_cryptoram();
    load_curve_params();
    slot_write(8, r_full, 32);
    slot_write(9, r_full + 32, 32);

    if (pk_run(CMD_ED_PTMUL)) { memset(h, 0, 64); memset(r_full, 0, 64); return -1; }

    /* Encode R → signature[0:32] */
    uint8_t Rx[32], Ry[32];
    slot_read(10, Rx);
    slot_read(11, Ry);
    memcpy(signature, Ry, 32);
    signature[31] |= (Rx[0] & 1) << 7;

    /* k = SHA-512(R || public_key || message) */
    uint8_t *k_input = k_malloc(32 + 32 + msg_len);
    if (!k_input) { memset(h, 0, 64); memset(r_full, 0, 64); return -1; }
    memcpy(k_input, signature, 32);
    memcpy(k_input + 32, public_key, 32);
    memcpy(k_input + 64, message, msg_len);
    uint8_t k_full[64];
    ret = sha512(k_input, 32 + 32 + msg_len, k_full);
    k_free(k_input);
    if (ret) { memset(h, 0, 64); memset(r_full, 0, 64); return -1; }

    /* S = (r + k * scalar) mod L — hardware sign */
    clear_cryptoram();
    load_curve_params();
    slot_write(6, k_full, 32);
    slot_write(7, k_full + 32, 32);
    slot_write(8, r_full, 32);
    slot_write(9, r_full + 32, 32);
    slot_write(11, scalar, 32);

    ret = pk_run(CMD_ED_SIGN);

    /* Clean sensitive data */
    memset(h, 0, 64);
    memset(scalar, 0, 32);
    memset(r_full, 0, 64);
    memset(k_full, 0, 64);

    if (ret) return -1;

    /* Read sig_s → signature[32:64] */
    slot_read(10, signature + 32);
    return 0;
}

int cracen_ed25519_verify(const uint8_t *public_key,
                          const uint8_t *message, size_t msg_len,
                          const uint8_t *signature)
{
    if (!cracen_initialized) return -1;

    /* k = SHA-512(R || public_key || message) */
    uint8_t *k_input = k_malloc(32 + 32 + msg_len);
    if (!k_input) return -1;
    memcpy(k_input, signature, 32);       /* R */
    memcpy(k_input + 32, public_key, 32);
    memcpy(k_input + 64, message, msg_len);
    uint8_t k_full[64];
    int ret = sha512(k_input, 32 + 32 + msg_len, k_full);
    k_free(k_input);
    if (ret) return -1;

    /* Extract Ay from public key (y-coord with x-sign bit cleared) */
    uint8_t Ay[32];
    memcpy(Ay, public_key, 32);
    uint8_t ax_lsb = (Ay[31] >> 7) & 1;
    Ay[31] &= 0x7F;

    /* Extract Ry from signature R (y-coord with x-sign bit cleared) */
    uint8_t Ry[32];
    memcpy(Ry, signature, 32);
    uint8_t rx_lsb = (Ry[31] >> 7) & 1;
    Ry[31] &= 0x7F;

    /* Hardware verify */
    clear_cryptoram();
    load_curve_params();
    slot_write(6, k_full, 32);
    slot_write(7, k_full + 32, 32);
    slot_write(9, Ay, 32);
    slot_write(10, signature + 32, 32); /* sig_s */
    slot_write(11, Ry, 32);

    uint32_t cmd = CMD_ED_VERIFY;
    if (ax_lsb) cmd |= CMD_FLAG_AX_LSB;
    if (rx_lsb) cmd |= CMD_FLAG_RX_LSB;

    return pk_run(cmd) == 0 ? 0 : -1;
}

/* ---- X25519 ECDH ---- */

static void load_montgomery_params(void)
{
    /* Curve25519 Montgomery curve: By² = x³ + Ax² + x
     * Slot 0: p (same as Ed25519)
     * Slot 1: A (486662) */
    slot_write(0, ED_P, 32);
    slot_write(1, MG_A, 32);
}

int cracen_x25519_scalarmult(const uint8_t *scalar,
                             const uint8_t *point,
                             uint8_t *result)
{
    if (!cracen_initialized) {
        return -1;
    }

    CRACEN_ENABLE |= 7;

    clear_cryptoram();
    load_montgomery_params();

    /* Slot 6 (PTR_A): point u-coordinate (LE, 32 bytes) */
    uint8_t pt[32];
    memcpy(pt, point, 32);
    pt[31] &= 0x7F; /* clamp point: clear top bit */
    slot_write(6, pt, 32);

    /* Slot 8 (PTR_B): scalar (LE, 32 bytes) */
    uint8_t k[32];
    memcpy(k, scalar, 32);
    k[0] &= 0xF8;          /* clamp scalar: clear bits 0,1,2 */
    k[31] = (k[31] | 0x40) & 0x7F; /* set bit 254, clear bit 255 */
    slot_write(8, k, 32);

    /* Montgomery PTMUL needs PK_CONFIG set (unlike EdDSA which uses fixed slots) */
    /* PK_CONFIG = ptrA | (ptrB << 8) | (ptrC << 16) */
    PK_CONFIG = 6 | (8 << 8) | (10 << 16);

    int err = pk_run(CMD_MG_PTMUL);
    memset(k, 0, 32);
    if (err) return -1;

    /* Read result from slot 10 (PTR_C) */
    slot_read(10, result);
    return 0;
}
