#pragma once

/*
 * nRF54L15 platform architecture defines for Meshtastic.
 * Replaces src/platform/nrf52/architecture.h for the Zephyr-based nRF54L15 port.
 */

#ifndef ARCH_NRF54L15
#define ARCH_NRF54L15
#endif

/* Default ringtone for ExternalNotificationModule */
#ifndef USERPREFS_RINGTONE_RTTTL
#define USERPREFS_RINGTONE_RTTTL ""
#endif

/*
 * Phase 1: enable only what is needed to boot and print the banner.
 * Later phases will enable BLE, GPS, display, etc.
 */
#ifndef HAS_BLUETOOTH
#define HAS_BLUETOOTH 1      /* Phase 3 — BLE via Zephyr BLE stack */
#endif
#ifndef HAS_SCREEN
#define HAS_SCREEN 1
#endif
#ifndef USE_SSD1306
#define USE_SSD1306 1
#endif
#ifndef HAS_WIRE
#define HAS_WIRE 0
#endif
#ifndef HAS_GPS
#define HAS_GPS 0
#endif
#ifndef HAS_BUTTON
#define HAS_BUTTON 1
#endif
#ifndef HAS_TELEMETRY
#define HAS_TELEMETRY 1
#endif
#ifndef HAS_SENSOR
#define HAS_SENSOR 0
#endif
#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif
#ifndef HAS_CPU_SHUTDOWN
#define HAS_CPU_SHUTDOWN 0
#endif
/*
 * nRF54L15 has CRACEN hardware crypto. Full integration deferred to Phase 4.
 */
#ifndef HAS_CUSTOM_CRYPTO_ENGINE
#if defined(CONFIG_NRF54L15_HARDWARE_CRYPTO) && CONFIG_NRF54L15_HARDWARE_CRYPTO
#define HAS_CUSTOM_CRYPTO_ENGINE 1
#endif
#endif

/*
 * Logging: map Meshtastic LOG_* macros to Zephyr printk for Phase 1.
 * Phase 2+ can switch to CONFIG_LOG/LOG_MODULE_REGISTER.
 */
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>  /* IS_ENABLED() */

/* Log macros use logLegacy() to support both string literals and variables.
 * printk("X " fmt "\n") only works when fmt is a literal; logLegacy() uses
 * vprintk and handles both cases. Defined in mesh_zephyr.cpp. */
#ifdef __cplusplus
extern "C" {
#endif
void logLegacy(const char *level, const char *fmt, ...);
/* Thin printk wrapper retained for source compatibility with call
 * sites from the old silent-gate era. Logs now target SEGGER RTT
 * (see mesh_zephyr.cpp and prj_hardware.conf), so there's no longer
 * a reason to filter them differently from plain printk. */
void qprintk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif
/* When CONFIG_MESHTASTIC_DEBUG_LOGGING=n the LOG_* macros compile to
 * no-ops so the format strings and argument evaluations get dead-
 * stripped. Saves ~120 KB of flash in production builds along with
 * the unlinked RTT/LOG subsystems. */
#if IS_ENABLED(CONFIG_MESHTASTIC_DEBUG_LOGGING)
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...)  logLegacy("I", fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...)  logLegacy("W", fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) logLegacy("E", fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) logLegacy("D", fmt, ##__VA_ARGS__)
#endif
#else
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...)  ((void)0)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...)  ((void)0)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) ((void)0)
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif
#endif

/*
 * Detect if running in ISR context (ARM Cortex-M33 on nRF54L15).
 * Equivalent to the nRF52840 definition in src/platform/nrf52/architecture.h.
 */
#include <zephyr/kernel.h>
#define xPortInIsrContext() (k_is_in_isr() ? pdTRUE : pdFALSE)

/*
 * Arduino compatibility shims: millis(), delay(), yield(), IRAM_ATTR.
 * Required by Meshtastic's cooperative threading layer (OSThread,
 * NotifiedWorkerThread, InterruptableDelay) when building for Zephyr.
 */
#include "arduino_compat.h"

/* IF_SCREEN / IF_ROUTER macros — used by modules. meshUtils.h defines them
 * but is not always included by configuration.h. Provide fallbacks here. */
#ifndef IF_SCREEN
#if HAS_SCREEN
#define IF_SCREEN(X) if (screen) { X; }
#else
#define IF_SCREEN(...)
#endif
#endif
