/*
 * DebugConfiguration.h — Zephyr stub for nRF54L15 port.
 *
 * Shadows src/DebugConfiguration.h to provide LOG_* macros via Zephyr's
 * printk WITHOUT pulling in SerialConsole.h → RedirectablePrint.h →
 * ../freertosinc.h (which does not exist at the expected path in the
 * Zephyr build).
 *
 * The real src/DebugConfiguration.h is only valid for Arduino/PlatformIO
 * builds where the full SerialConsole stack is compiled.
 */
#pragma once

#ifdef ARCH_NRF54L15

#include <zephyr/sys/printk.h>
#include <stdarg.h>

/* ---- Log level strings (match the upstream constants) ---- */
#define MESHTASTIC_LOG_LEVEL_DEBUG "DEBUG"
#define MESHTASTIC_LOG_LEVEL_INFO  "INFO "
#define MESHTASTIC_LOG_LEVEL_WARN  "WARN "
#define MESHTASTIC_LOG_LEVEL_ERROR "ERROR"
#define MESHTASTIC_LOG_LEVEL_CRIT  "CRIT "
#define MESHTASTIC_LOG_LEVEL_TRACE "TRACE"
#define MESHTASTIC_LOG_LEVEL_HEAP  "HEAP "

/* ---- LOG_* macros — printk-backed ---- */
#define LOG_DEBUG(fmt, ...) printk("[DBG] " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  printk("[INF] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  printk("[WRN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printk("[ERR] " fmt "\n", ##__VA_ARGS__)
#define LOG_CRIT(fmt, ...)  printk("[CRT] " fmt "\n", ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) printk("[TRC] " fmt "\n", ##__VA_ARGS__)
#define LOG_HEAP(...)

/* ---- Heap debug stubs ---- */
#define DEBUG_HEAP_BEFORE
#define DEBUG_HEAP_AFTER(context, ptr)

/* ---- Serial baud / LED (referenced by some headers) ---- */
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
#ifndef LED_STATE_ON
#define LED_STATE_ON 1
#endif

/*
 * logLegacy — C-linkage wrapper for LOG_DEBUG; defined in mesh_zephyr.cpp.
 * Declared here to satisfy #include chains from files that pull in
 * DebugConfiguration.h.
 */
extern "C" void logLegacy(const char *level, const char *fmt, ...);

/* ---- Syslog priority constants (not used on Zephyr, but declared to
 * satisfy headers that reference them via DebugConfiguration.h) ---- */
#define SYSLOG_NILVALUE "-"
#define SYSLOG_CRIT  2
#define SYSLOG_ERR   3
#define SYSLOG_WARN  4
#define SYSLOG_INFO  6
#define SYSLOG_DEBUG 7

#define LOG_PRIMASK  0x07
#define LOG_PRI(p)   ((p) & LOG_PRIMASK)
#define LOG_MAKEPRI(fac, pri) (((fac) << 3) | (pri))

#define LOGLEVEL_KERN     (0  << 3)
#define LOGLEVEL_USER     (1  << 3)
#define LOGLEVEL_MAIL     (2  << 3)
#define LOGLEVEL_DAEMON   (3  << 3)
#define LOGLEVEL_AUTH     (4  << 3)
#define LOGLEVEL_SYSLOG   (5  << 3)
#define LOGLEVEL_LPR      (6  << 3)
#define LOGLEVEL_NEWS     (7  << 3)
#define LOGLEVEL_UUCP     (8  << 3)
#define LOGLEVEL_CRON     (9  << 3)
#define LOGLEVEL_AUTHPRIV (10 << 3)
#define LOGLEVEL_FTP      (11 << 3)
#define LOGLEVEL_LOCAL0   (16 << 3)
#define LOGLEVEL_LOCAL1   (17 << 3)
#define LOGLEVEL_LOCAL2   (18 << 3)
#define LOGLEVEL_LOCAL3   (19 << 3)
#define LOGLEVEL_LOCAL4   (20 << 3)
#define LOGLEVEL_LOCAL5   (21 << 3)
#define LOGLEVEL_LOCAL6   (22 << 3)
#define LOGLEVEL_LOCAL7   (23 << 3)
#define LOG_NFACILITIES   24
#define LOG_FACMASK       0x03f8
#define LOG_FAC(p)        (((p) & LOG_FACMASK) >> 3)
#define LOG_MASK(pri)     (1 << (pri))
#define LOG_UPTO(pri)     ((1 << ((pri) + 1)) - 1)

/* ---- Bluetooth power control stubs ---- */
#ifndef GPS_POWER_CTRL_CH
#define GPS_POWER_CTRL_CH  3
#endif
#ifndef LORA_POWER_CTRL_CH
#define LORA_POWER_CTRL_CH 2
#endif

/* ---- Default BLE PIN ---- */
#ifndef defaultBLEPin
#define defaultBLEPin 123456
#endif

#endif /* ARCH_NRF54L15 */
