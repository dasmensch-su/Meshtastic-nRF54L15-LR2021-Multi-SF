/*
 * Arduino.h — Zephyr shim for the nRF54L15 port.
 *
 * Provides the Arduino API surface that Meshtastic's core files expect,
 * without requiring the Arduino toolchain.  Only the subset actually used
 * by the files we compile is implemented; everything else is a no-op stub.
 */
#pragma once

#ifdef ARCH_NRF54L15

#include "arduino_compat.h" /* millis(), delay(), yield(), IRAM_ATTR */
#include <zephyr/sys/printk.h>

#include <stdint.h>
#include <strings.h>  /* strcasecmp — used by Router.cpp PKI path */

typedef unsigned long ulong;
#include <stddef.h>
#include <stdlib.h>

/* setenv — POSIX function not in picolibc, used by MenuHandler for timezone */
#ifdef __cplusplus
extern "C"
#endif
int setenv(const char *, const char *, int);
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <string>    /* our minimal std::string stub (found via -I path) */
#include <algorithm> /* our minimal std::algorithm stub */
#include <utility>   /* std::move, std::forward */

/* ------------------------------------------------------------------ */
/* Basic types                                                          */
/* ------------------------------------------------------------------ */
typedef uint8_t  byte;
typedef uint16_t word;
typedef bool     boolean;

/* ------------------------------------------------------------------ */
/* Pin constants (no GPIO on this build, but headers reference them)  */
/* ------------------------------------------------------------------ */
#define INPUT          0
#define OUTPUT         1
#define INPUT_PULLUP   2
#define INPUT_PULLDOWN 3
#define HIGH           1
#define LOW            0

/* ------------------------------------------------------------------ */
/* Math                                                                 */
/* ------------------------------------------------------------------ */
#ifndef PI
#define PI         3.14159265358979323846
#endif
#define TWO_PI     (2.0 * PI)
#define HALF_PI    (PI / 2.0)
#define DEG_TO_RAD (PI / 180.0)
#define RAD_TO_DEG (180.0 / PI)
#define EULER      2.718281828459045235360

#define sq(x)        ((x) * (x))
#define radians(d)   ((d) * DEG_TO_RAD)
#define degrees(r)   ((r) * RAD_TO_DEG)

#ifndef abs
#define abs(x) ((x) >= 0 ? (x) : -(x))
#endif
/* Arduino min/max — NOT macros to avoid collision with std::min/std::max.
 * Include <algorithm> which provides std::min/std::max and exposes them
 * at global scope via using-declarations. */
#undef min
#undef max
#include <algorithm>

template <typename T>
static inline T constrain(T val, T lo, T hi)
{
    return val < lo ? lo : (val > hi ? hi : val);
}

/* ------------------------------------------------------------------ */
/* PROGMEM / pgmspace — no-ops on nRF54L15 (all flash is executable)  */
/* ------------------------------------------------------------------ */
#define PROGMEM
#define PSTR(s)          (s)
#define F(s)             (s)
#define PGM_P            const char *
#define FPSTR(s)         (s)
#define pgm_read_byte(p)  (*(const uint8_t *)(p))
#define pgm_read_word(p)  (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#define pgm_read_float(p) (*(const float *)(p))
#define pgm_read_ptr(p)   (*(const void **)(p))
#define strlen_P          strlen
#define strcpy_P          strcpy
#define strcmp_P          strcmp
#define strncpy_P         strncpy
#define sprintf_P         sprintf

/* ------------------------------------------------------------------ */
/* String class — Arduino-compatible wrapper around std::string        */
/* ------------------------------------------------------------------ */

#define HEX 16
#define DEC 10
#define OCT  8
#define BIN  2

class String : public std::string
{
  public:
    /* Constructors */
    String() {}
    String(const char *s) : std::string(s ? s : "") {}
    String(const std::string &s) : std::string(s) {}
    String(std::string &&s) : std::string(std::move(s)) {}
    explicit String(char c) : std::string(1, c) {}

