/*
 * arduino_compat.h — Arduino API shims for the Zephyr nRF54L15 port.
 *
 * Provides the small surface of Arduino-isms that Meshtastic's cooperative
 * threading layer (OSThread / NotifiedWorkerThread / InterruptableDelay)
 * depends on, without pulling in any Arduino toolchain.
 */
#pragma once

#ifdef ARCH_NRF54L15

#include <zephyr/kernel.h>
#include <stdint.h>

/* millis() — milliseconds since boot */
static inline unsigned long millis(void)
{
    return (unsigned long)k_uptime_get_32();
}

/* delay() — blocking sleep in milliseconds */
static inline void delay(uint32_t ms)
{
    k_msleep((int32_t)ms);
}

/* yield() — cooperative yield to other threads */
static inline void yield(void)
{
    k_yield();
}

/*
 * IRAM_ATTR — ESP32 attribute that places ISR functions in IRAM.
 * Unused on nRF54L15 (all flash is executable); define as empty.
 */
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#endif /* ARCH_NRF54L15 */
