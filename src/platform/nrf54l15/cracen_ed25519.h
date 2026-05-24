/*
 * cracen_ed25519.h — Hardware Ed25519 via CRACEN BA414EP PKE
 *
 * Uses the BA414EP public key engine on nRF54L15 for Ed25519.
 * Requires: CRACEN enabled, microcode loaded, PK_OPSIZE set.
 * Falls back to software (TweetNaCl) on chips without CRACEN.
 */

#ifndef CRACEN_ED25519_H
#define CRACEN_ED25519_H

#include <stdint.h>
#include <stddef.h>

#ifndef ARCH_NRF54L15
/* Stubs when CRACEN hardware is not available (e.g. nRF52840) */
static inline int cracen_ed25519_init(void) { return -1; }
static inline int cracen_ed25519_available(void) { return 0; }
static inline int cracen_ed25519_generate(uint8_t *pk, uint8_t *sk) { return -1; }
static inline int cracen_ed25519_pubkey(const uint8_t *pk, uint8_t *sk) { return -1; }
static inline int cracen_ed25519_sign(const uint8_t *pk, const uint8_t *sk,
    const uint8_t *m, size_t ml, uint8_t *s) { return -1; }
static inline int cracen_ed25519_verify(const uint8_t *pk,
    const uint8_t *m, size_t ml, const uint8_t *s) { return -1; }
static inline int cracen_x25519_scalarmult(const uint8_t *sc,
    const uint8_t *pt, uint8_t *r) { return -1; }
#else

/* Initialize CRACEN PKE: enable peripheral, load microcode, set slot size.
 * Call once at boot, after Zephyr kernel is running. Returns 0 on success. */
int cracen_ed25519_init(void);

/* Check if CRACEN Ed25519 is available (init succeeded) */
int cracen_ed25519_available(void);

/* Generate Ed25519 keypair.
 * private_key: 32-byte seed (output)
 * public_key: 32-byte compressed point (output) */
int cracen_ed25519_generate(uint8_t *private_key, uint8_t *public_key);

/* Compute public key from private key seed.
 * private_key: 32-byte seed
 * public_key: 32-byte compressed point (output) */
int cracen_ed25519_pubkey(const uint8_t *private_key, uint8_t *public_key);

/* Sign a message.
 * private_key: 32-byte seed
 * public_key: 32-byte compressed point
 * message: data to sign
 * msg_len: length of message
 * signature: 64-byte output (R || S) */
int cracen_ed25519_sign(const uint8_t *private_key,
                        const uint8_t *public_key,
                        const uint8_t *message, size_t msg_len,
                        uint8_t *signature);

/* Verify a signature.
 * public_key: 32-byte compressed point
 * message: signed data
 * msg_len: length of message
 * signature: 64-byte signature (R || S)
 * Returns 0 on success, -1 on failure. */
int cracen_ed25519_verify(const uint8_t *public_key,
                          const uint8_t *message, size_t msg_len,
                          const uint8_t *signature);

/* ---- X25519 ECDH via CRACEN PKE ---- */

/* X25519 scalar multiplication: result = scalar * point
 * scalar: 32-byte private key (will be clamped internally)
 * point: 32-byte u-coordinate of peer's public key
 * result: 32-byte u-coordinate output (shared secret)
 * Returns 0 on success, -1 on failure. */
int cracen_x25519_scalarmult(const uint8_t *scalar,
                             const uint8_t *point,
                             uint8_t *result);

#endif /* ARCH_NRF54L15 */
#endif /* CRACEN_ED25519_H */
