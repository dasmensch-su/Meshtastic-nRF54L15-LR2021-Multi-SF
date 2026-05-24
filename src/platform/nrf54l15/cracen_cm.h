/*
 * cracen_cm.h — CRACEN CryptoMaster hardware SHA/AES/HMAC
 *
 * Uses the BA413 hash engine and AES engine in the CryptoMaster block
 * of nRF54L15's CRACEN for hardware-accelerated symmetric crypto.
 */

#ifndef CRACEN_CM_H
#define CRACEN_CM_H

#include <stdint.h>
#include <stddef.h>

#ifndef ARCH_NRF54L15
/* Stubs when CRACEN hardware is not available */
static inline int cracen_cm_init(void) { return -1; }
static inline int cracen_cm_available(void) { return 0; }
static inline int cracen_cm_sha256(const uint8_t *d, size_t l, uint8_t *h) { return -1; }
static inline int cracen_cm_sha512(const uint8_t *d, size_t l, uint8_t *h) { return -1; }
static inline int cracen_cm_aes_cbc_encrypt(const uint8_t *k, size_t kl,
    const uint8_t *iv, const uint8_t *p, size_t l, uint8_t *c) { return -1; }
static inline int cracen_cm_aes_cbc_decrypt(const uint8_t *k, size_t kl,
    const uint8_t *iv, const uint8_t *c, size_t l, uint8_t *p) { return -1; }
#else

/* Initialize CryptoMaster (call once at boot) */
int cracen_cm_init(void);

/* Check if CryptoMaster is available */
int cracen_cm_available(void);

/* SHA-256: hash → 32 bytes */
int cracen_cm_sha256(const uint8_t *data, size_t len, uint8_t *hash);

/* SHA-512: hash → 64 bytes */
int cracen_cm_sha512(const uint8_t *data, size_t len, uint8_t *hash);

/* AES-CBC encrypt (AES-128/192/256, data must be pre-padded to block boundary) */
int cracen_cm_aes_cbc_encrypt(const uint8_t *key, size_t key_len,
                               const uint8_t *iv,
                               const uint8_t *plaintext, size_t len,
                               uint8_t *ciphertext);

/* AES-CBC decrypt */
int cracen_cm_aes_cbc_decrypt(const uint8_t *key, size_t key_len,
                               const uint8_t *iv,
                               const uint8_t *ciphertext, size_t len,
                               uint8_t *plaintext);

#endif /* ARCH_NRF54L15 */
#endif /* CRACEN_CM_H */
