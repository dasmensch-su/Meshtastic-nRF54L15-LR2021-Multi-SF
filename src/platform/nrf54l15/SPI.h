/*
 * SPI.h — stub for Zephyr nRF54L15 port.
 *
 * Meshtastic's main.h (and a few other headers) include <SPI.h>.  On
 * Zephyr we use the native SPI driver via DTS; this header exists only
 * to satisfy #include directives so the build does not fail.
 */
#pragma once

#ifdef ARCH_NRF54L15

#include <stdint.h>

/* SPISettings — no-op wrapper used by some RadioLib / module headers */
struct SPISettings {
    SPISettings() {}
    SPISettings(uint32_t /*clock*/, uint8_t /*bit_order*/, uint8_t /*data_mode*/) {}
};

/* SPIClass stub — not used for the LR2021 (Zephyr spi_dt_spec used instead) */
class SPIClass {
  public:
    void begin() {}
    void end()   {}
    void beginTransaction(SPISettings) {}
    void endTransaction()              {}
    uint8_t transfer(uint8_t data)     { (void)data; return 0; }
    void    transfer(void *, size_t)   {}
};

extern SPIClass SPI;

#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define MSBFIRST  1
#define LSBFIRST  0

#endif /* ARCH_NRF54L15 */
