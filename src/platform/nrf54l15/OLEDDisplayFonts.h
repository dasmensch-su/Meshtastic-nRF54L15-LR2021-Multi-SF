#pragma once
/*
 * OLEDDisplayFonts.h - ArialMT proportional font declarations
 *
 * Font data from ThingPulse esp8266-oled-ssd1306 library.
 * Three sizes: 10pt (height 13px), 16pt (height 19px), 24pt (height 28px).
 * Each covers 224 characters starting from ASCII 32 (space).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "Arduino.h"  // for PROGMEM

extern const uint8_t ArialMT_Plain_10[] PROGMEM;
extern const uint8_t ArialMT_Plain_16[] PROGMEM;
extern const uint8_t ArialMT_Plain_24[] PROGMEM;
