/*
 * SSD1306Wire.cpp — Zephyr display bridge for SSD1306 OLED.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "SSD1306Wire.h"
#include <string.h>

SSD1306Wire::SSD1306Wire(uint8_t address, int sda, int scl,
                         OLEDDISPLAY_GEOMETRY geo, HW_I2C bus, int frequency)
    : dev(nullptr), ready(false)
{
    (void)address; (void)sda; (void)scl; (void)bus; (void)frequency;

    setGeometry(geo);

    /* Get display device from Zephyr device tree */
#if DT_HAS_CHOSEN(zephyr_display)
    dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#endif

    /* Eagerly connect — OLEDDisplayUi::init() calls display->init()
     * but that only allocates the buffer. We need to configure the
     * Zephyr display device (pixel format, blanking) now so that
     * display() actually pushes pixels. */
    connect();
}

bool SSD1306Wire::connect()
{
    if (!dev || !device_is_ready(dev)) {
        ready = false;
        return false;
    }

    /* Initialize the framebuffer (allocates buffer, clears it) */
    if (!buffer && !init()) {
        ready = false;
        return false;
    }

    /* Configure pixel format for SSD1306 monochrome */
    display_set_pixel_format(dev, PIXEL_FORMAT_MONO10);
    display_blanking_off(dev);

    ready = true;
    return true;
}

void SSD1306Wire::display()
{
    if (!ready || !buffer || !dev) return;

    struct display_buffer_descriptor desc;
    desc.buf_size = (uint32_t)displayWidth * displayHeight / 8;
    desc.width = displayWidth;
    desc.height = displayHeight;
    desc.pitch = displayWidth;

    display_write(dev, 0, 0, &desc, buffer);
}

void SSD1306Wire::setBrightness(uint8_t b)
{
    if (!ready || !dev) return;
    display_set_contrast(dev, b);
}

void SSD1306Wire::displayOn()
{
    if (!dev) return;
    display_blanking_off(dev); /* blanking off = display shows content */
    ready = true;
}

void SSD1306Wire::displayOff()
{
    if (!dev) return;
    display_blanking_on(dev); /* blanking on = display goes dark */
}
