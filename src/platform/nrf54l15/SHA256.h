#ifndef CRYPTO_SHA256_h
#define CRYPTO_SHA256_h

#include "Hash.h"
#include <string.h>

extern "C" {
#include "cracen_cm.h"
}

class SHA256 : public Hash
{
public:
    SHA256() { reset(); }
    virtual ~SHA256() { clear(); }

    size_t hashSize() const { return HASH_SIZE; }
    size_t blockSize() const { return BLOCK_SIZE; }

    void reset()
    {
        buf_len = 0;
        memset(buf, 0, sizeof(buf));
    }

    void update(const void *data, size_t len)
    {
        if (buf_len + len > sizeof(buf))
            len = sizeof(buf) - buf_len;
        memcpy(buf + buf_len, data, len);
        buf_len += len;
    }

    void finalize(void *hash, size_t len)
    {
        uint8_t digest[HASH_SIZE];
        cracen_cm_sha256(buf, buf_len, digest);
        if (len > HASH_SIZE)
            len = HASH_SIZE;
        memcpy(hash, digest, len);
        reset();
    }

    void clear()
    {
        memset(buf, 0, sizeof(buf));
        buf_len = 0;
    }

    void resetHMAC(const void *key, size_t keyLen) { (void)key; (void)keyLen; }
    void finalizeHMAC(const void *key, size_t keyLen, void *hash, size_t hashLen)
    {
        (void)key; (void)keyLen; (void)hash; (void)hashLen;
    }

    static const size_t HASH_SIZE  = 32;
    static const size_t BLOCK_SIZE = 64;

private:
    uint8_t buf[256];
    size_t buf_len;
};

#endif
