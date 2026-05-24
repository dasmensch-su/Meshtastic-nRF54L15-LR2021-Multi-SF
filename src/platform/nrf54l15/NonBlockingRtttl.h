/* NonBlockingRtttl.h — Stub for Zephyr nRF54L15 (no buzzer/PWM) */
#pragma once
namespace rtttl {
    inline void begin(int, const char *) {}
    inline void play() {}
    inline void stop() {}
    inline bool isPlaying() { return false; }
    inline bool done() { return true; }
}
