#ifndef CRYPTO_HASH_h
#define CRYPTO_HASH_h

#include <inttypes.h>
#include <stddef.h>

class Hash
{
public:
    Hash() {}
    virtual ~Hash() {}

    virtual size_t hashSize() const = 0;
    virtual size_t blockSize() const = 0;

    virtual void reset() = 0;
    virtual void update(const void *data, size_t len) = 0;
    virtual void finalize(void *hash, size_t len) = 0;

    virtual void clear() = 0;

    virtual void resetHMAC(const void *key, size_t keyLen) = 0;
    virtual void finalizeHMAC(const void *key, size_t keyLen, void *hash, size_t hashLen) = 0;
};

#endif
