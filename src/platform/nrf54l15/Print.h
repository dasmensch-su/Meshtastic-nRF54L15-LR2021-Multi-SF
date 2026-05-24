#pragma once
/*
 * Minimal Arduino-compat Print.h for the nRF54L15 Meshtastic port.
 *
 * SerialConsole / RedirectablePrint / StreamAPI all reach into Arduinos
 * Print + Stream class hierarchy. We dont have the real Arduino core,
 * so this header (and its sibling Stream.h) provides just enough surface
 * for those classes to compile and behave correctly:
 *
 *   - Pure-virtual write(uint8_t) lets subclasses (RedirectablePrint,
 *     _ZephyrSerial) decide how to deliver bytes (printk vs ring-buffer
 *     vs USB-CDC, etc).
 *   - write(const uint8_t*, size_t) defaults to a byte loop calling the
 *     virtual write(uint8_t). Override in subclasses for bulk efficiency.
 *   - Just enough print/println overloads for SerialConsole and the
 *     RedirectablePrint::log path.
 *
 * No floating-point print helpers (we never print doubles in the device
 * console path), no number-base helpers beyond DEC/HEX, no String class
 * support  the upstream Meshtastic logging never relies on those.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <string>

#define DEC 10
#define HEX 16

class Print
{
  public:
    virtual ~Print() = default;

    /* Subclasses MUST implement this. All other methods funnel here. */
    virtual size_t write(uint8_t c) = 0;

    /* Loop the byte writer for buffered writes. Subclasses can override. */
    virtual size_t write(const uint8_t *buf, size_t len)
    {
        size_t n = 0;
        for (size_t i = 0; i < len; i++) {
            if (write(buf[i]) != 1) break;
            n++;
        }
        return n;
    }

    /* C-string convenience (RedirectablePrint::log uses these). */
    size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
    size_t write(const char *buf, size_t len) { return write((const uint8_t *)buf, len); }

    /* RedirectablePrint::log_to_serial / vprintf call printf. */
    size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

    /* Minimal print/println surface. We only need the variants the
     * upstream logging path actually invokes; expand if a future module
     * expects more. */
    size_t print(const char *s)         { return write(s); }
    size_t print(const std::string &s)  { return write(s.c_str(), s.size()); }
    size_t println(const char *s)       { size_t n = write(s); n += write("\r\n", 2); return n; }
    size_t println(const std::string &s){ size_t n = write(s.c_str(), s.size()); n += write("\r\n", 2); return n; }
    size_t println(void)                { return write("\r\n", 2); }

    /* Default flush is a no-op so Stream subclasses can override. */
    virtual void flush() {}
};