    explicit String(unsigned char n, int base = DEC)   { _fromInt((unsigned long)n, base); }
    explicit String(int n,           int base = DEC)   { _fromInt((long)n, base); }
    explicit String(unsigned int n,  int base = DEC)   { _fromInt((unsigned long)n, base); }
    explicit String(long n,          int base = DEC)   { _fromInt(n, base); }
    explicit String(unsigned long n, int base = DEC)   { _fromInt(n, base); }
    explicit String(float f,  int decimals = 2)        { _fromFloat((double)f, decimals); }
    explicit String(double d, int decimals = 2)        { _fromFloat(d, decimals); }

    /* Capacity */
    unsigned int length()   const { return (unsigned int)size(); }
    bool         isEmpty()  const { return empty(); }
    void         reserve(unsigned int n) { std::string::reserve(n); }

    /* Character access */
    char  charAt(unsigned int i)              const { return at(i); }
    void  setCharAt(unsigned int i, char c)         { (*this)[i] = c; }
    char  operator[](unsigned int i)          const { return std::string::operator[](i); }
    char &operator[](unsigned int i)                { return std::string::operator[](i); }

    /* Concatenation */
    String &operator+=(const String &rhs)  { append(rhs);            return *this; }
    String &operator+=(const char  *rhs)   { if (rhs) append(rhs);   return *this; }
    String &operator+=(char c)             { push_back(c);            return *this; }
    String &operator+=(int n)              { *this += String(n);      return *this; }
    String &operator+=(unsigned int n)     { *this += String(n);      return *this; }
    String &operator+=(long n)             { *this += String(n);      return *this; }
    String &operator+=(unsigned long n)    { *this += String(n);      return *this; }

    String operator+(const String &rhs)    const { return String(std::string(*this) + std::string(rhs)); }
    String operator+(const char   *rhs)    const { return String(std::string(*this) + (rhs ? rhs : "")); }
    friend String operator+(const char *lhs, const String &rhs) { return String(std::string(lhs ? lhs : "") + std::string(rhs)); }

    /* Comparison */
    bool equals(const String &s)           const { return *this == s; }
    bool equals(const char *s)             const { return compare(s) == 0; }
    bool equalsIgnoreCase(const String &s) const {
        if (size() != s.size()) return false;
        for (size_t i = 0; i < size(); ++i)
            if (tolower((*this)[i]) != tolower(s[i])) return false;
        return true;
    }
    int compareTo(const String &s)         const { return compare(s); }

    /* Search */
    int indexOf(char c, unsigned int from = 0) const {
        size_t p = find(c, from);
        return (p == npos) ? -1 : (int)p;
    }
    int indexOf(const String &s, unsigned int from = 0) const {
        size_t p = find(std::string(s), from);
        return (p == npos) ? -1 : (int)p;
    }
    int indexOf(const char *s, unsigned int from = 0) const {
        size_t p = find(s, from);
        return (p == npos) ? -1 : (int)p;
    }
    int lastIndexOf(char c) const {
        size_t p = rfind(c);
        return (p == npos) ? -1 : (int)p;
    }
    int lastIndexOf(const String &s) const {
        size_t p = rfind(std::string(s));
        return (p == npos) ? -1 : (int)p;
    }

    bool startsWith(const String &s) const {
        if (s.size() > size()) return false;
        return substr(0, s.size()) == std::string(s);
    }
    bool startsWith(const char *s) const {
        if (!s) return false;
        return substr(0, strlen(s)) == s;
    }
    bool endsWith(const String &s) const {
        if (s.size() > size()) return false;
        return substr(size() - s.size()) == std::string(s);
    }

    /* Substring */
    String substring(unsigned int begin) const {
        return String(substr(begin));
    }
    String substring(unsigned int begin, unsigned int end) const {
        if (end <= begin) return String();
        return String(substr(begin, end - begin));
    }

    /* Conversion */
    long          toInt()    const { return strtol(c_str(), nullptr, 10); }
    float         toFloat()  const { return strtof(c_str(), nullptr); }
    double        toDouble() const { return strtod(c_str(), nullptr); }

