#ifndef CRYPTO_RNG_h
#define CRYPTO_RNG_h

#include <inttypes.h>
#include <stddef.h>

class NoiseSource;

class RNGClass
{
public:
    RNGClass() {}
    ~RNGClass() {}
    void begin(const char *) {}
    void addNoiseSource(NoiseSource &) {}
    void setAutoSaveTime(uint16_t) {}
    void rand(uint8_t *, size_t) {}
    bool available(size_t) const { return false; }
    void stir(const uint8_t *, size_t, unsigned int = 0) {}
    void save() {}
    void loop() {}
    void destroy() {}
    static const int SEED_SIZE = 48;
};

#ifndef RNG
extern RNGClass RNG;
#endif

#endif
