/*
 * Out-of-line bits of the minimal Arduino-compat Print class. Just
 * Print::printf (vsnprintf into a stack buffer, then write the result).
 * Everything else inlines from Print.h.
 */

#include "Print.h"

#include <stdio.h>

size_t Print::printf(const char *fmt, ...)
{
    /* RedirectablePrint::vprintf already uses a 320-byte buffer for log
     * lines; matching that here keeps any single printf call within the
     * same envelope without extra fragmentation. */
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return 0;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    return write((const uint8_t *)buf, (size_t)n);
}