    /* Modification */
    void replace(char from, char to) {
        for (auto &ch : *this) if (ch == from) ch = to;
    }
    void replace(const String &from, const String &to) {
        size_t pos = 0;
        while ((pos = find(std::string(from), pos)) != npos) {
            std::string::replace(pos, from.size(), std::string(to));
            pos += to.size();
        }
    }
    void toLowerCase() { for (auto &ch : *this) ch = (char)tolower(ch); }
    void toUpperCase() { for (auto &ch : *this) ch = (char)toupper(ch); }
    void trim() {
        size_t s = find_first_not_of(" \t\r\n");
        if (s == npos) { clear(); return; }
        size_t e = find_last_not_of(" \t\r\n");
        *this = substr(s, e - s + 1);
    }
    void remove(unsigned int idx, unsigned int cnt = 1) { erase(idx, cnt); }

    /* concat family */
    bool concat(const String &s)  { append(s);          return true; }
    bool concat(const char  *s)   { if (s) append(s);   return true; }
    bool concat(char c)           { push_back(c);        return true; }
    bool concat(int n)            { *this += String(n);  return true; }

    /* toCharArray */
    void toCharArray(char *buf, unsigned int bufsize, unsigned int idx = 0) const {
        strncpy(buf, c_str() + idx, bufsize - 1);
        buf[bufsize - 1] = '\0';
    }

    /* Explicit bool conversion — empty == false, non-empty == true */
    explicit operator bool() const { return !empty(); }

  private:
    void _fromInt(long n, int base) {
        char buf[32];
        if (base == HEX)      snprintf(buf, sizeof(buf), "%lx", n);
        else if (base == OCT) snprintf(buf, sizeof(buf), "%lo", n);
        else                  snprintf(buf, sizeof(buf), "%ld", n);
        assign(buf);
    }
    void _fromInt(unsigned long n, int base) {
        char buf[32];
        if (base == HEX)      snprintf(buf, sizeof(buf), "%lx", n);
        else if (base == OCT) snprintf(buf, sizeof(buf), "%lo", n);
        else                  snprintf(buf, sizeof(buf), "%lu", n);
        assign(buf);
    }
    void _fromFloat(double d, int decimals) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, d);
        assign(buf);
    }
};

/* Helper to concatenate arbitrary types with + */
inline String operator+(const String &lhs, int rhs)          { return lhs + String(rhs); }
inline String operator+(const String &lhs, unsigned int rhs) { return lhs + String(rhs); }
inline String operator+(const String &lhs, long rhs)         { return lhs + String(rhs); }
inline String operator+(const String &lhs, unsigned long rhs){ return lhs + String(rhs); }
inline String operator+(const String &lhs, float rhs)        { return lhs + String(rhs); }
inline String operator+(const String &lhs, double rhs)       { return lhs + String(rhs); }
inline String operator+(const String &lhs, char rhs)         { return lhs + String(rhs); }

/* ------------------------------------------------------------------ */
/* Serial — printk-backed stream                                        */
/* ------------------------------------------------------------------ */

/* Out-of-class hooks implemented in mesh_zephyr.cpp. They drive the
 * Zephyr console-UART RX-IRQ ring buffer that backs Serial.available()
 * and Serial.read(). Without these the Meshtastic SerialConsole
 * (StreamAPI) never sees the wake byte from `meshtastic --port`, and
 * every CLI command times out with "Timed out waiting for connection
 * completion." See SerialConsole.cpp + StreamAPI for the consumer side. */
extern "C" int  _zephyr_serial_available(void);
extern "C" int  _zephyr_serial_read(void);
extern "C" int  _zephyr_serial_peek(void);

#include "Stream.h"

class _ZephyrSerial : public Stream
{
  public:
    void begin(unsigned long baud) { (void)baud; }
    void end()                     {}
    void flush() override          {}
    int  available() override      { return _zephyr_serial_available(); }
    int  read() override           { return _zephyr_serial_read(); }
    int  peek() override           { return _zephyr_serial_peek(); }
    explicit operator bool()       { return true; }

