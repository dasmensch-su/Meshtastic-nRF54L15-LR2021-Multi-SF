/*
 * OLEDDisplay.h — Zephyr-native reimplementation of ThingPulse OLEDDisplay.
 *
 * Software framebuffer renderer for 128x64 monochrome OLED.
 * All drawing primitives operate on an in-memory pixel buffer.
 * Subclasses (SSD1306Wire) implement display() to push the buffer
 * to hardware via Zephyr's display_write() API.
 *
 * Font format: ThingPulse esp8266-oled-ssd1306 format.
 * Buffer format: SSD1306-native column-major pages (1 byte = 8 vertical pixels).
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* Arduino String compatibility — Meshtastic code passes both const char* and String */
#include "Arduino.h"

/* PROGMEM is a no-op on ARM */
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif

/* Font declarations — must be visible before ScreenFonts.h references them */
#include "OLEDDisplayFonts.h"

enum OLEDDISPLAY_COLOR { BLACK = 0, WHITE = 1, INVERSE = 2 };
enum OLEDDISPLAY_TEXT_ALIGNMENT { TEXT_ALIGN_LEFT = 0, TEXT_ALIGN_RIGHT = 1, TEXT_ALIGN_CENTER = 2, TEXT_ALIGN_CENTER_BOTH = 3 };
enum OLEDDISPLAY_GEOMETRY { GEOMETRY_128_64 = 0, GEOMETRY_128_32, GEOMETRY_64_48, GEOMETRY_64_32, GEOMETRY_RAWMODE, GEOMETRY_128_128 };

typedef char (*FontTableLookupFunction)(const uint8_t ch);

class OLEDDisplay {
public:
    OLEDDisplay();
    virtual ~OLEDDisplay();

    bool init();

    /* Pure virtual — subclass pushes buffer to hardware */
    virtual void display() = 0;

    /* ---- Buffer management ---- */
    void clear();
    void setGeometry(OLEDDISPLAY_GEOMETRY g, uint16_t w = 0, uint16_t h = 0);

    /* ---- Pixel operations ---- */
    void setPixel(int16_t x, int16_t y);
    void setPixelColor(int16_t x, int16_t y, OLEDDISPLAY_COLOR color);
    void clearPixel(int16_t x, int16_t y);

    /* ---- Shape drawing ---- */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
    void drawHorizontalLine(int16_t x, int16_t y, int16_t length);
    void drawVerticalLine(int16_t x, int16_t y, int16_t length);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h);
    void drawCircle(int16_t x, int16_t y, int16_t radius);
    void drawCircleQuads(int16_t x0, int16_t y0, int16_t radius, uint8_t quads);
    void fillCircle(int16_t x, int16_t y, int16_t radius);
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
    void drawProgressBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t progress);

    /* ---- Image drawing ---- */
    void drawXbm(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *xbm);
    void drawFastImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *image);
    void drawIco16x16(int16_t x, int16_t y, const uint8_t *ico, bool inverse = false);

    /* ---- Text ---- */
    uint16_t drawString(int16_t x, int16_t y, const char *text);
    uint16_t drawString(int16_t x, int16_t y, const String &text);
    void drawStringf(int16_t x, int16_t y, char *buf, const char *fmt, ...);
    uint16_t drawStringMaxWidth(int16_t x, int16_t y, uint16_t maxWidth, const char *text);
    uint16_t drawStringMaxWidth(int16_t x, int16_t y, uint16_t maxWidth, const String &text);
    uint16_t getStringWidth(const char *text, uint16_t length = 0, bool utf8 = false);
    uint16_t getStringWidth(const String &text);

    /* ---- State ---- */
    void setColor(OLEDDISPLAY_COLOR color);
    OLEDDISPLAY_COLOR getColor();
    void setFont(const uint8_t *fontData);
    void setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT align);
    void setFontTableLookupFunction(FontTableLookupFunction fn);

    /* ---- Dimensions ---- */
    uint16_t getWidth();
    uint16_t getHeight();
    uint16_t width() const;
    uint16_t height() const;

    /* ---- Display control ---- */
    virtual void setBrightness(uint8_t b);
    void flipScreenVertically();
    void mirrorScreen();
    virtual void displayOn();
    virtual void displayOff();
    void invertDisplay();
    void normalDisplay();
    void resetDisplay();
    void setContrast(uint8_t contrast, uint8_t precharge = 241, uint8_t comdetect = 64);

    /* ---- Log buffer (Print-compatible) ---- */
    bool setLogBuffer(uint16_t lines, uint16_t chars);
    void drawLogBuffer(uint16_t x, uint16_t y);
    size_t write(uint8_t c);
    size_t write(const char *s);

    /* ---- Virtual methods expected by TFTDisplay.h (not used on nRF54L15) ---- */
    virtual int getBufferOffset() { return 0; }
    virtual void sendCommand(uint8_t com) { (void)com; }
    virtual bool connect() { return true; }

    /* ---- Public buffer access (used by some TFT code, harmless to expose) ---- */
    uint8_t *buffer;

protected:
    uint16_t displayWidth;
    uint16_t displayHeight;
    OLEDDISPLAY_GEOMETRY geometry;
    OLEDDISPLAY_COLOR color;
    OLEDDISPLAY_TEXT_ALIGNMENT textAlignment;
    const uint8_t *fontData;
    FontTableLookupFunction fontTableLookupFunction;
    bool flippedVertically;
    bool mirroredHorizontally;

    /* Log buffer */
    char *logBuffer;
    uint16_t logBufferSize;
    uint16_t logBufferFilled;
    uint16_t logBufferLine;
    uint16_t logBufferMaxLines;

private:
    void drawInternal(int16_t xMove, int16_t yMove, int16_t w, int16_t h,
                      const uint8_t *data, uint16_t offset, uint16_t bytesInData);
    uint16_t drawStringInternal(int16_t xMove, int16_t yMove, const char *text, uint16_t textLength, uint16_t textWidth, bool utf8);
    uint16_t getStringWidthInternal(const char *text, uint16_t length, bool utf8);
    inline void setPixelInternal(int16_t x, int16_t y);
    inline void clearPixelInternal(int16_t x, int16_t y);
    inline void invertPixelInternal(int16_t x, int16_t y);
};
