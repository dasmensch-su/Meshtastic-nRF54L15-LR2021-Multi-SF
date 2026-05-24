/*
 * CTR.h — Arduino Crypto CTR mode shim using real AES block cipher.
 *
 * Provides CTR<T> and CTRCommon matching the rweather/Crypto library API.
 * Uses the real AES block cipher from AES.h (mbedTLS-backed) for CTR mode.
 */
#pragma once

#include "AES.h"
#include <stdint.h>
#include <string.h>

class CTRCommon {
    uint8_t iv_[16] = {};
    uint8_t counter_[16] = {};
    uint8_t keystreamBlock_[16] = {};
    size_t counterSize_ = 4;
    bool keySet_ = false;

protected:
    AESCommon *blockCipher_ = nullptr;

    void ctrProcess(uint8_t *output, const uint8_t *input, size_t len)
    {
        if (!keySet_ || !blockCipher_) {
            memcpy(output, input, len);
            return;
        }
        /* Reset counter to IV at start of each encrypt/decrypt call */
        memcpy(counter_, iv_, 16);

        size_t offset = 0;
        while (offset < len) {
            /* Encrypt counter block to produce keystream */
            blockCipher_->encryptBlock(keystreamBlock_, counter_);

            /* XOR keystream with input */
            size_t blockBytes = (len - offset < 16) ? (len - offset) : 16;
            for (size_t i = 0; i < blockBytes; i++)
                output[offset + i] = input[offset + i] ^ keystreamBlock_[i];
            offset += blockBytes;

            /* Increment counter (last counterSize_ bytes, big-endian) */
            for (int i = 15; i >= (int)(16 - counterSize_); i--) {
                if (++counter_[i] != 0) break;
            }
        }
    }

public:
    virtual ~CTRCommon() {}

    virtual bool setKey(const uint8_t *key, size_t len) {
        if (blockCipher_) {
            keySet_ = blockCipher_->setKey(key, len);
            return keySet_;
        }
        return false;
    }

    virtual bool setIV(const uint8_t *iv, size_t len) {
        size_t copyLen = (len < 16) ? len : 16;
        memset(iv_, 0, 16);
        memcpy(iv_, iv, copyLen);
        return true;
    }

    virtual void setCounterSize(size_t size) { counterSize_ = size; }

    virtual void encrypt(uint8_t *output, const uint8_t *input, size_t len) {
        ctrProcess(output, input, len);
    }

    virtual void decrypt(uint8_t *output, const uint8_t *input, size_t len) {
        /* CTR mode: decrypt == encrypt */
        ctrProcess(output, input, len);
    }
};

template <typename T>
class CTR : public CTRCommon {
    T cipher_;
public:
    CTR() { blockCipher_ = &cipher_; }

    bool setKey(const uint8_t *key, size_t len) override {
        return CTRCommon::setKey(key, len);
    }
};