    /* Wire-protocol writes bypass the deferred-log ring buffer by
     * going straight to the console UART via uart_poll_out (defined
     * in mesh_zephyr.cpp alongside the RX backend). The log-overflow
     * drop path was fragmenting 0x94c3-framed protobufs when log
     * traffic competed for the ring; the silent-mode gate added in
     * c2b02a523 already muted our app-side logs once canWrite=true,
     * so this direct-write path only contends with brief TX-FIFO
     * back-pressure from the host, which drains quickly as the CLI
     * starts reading. */
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buf, size_t len) override;
};

extern _ZephyrSerial Serial;
extern _ZephyrSerial Serial1;

/* Arduino compatibility typedef — GPS.h uses HardwareSerial */
typedef _ZephyrSerial HardwareSerial;

/* Non-standard 'uint' used in AdminModule.h */
typedef unsigned int uint;

/* ------------------------------------------------------------------ */
/* Misc Arduino-isms used in various Meshtastic headers                */
/* ------------------------------------------------------------------ */

/* randomSeed / random */
static inline void randomSeed(unsigned long seed) { srand((unsigned int)seed); }
static inline long random()                        { return (long)rand(); }
static inline long random(long howbig)             { return (long)(rand() % howbig); }
static inline long random(long howsmall, long howbig) {
    if (howsmall >= howbig) return howsmall;
    return howsmall + (long)(rand() % (howbig - howsmall));
}

/* bit manipulation */
#define bitRead(val, n)        (((val) >> (n)) & 0x01)
#define bitSet(val, n)         ((val) |= (1UL << (n)))
#define bitClear(val, n)       ((val) &= ~(1UL << (n)))
#define bitWrite(val, n, b)    (b ? bitSet(val, n) : bitClear(val, n))
#define bit(n)                 (1UL << (n))
#define lowByte(w)             ((uint8_t)((w) & 0xff))
#define highByte(w)            ((uint8_t)(((w) >> 8) & 0xff))

/* map() */
static inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/* interrupts stub — used in some headers but not called on Zephyr */
static inline void interrupts()   {}
static inline void noInterrupts() {}

/* GPIO stubs — not used on Zephyr (DTS/GPIO driver used instead)    */
static inline void pinMode(uint8_t /*pin*/, uint8_t /*mode*/) {}
static inline void digitalWrite(uint8_t /*pin*/, uint8_t /*val*/) {}
static inline int  digitalRead(uint8_t /*pin*/) { return LOW; }
static inline int  analogRead(uint8_t /*pin*/)  { return 0; }
static inline void analogWrite(uint8_t /*pin*/, int /*val*/) {}

/* Interrupt stubs — OneButton handles GPIO reads via Zephyr driver    */
#define CHANGE 1
#define FALLING 2
#define RISING 3
typedef void (*voidFuncPtr)(void);
static inline void attachInterrupt(uint8_t, voidFuncPtr, int) {}
static inline void detachInterrupt(uint8_t) {}

/* tone / noTone stubs */
static inline void tone(uint8_t /*pin*/, unsigned int /*freq*/, unsigned long /*dur*/ = 0) {}
static inline void noTone(uint8_t /*pin*/) {}

/* shiftIn / shiftOut stubs */
#define MSBFIRST 1
#define LSBFIRST 0
static inline uint8_t shiftIn(uint8_t /*data*/, uint8_t /*clk*/, uint8_t /*bit_order*/) { return 0; }
static inline void shiftOut(uint8_t /*data*/, uint8_t /*clk*/, uint8_t /*bit_order*/, uint8_t /*val*/) {}

/* pulse stubs */
static inline unsigned long pulseIn(uint8_t /*pin*/, uint8_t /*state*/, unsigned long /*timeout*/ = 1000000UL) { return 0; }

/* A0-A7 analog pin aliases */
#define A0 0
#define A1 1
#define A2 2
#define A3 3
#define A4 4
#define A5 5
#define A6 6
#define A7 7

#endif /* ARCH_NRF54L15 */
