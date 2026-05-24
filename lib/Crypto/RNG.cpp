/*
 * RNG shim for Zephyr — replaces Arduino RNG with Zephyr entropy API.
 * The nRF54L15 CRACEN provides hardware TRNG via CONFIG_ENTROPY_NRF_CRACEN_CTR_DRBG.
 *
 * SPDX-License-Identifier: MIT (matching rweather/Crypto library license)
 */

#include "RNG.h"
#include <zephyr/random/random.h>
#include <string.h>

RNGClass::RNGClass()
    : credits(0), firstSave(0), initialized(0), trngPending(0),
      timer(0), timeout(0), count(0), trngPosn(0)
{
    memset(block, 0, sizeof(block));
    memset(stream, 0, sizeof(stream));
    memset(noiseSources, 0, sizeof(noiseSources));
}

RNGClass::~RNGClass() {}

void RNGClass::begin(const char *tag)
{
    (void)tag;
    /* Seed from Zephyr hardware entropy (CRACEN TRNG on nRF54L15) */
    sys_rand_get(block, sizeof(block));
    initialized = 1;
}

void RNGClass::addNoiseSource(NoiseSource &source)
{
    (void)source;
}

void RNGClass::setAutoSaveTime(uint16_t minutes)
{
    (void)minutes;
}

void RNGClass::rand(uint8_t *data, size_t len)
{
    /* Use Zephyr's CSPRNG directly — backed by CRACEN CTR-DRBG on hardware */
    sys_rand_get(data, len);
}

bool RNGClass::available(size_t len) const
{
    (void)len;
    return true; /* Zephyr entropy is always available */
}

void RNGClass::stir(const uint8_t *data, size_t len, unsigned int credit)
{
    (void)data;
    (void)len;
    (void)credit;
    /* No-op — Zephyr entropy pool handles its own reseeding */
}

void RNGClass::save() {}
void RNGClass::loop() {}
void RNGClass::destroy() {}

RNGClass RNG;
