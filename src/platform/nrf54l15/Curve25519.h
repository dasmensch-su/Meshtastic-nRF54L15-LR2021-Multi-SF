#ifndef CRYPTO_CURVE25519_h
#define CRYPTO_CURVE25519_h

#include "BigNumberUtil.h"

class Curve25519
{
public:
    static bool eval(uint8_t result[32], const uint8_t s[32], const uint8_t x[32]);
    static void dh1(uint8_t k[32], uint8_t f[32]);
    static bool dh2(uint8_t k[32], uint8_t f[32]);
    static uint8_t isWeakPoint(const uint8_t k[32]);

private:
    Curve25519() {}
    ~Curve25519() {}
};

#endif
