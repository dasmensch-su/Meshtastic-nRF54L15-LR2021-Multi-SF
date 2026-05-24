/* ErriezCRC32.h — Lightweight CRC32 stub for nRF54L15 Zephyr port */
#pragma once
#include <stdint.h>
#include <stddef.h>

static inline uint32_t crc32Buffer(const void *data, size_t length)
{
    /* Simple CRC32 (same polynomial as zlib) */
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}
