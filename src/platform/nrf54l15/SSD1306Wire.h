/*
 * SSD1306Wire.h — Zephyr display_write() bridge for SSD1306 OLED.
 *
 * Subclass of OLEDDisplay that pushes the pixel buffer to the SSD1306
 * via Zephyr's display driver API. Constructor ignores Arduino I2C
 * parameters; the display device comes from the Zephyr device tree.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include "OLEDDisplay.h"

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

enum HW_I2C { I2C_ONE = 0, I2C_TWO = 1 };

class SSD1306Wire : public OLEDDisplay {
public:
    /**
     * Constructor signature matches ThingPulse SSD1306Wire for compatibility
     * with Screen.cpp. The sda/scl/address/bus parameters are ignored — the
     * Zephyr device tree defines the hardware configuration.
     */
    SSD1306Wire(uint8_t address, int sda = -1, int scl = -1,
                OLEDDISPLAY_GEOMETRY geo = GEOMETRY_128_64,
                HW_I2C bus = I2C_ONE, int frequency = 700000);

    bool connect();

    /** Push the framebuffer to the SSD1306 via Zephyr display_write(). */
    void display() override;

    /** Set display brightness via Zephyr display driver. */
    void setBrightness(uint8_t b) override;

    /** Turn display on/off via Zephyr blanking API. */
    void displayOn() override;
    void displayOff() override;

private:
    const struct device *dev;
    bool ready;
};
