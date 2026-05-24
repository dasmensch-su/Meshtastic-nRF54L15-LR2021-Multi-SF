/* TinyGPS++.h — Stub for Zephyr nRF54L15 port (no GPS hardware). */
#pragma once
#include <cstdint>

class TinyGPSCustom {
public:
    TinyGPSCustom() {}
    TinyGPSCustom(class TinyGPSPlus &, const char *, int) {}
    const char *value() { return ""; }
    bool isValid() { return false; }
    bool isUpdated() { return false; }
};

class TinyGPSPlus {
public:
    struct TinyGPSLocation { double lat() { return 0; } double lng() { return 0; } bool isValid() { return false; } };
    struct TinyGPSAltitude { double meters() { return 0; } bool isValid() { return false; } };
    struct TinyGPSDate { uint32_t value() { return 0; } bool isValid() { return false; } };
    struct TinyGPSTime { uint32_t value() { return 0; } bool isValid() { return false; } };
    struct TinyGPSInteger { uint32_t value() { return 0; } bool isValid() { return false; } };
    TinyGPSLocation location;
    TinyGPSAltitude altitude;
    TinyGPSDate date;
    TinyGPSTime time;
    TinyGPSInteger satellites;
    TinyGPSInteger hdop;
    bool encode(char) { return false; }
};
