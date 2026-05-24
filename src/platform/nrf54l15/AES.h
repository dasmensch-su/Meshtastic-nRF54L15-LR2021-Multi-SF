/*
 * AES.h — Arduino Crypto library shim with real AES for nRF54L15.
 *
 * Uses aes_impl.c (compact software AES-128/256) for the block cipher.
 * For hardware deployment, wire to nRF54L15 CRACEN.
 */
#pragma once

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
void aes_key_expansion(const uint8_t *key, uint8_t *roundKeys, int keyLen);
void aes_ecb_encrypt(const uint8_t *roundKeys, int Nr, const uint8_t *input, uint8_t *output);
#ifdef __cplusplus
}
#endif

class AESCommon {
    uint8_t roundKeys_[240]; /* max for AES-256: 15 * 16 = 240 bytes */
    int nr_ = 0;
    bool keySet_ = false;
public:
    virtual ~AESCommon() {}

    virtual bool setKey(const uint8_t *key, size_t len) {
        if (len != 16 && len != 32) return false;
        nr_ = (len == 16) ? 10 : 14;
        aes_key_expansion(key, roundKeys_, (int)len);
        keySet_ = true;
        return true;
    }

    virtual void encryptBlock(uint8_t *output, const uint8_t *input) {
        if (keySet_)
            aes_ecb_encrypt(roundKeys_, nr_, input, output);
        else
            memcpy(output, input, 16);
    }

    virtual void decryptBlock(uint8_t *output, const uint8_t *input) {
        /* CTR mode only uses encryptBlock; stub for decryptBlock */
        encryptBlock(output, input);
    }

    virtual size_t keySize() const = 0;
    virtual size_t blockSize() const { return 16; }
};

class AES128 : public AESCommon {
public:
    size_t keySize() const override { return 16; }
};

class AES256 : public AESCommon {
public:
    size_t keySize() const override { return 32; }
};

class AESSmall256 : public AES256 {};
