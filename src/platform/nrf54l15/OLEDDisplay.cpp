/*
 * OLEDDisplay.cpp — Software framebuffer renderer for Meshtastic on Zephyr.
 *
 * Reimplements ThingPulse OLEDDisplay drawing primitives:
 *   - Bresenham line, midpoint circle, scanline triangle fill
 *   - XBM and FastImage bitmap rendering
 *   - ThingPulse font format text rendering
 *   - Log buffer for Print-style output
 *
 * All operations target a 1024-byte (128x64/8) pixel buffer in SSD1306
 * column-major page format.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "OLEDDisplay.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  Construction / Init                                                */
/* ------------------------------------------------------------------ */

OLEDDisplay::OLEDDisplay()
    : buffer(nullptr), displayWidth(128), displayHeight(64),
      geometry(GEOMETRY_128_64), color(WHITE),
      textAlignment(TEXT_ALIGN_LEFT), fontData(nullptr),
      fontTableLookupFunction(nullptr),
      flippedVertically(false), mirroredHorizontally(false),
      logBuffer(nullptr), logBufferSize(0), logBufferFilled(0),
      logBufferLine(0), logBufferMaxLines(0)
{
}

OLEDDisplay::~OLEDDisplay()
{
    if (buffer) { free(buffer); buffer = nullptr; }
    if (logBuffer) { free(logBuffer); logBuffer = nullptr; }
}

bool OLEDDisplay::init()
{
    setGeometry(geometry);
    if (!buffer) {
        size_t sz = (size_t)displayWidth * displayHeight / 8;
        buffer = (uint8_t *)malloc(sz);
        if (!buffer) return false;
    }
    clear();
    return true;
}

void OLEDDisplay::setGeometry(OLEDDISPLAY_GEOMETRY g, uint16_t w, uint16_t h)
{
    geometry = g;
    switch (g) {
    case GEOMETRY_128_64:  displayWidth = 128; displayHeight = 64; break;
    case GEOMETRY_128_32:  displayWidth = 128; displayHeight = 32; break;
    case GEOMETRY_64_48:   displayWidth = 64;  displayHeight = 48; break;
    case GEOMETRY_64_32:   displayWidth = 64;  displayHeight = 32; break;
    case GEOMETRY_128_128: displayWidth = 128; displayHeight = 128; break;
    case GEOMETRY_RAWMODE:
        if (w > 0) displayWidth = w;
        if (h > 0) displayHeight = h;
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Buffer management                                                  */
/* ------------------------------------------------------------------ */

void OLEDDisplay::clear()
{
    if (buffer) {
        memset(buffer, 0, (size_t)displayWidth * displayHeight / 8);
    }
}

/* ------------------------------------------------------------------ */
/*  Pixel operations                                                   */
/* ------------------------------------------------------------------ */

inline void OLEDDisplay::setPixelInternal(int16_t x, int16_t y)
{
    buffer[x + (y / 8) * displayWidth] |= (1 << (y & 7));
}

inline void OLEDDisplay::clearPixelInternal(int16_t x, int16_t y)
{
    buffer[x + (y / 8) * displayWidth] &= ~(1 << (y & 7));
}

inline void OLEDDisplay::invertPixelInternal(int16_t x, int16_t y)
{
    buffer[x + (y / 8) * displayWidth] ^= (1 << (y & 7));
}

void OLEDDisplay::setPixel(int16_t x, int16_t y)
{
    if (x < 0 || x >= displayWidth || y < 0 || y >= displayHeight) return;
    switch (color) {
    case WHITE:   setPixelInternal(x, y); break;
    case BLACK:   clearPixelInternal(x, y); break;
    case INVERSE: invertPixelInternal(x, y); break;
    }
}

void OLEDDisplay::setPixelColor(int16_t x, int16_t y, OLEDDISPLAY_COLOR c)
{
    if (x < 0 || x >= displayWidth || y < 0 || y >= displayHeight) return;
    switch (c) {
    case WHITE:   setPixelInternal(x, y); break;
    case BLACK:   clearPixelInternal(x, y); break;
    case INVERSE: invertPixelInternal(x, y); break;
    }
}

void OLEDDisplay::clearPixel(int16_t x, int16_t y)
{
    if (x < 0 || x >= displayWidth || y < 0 || y >= displayHeight) return;
    clearPixelInternal(x, y);
}

/* ------------------------------------------------------------------ */
/*  Line drawing                                                       */
/* ------------------------------------------------------------------ */

void OLEDDisplay::drawHorizontalLine(int16_t x, int16_t y, int16_t length)
{
    if (y < 0 || y >= displayHeight) return;

    int16_t x1 = x;
    int16_t x2 = x + length - 1;
    if (length < 0) { x1 = x + length + 1; x2 = x; }

    if (x1 < 0) x1 = 0;
    if (x2 >= displayWidth) x2 = displayWidth - 1;
    if (x1 > x2) return;

    for (int16_t i = x1; i <= x2; i++) {
        setPixel(i, y);
    }
}

void OLEDDisplay::drawVerticalLine(int16_t x, int16_t y, int16_t length)
{
    if (x < 0 || x >= displayWidth) return;

    int16_t y1 = y;
    int16_t y2 = y + length - 1;
    if (length < 0) { y1 = y + length + 1; y2 = y; }

    if (y1 < 0) y1 = 0;
    if (y2 >= displayHeight) y2 = displayHeight - 1;
    if (y1 > y2) return;

    for (int16_t i = y1; i <= y2; i++) {
        setPixel(x, i);
    }
}

void OLEDDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    /* Bresenham's line algorithm */
    int16_t dx = abs(x1 - x0);
    int16_t dy = -abs(y1 - y0);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;

    while (true) {
        setPixel(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ------------------------------------------------------------------ */
/*  Rectangles                                                         */
/* ------------------------------------------------------------------ */

void OLEDDisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    drawHorizontalLine(x, y, w);
    drawHorizontalLine(x, y + h - 1, w);
    drawVerticalLine(x, y, h);
    drawVerticalLine(x + w - 1, y, h);
}

void OLEDDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    for (int16_t i = 0; i < h; i++) {
        drawHorizontalLine(x, y + i, w);
    }
}

/* ------------------------------------------------------------------ */
/*  Circles                                                            */
/* ------------------------------------------------------------------ */

void OLEDDisplay::drawCircle(int16_t x0, int16_t y0, int16_t radius)
{
    /* Midpoint circle algorithm */
    int16_t x = 0, y = radius;
    int16_t dp = 1 - radius;

    while (x <= y) {
        setPixel(x0 + x, y0 + y);  /* octant 1 */
        setPixel(x0 - x, y0 + y);  /* octant 4 */
        setPixel(x0 + x, y0 - y);  /* octant 8 */
        setPixel(x0 - x, y0 - y);  /* octant 5 */
        setPixel(x0 + y, y0 + x);  /* octant 2 */
        setPixel(x0 - y, y0 + x);  /* octant 3 */
        setPixel(x0 + y, y0 - x);  /* octant 7 */
        setPixel(x0 - y, y0 - x);  /* octant 6 */
        if (dp < 0) {
            dp += 2 * x + 3;
        } else {
            dp += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

void OLEDDisplay::drawCircleQuads(int16_t x0, int16_t y0, int16_t radius, uint8_t quads)
{
    /* quads bitmask: 1=top-right, 2=top-left, 4=bottom-left, 8=bottom-right */
    int16_t x = 0, y = radius;
    int16_t dp = 1 - radius;

    while (x <= y) {
        if (quads & 0x1) { setPixel(x0 + x, y0 - y); setPixel(x0 + y, y0 - x); }
        if (quads & 0x2) { setPixel(x0 - x, y0 - y); setPixel(x0 - y, y0 - x); }
        if (quads & 0x4) { setPixel(x0 - x, y0 + y); setPixel(x0 - y, y0 + x); }
        if (quads & 0x8) { setPixel(x0 + x, y0 + y); setPixel(x0 + y, y0 + x); }
        if (dp < 0) {
            dp += 2 * x + 3;
        } else {
            dp += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

void OLEDDisplay::fillCircle(int16_t x0, int16_t y0, int16_t radius)
{
    int16_t x = 0, y = radius;
    int16_t dp = 1 - radius;

    while (x <= y) {
        drawHorizontalLine(x0 - x, y0 - y, 2 * x + 1);
        drawHorizontalLine(x0 - x, y0 + y, 2 * x + 1);
        drawHorizontalLine(x0 - y, y0 - x, 2 * y + 1);
        drawHorizontalLine(x0 - y, y0 + x, 2 * y + 1);
        if (dp < 0) {
            dp += 2 * x + 3;
        } else {
            dp += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

/* ------------------------------------------------------------------ */
/*  Triangles                                                          */
/* ------------------------------------------------------------------ */

void OLEDDisplay::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    drawLine(x0, y0, x1, y1);
    drawLine(x1, y1, x2, y2);
    drawLine(x2, y2, x0, y0);
}

void OLEDDisplay::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    /* Sort vertices by y-coordinate ascending */
    if (y0 > y1) { int16_t t; t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
    if (y1 > y2) { int16_t t; t=x1; x1=x2; x2=t; t=y1; y1=y2; y2=t; }
    if (y0 > y1) { int16_t t; t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }

    if (y0 == y2) {
        /* Degenerate case — all on one line */
        int16_t minx = x0, maxx = x0;
        if (x1 < minx) minx = x1; else if (x1 > maxx) maxx = x1;
        if (x2 < minx) minx = x2; else if (x2 > maxx) maxx = x2;
        drawHorizontalLine(minx, y0, maxx - minx + 1);
        return;
    }

    /* Scanline fill */
    int16_t dy01 = y1 - y0, dy02 = y2 - y0, dy12 = y2 - y1;
    int16_t dx01 = x1 - x0, dx02 = x2 - x0, dx12 = x2 - x1;

    int32_t sa = 0, sb = 0;

    /* Upper half */
    int16_t last = (y1 == y2) ? y1 : y1 - 1;
    for (int16_t y = y0; y <= last; y++) {
        int16_t a = x0 + sa / dy01;
        int16_t b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        if (a > b) { int16_t t = a; a = b; b = t; }
        drawHorizontalLine(a, y, b - a + 1);
    }

    /* Lower half */
    sa = (int32_t)dx12 * (y1 == y2 ? 0 : 0);
    sa = 0;
    sb = (int32_t)dx02 * (y1 - y0);
    for (int16_t y = y1; y <= y2; y++) {
        int16_t a = x1 + sa / (dy12 ? dy12 : 1);
        int16_t b = x0 + sb / (dy02 ? dy02 : 1);
        sa += dx12;
        sb += dx02;
        if (a > b) { int16_t t = a; a = b; b = t; }
        drawHorizontalLine(a, y, b - a + 1);
    }
}

void OLEDDisplay::drawProgressBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t progress)
{
    drawRect(x, y, w, h);
    uint16_t fillW = ((uint32_t)(w - 2) * progress) / 100;
    fillRect(x + 1, y + 1, fillW, h - 2);
}

/* ------------------------------------------------------------------ */
/*  Image drawing                                                      */
/* ------------------------------------------------------------------ */

void OLEDDisplay::drawXbm(int16_t xMove, int16_t yMove, int16_t w, int16_t h, const uint8_t *xbm)
{
    /* XBM format: LSB-first horizontal bits, row-major */
    int16_t widthInBytes = (w + 7) / 8;
    uint8_t data = 0;

    for (int16_t y = 0; y < h; y++) {
        for (int16_t x = 0; x < w; x++) {
            if (x & 7) {
                data >>= 1;
            } else {
                data = pgm_read_byte(xbm + (y * widthInBytes) + (x >> 3));
            }
            if (data & 0x01) {
                setPixel(xMove + x, yMove + y);
            }
        }
    }
}

void OLEDDisplay::drawFastImage(int16_t xMove, int16_t yMove, int16_t w, int16_t h, const uint8_t *image)
{
    /* FastImage format: SSD1306 vertical byte format (each byte = 8 vertical pixels) */
    drawInternal(xMove, yMove, w, h, image, 0, (uint16_t)w * ((h + 7) / 8));
}

void OLEDDisplay::drawIco16x16(int16_t x, int16_t y, const uint8_t *ico, bool inverse)
{
    /* 16x16 XBM icon */
    OLEDDISPLAY_COLOR save = color;
    if (inverse) {
        setColor(BLACK);
        fillRect(x, y, 16, 16);
        setColor(WHITE);
    }
    drawXbm(x, y, 16, 16, ico);
    setColor(save);
}

/*
 * drawInternal — blit column-major glyph/image data into the framebuffer.
 *
 * This is the core bitmap renderer shared by drawFastImage and font rendering.
 * Data format matches the SSD1306 page layout: each byte represents 8 vertical
 * pixels (bit 0 = topmost), read column-by-column.
 */
void OLEDDisplay::drawInternal(int16_t xMove, int16_t yMove, int16_t w, int16_t h,
                               const uint8_t *data, uint16_t offset, uint16_t bytesInData)
{
    if (w <= 0 || h <= 0) return;

    int16_t rasterHeight = ((h + 7) / 8) * 8; /* round up to page boundary */
    int16_t pages = rasterHeight / 8;

    for (int16_t x = 0; x < w; x++) {
        for (int16_t page = 0; page < pages; page++) {
            uint16_t idx = offset + x * pages + page;
            if (idx >= bytesInData) break;

            uint8_t byte = pgm_read_byte(data + idx);
            if (byte == 0 && color != INVERSE) continue; /* skip empty columns */

            for (int16_t bit = 0; bit < 8; bit++) {
                int16_t py = yMove + page * 8 + bit;
                int16_t px = xMove + x;
                if (py < 0 || py >= displayHeight || px < 0 || px >= displayWidth) continue;

                if (byte & (1 << bit)) {
                    switch (color) {
                    case WHITE:   setPixelInternal(px, py); break;
                    case BLACK:   clearPixelInternal(px, py); break;
                    case INVERSE: invertPixelInternal(px, py); break;
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Text rendering — ThingPulse font format                            */
/* ------------------------------------------------------------------ */

/*
 * ThingPulse font format:
 *   [0]  = character width (max)
 *   [1]  = character height
 *   [2]  = first char code
 *   [3]  = number of chars
 *   [4..] = jump table: 4 bytes per char:
 *           [0,1] = MSB,LSB offset into glyph data (from start of glyph data area)
 *           [2]   = glyph data size in bytes
 *           [3]   = glyph width in pixels
 *   After jump table: glyph data (column-major, pages of 8 vertical pixels)
 */

uint16_t OLEDDisplay::drawString(int16_t x, int16_t y, const char *text)
{
    if (!text || !fontData) return 0;

    uint16_t textLength = strlen(text);
    uint16_t textWidth = getStringWidthInternal(text, textLength, fontTableLookupFunction != nullptr);

    /* Adjust x for alignment */
    switch (textAlignment) {
    case TEXT_ALIGN_CENTER:      x -= textWidth / 2; break;
    case TEXT_ALIGN_CENTER_BOTH: x -= textWidth / 2; y -= fontData[1] / 2; break;
    case TEXT_ALIGN_RIGHT:       x -= textWidth; break;
    default: break;
    }

    return drawStringInternal(x, y, text, textLength, textWidth, fontTableLookupFunction != nullptr);
}

uint16_t OLEDDisplay::drawString(int16_t x, int16_t y, const String &text)
{
    return drawString(x, y, text.c_str());
}

void OLEDDisplay::drawStringf(int16_t x, int16_t y, char *buf, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 256, fmt, args);
    va_end(args);
    drawString(x, y, buf);
}

uint16_t OLEDDisplay::drawStringMaxWidth(int16_t x, int16_t y, uint16_t maxWidth, const char *text)
{
    if (!text || !fontData) return 0;

    uint16_t textLength = strlen(text);
    uint16_t lineHeight = pgm_read_byte(fontData + 1) + 1;
    uint16_t firstLineChars = 0;
    uint16_t lineWidth = 0;
    uint16_t linesDrawn = 0;
    uint16_t cursor = 0;

    while (cursor < textLength) {
        /* Find end of line (word-wrap at maxWidth) */
        uint16_t lineStart = cursor;
        uint16_t lastSpace = cursor;
        lineWidth = 0;

        while (cursor < textLength) {
            char c = text[cursor];
            if (fontTableLookupFunction) c = fontTableLookupFunction((uint8_t)c);

            uint8_t firstChar = pgm_read_byte(fontData + 2);
            uint8_t numChars = pgm_read_byte(fontData + 3);
            uint8_t ci = (uint8_t)c - firstChar;

            uint8_t charWidth = 0;
            if (ci < numChars) {
                charWidth = pgm_read_byte(fontData + 4 + ci * 4 + 3);
            }

            if (lineWidth + charWidth > maxWidth && cursor > lineStart) {
                if (lastSpace > lineStart) cursor = lastSpace + 1;
                break;
            }
            lineWidth += charWidth;
            if (c == ' ') lastSpace = cursor;
            cursor++;
        }

        /* Draw this line */
        uint16_t len = cursor - lineStart;
        char lineBuf[128];
        if (len > sizeof(lineBuf) - 1) len = sizeof(lineBuf) - 1;
        memcpy(lineBuf, text + lineStart, len);
        lineBuf[len] = '\0';

        uint16_t w = getStringWidthInternal(lineBuf, len, fontTableLookupFunction != nullptr);

        int16_t lineX = x;
        switch (textAlignment) {
        case TEXT_ALIGN_CENTER:      lineX = x + (maxWidth - w) / 2; break;
        case TEXT_ALIGN_CENTER_BOTH: lineX = x + (maxWidth - w) / 2; break;
        case TEXT_ALIGN_RIGHT:       lineX = x + maxWidth - w; break;
        default: break;
        }

        drawStringInternal(lineX, y + linesDrawn * lineHeight, lineBuf, len, w,
                           fontTableLookupFunction != nullptr);

        if (linesDrawn == 0) firstLineChars = len;
        linesDrawn++;
    }

    return firstLineChars;
}

uint16_t OLEDDisplay::drawStringMaxWidth(int16_t x, int16_t y, uint16_t maxWidth, const String &text)
{
    return drawStringMaxWidth(x, y, maxWidth, text.c_str());
}

uint16_t OLEDDisplay::drawStringInternal(int16_t xMove, int16_t yMove, const char *text,
                                         uint16_t textLength, uint16_t textWidth, bool utf8)
{
    if (!fontData || !text) return 0;

    uint8_t charHeight = pgm_read_byte(fontData + 1);
    uint8_t firstChar = pgm_read_byte(fontData + 2);
    uint8_t numChars = pgm_read_byte(fontData + 3);
    uint16_t jumpTableStart = 4;
    uint16_t glyphDataStart = jumpTableStart + (uint16_t)numChars * 4;

    uint16_t cursorX = 0;

    for (uint16_t i = 0; i < textLength; i++) {
        char c = text[i];
        if (utf8 && fontTableLookupFunction) c = fontTableLookupFunction((uint8_t)c);

        uint8_t ci = (uint8_t)c - firstChar;
        if (ci >= numChars) continue;

        /* Read jump table entry */
        uint16_t jtEntry = jumpTableStart + (uint16_t)ci * 4;
        uint16_t glyphOffset = ((uint16_t)pgm_read_byte(fontData + jtEntry) << 8) |
                               pgm_read_byte(fontData + jtEntry + 1);
        uint8_t glyphSize = pgm_read_byte(fontData + jtEntry + 2);
        uint8_t charWidth = pgm_read_byte(fontData + jtEntry + 3);

        if (glyphSize > 0) {
            drawInternal(xMove + cursorX, yMove, charWidth, charHeight,
                         fontData, glyphDataStart + glyphOffset, glyphDataStart + glyphOffset + glyphSize);
        }

        cursorX += charWidth;
    }

    return cursorX;
}

uint16_t OLEDDisplay::getStringWidth(const char *text, uint16_t length, bool utf8)
{
    if (!text || !fontData) return 0;
    if (length == 0) length = strlen(text);
    return getStringWidthInternal(text, length, utf8 || fontTableLookupFunction != nullptr);
}

uint16_t OLEDDisplay::getStringWidth(const String &text)
{
    return getStringWidth(text.c_str());
}

uint16_t OLEDDisplay::getStringWidthInternal(const char *text, uint16_t length, bool utf8)
{
    if (!fontData || !text) return 0;

    uint8_t firstChar = pgm_read_byte(fontData + 2);
    uint8_t numChars = pgm_read_byte(fontData + 3);

    uint16_t width = 0;
    for (uint16_t i = 0; i < length; i++) {
        char c = text[i];
        if (utf8 && fontTableLookupFunction) c = fontTableLookupFunction((uint8_t)c);

        uint8_t ci = (uint8_t)c - firstChar;
        if (ci < numChars) {
            width += pgm_read_byte(fontData + 4 + (uint16_t)ci * 4 + 3);
        }
    }
    return width;
}

/* ------------------------------------------------------------------ */
/*  State management                                                   */
/* ------------------------------------------------------------------ */

void OLEDDisplay::setColor(OLEDDISPLAY_COLOR c) { color = c; }
OLEDDISPLAY_COLOR OLEDDisplay::getColor() { return color; }
void OLEDDisplay::setFont(const uint8_t *fd) { fontData = fd; }
void OLEDDisplay::setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT a) { textAlignment = a; }
void OLEDDisplay::setFontTableLookupFunction(FontTableLookupFunction fn) { fontTableLookupFunction = fn; }

/* ------------------------------------------------------------------ */
/*  Dimensions                                                         */
/* ------------------------------------------------------------------ */

uint16_t OLEDDisplay::getWidth() { return displayWidth; }
uint16_t OLEDDisplay::getHeight() { return displayHeight; }
uint16_t OLEDDisplay::width() const { return displayWidth; }
uint16_t OLEDDisplay::height() const { return displayHeight; }

/* ------------------------------------------------------------------ */
/*  Display control (no-ops for software renderer, subclass overrides) */
/* ------------------------------------------------------------------ */

void OLEDDisplay::setBrightness(uint8_t b) { (void)b; }
void OLEDDisplay::flipScreenVertically() { flippedVertically = true; }
void OLEDDisplay::mirrorScreen() { mirroredHorizontally = true; }
void OLEDDisplay::displayOn() {}
void OLEDDisplay::displayOff() {}
void OLEDDisplay::invertDisplay() {}
void OLEDDisplay::normalDisplay() {}
void OLEDDisplay::resetDisplay() {}
void OLEDDisplay::setContrast(uint8_t contrast, uint8_t precharge, uint8_t comdetect)
{
    (void)contrast; (void)precharge; (void)comdetect;
}

/* ------------------------------------------------------------------ */
/*  Log buffer                                                         */
/* ------------------------------------------------------------------ */

bool OLEDDisplay::setLogBuffer(uint16_t lines, uint16_t chars)
{
    if (logBuffer) { free(logBuffer); logBuffer = nullptr; }

    logBufferMaxLines = lines;
    logBufferSize = lines * chars;
    logBufferFilled = 0;
    logBufferLine = 0;

    logBuffer = (char *)malloc(logBufferSize);
    if (!logBuffer) {
        logBufferSize = 0;
        return false;
    }
    memset(logBuffer, 0, logBufferSize);
    return true;
}

void OLEDDisplay::drawLogBuffer(uint16_t x, uint16_t y)
{
    if (!logBuffer || !fontData) return;

    uint8_t lineHeight = pgm_read_byte(fontData + 1) + 1;
    uint16_t charsPerLine = logBufferSize / logBufferMaxLines;

    for (uint16_t i = 0; i < logBufferMaxLines; i++) {
        uint16_t lineIdx = (logBufferLine + i) % logBufferMaxLines;
        char *lineStart = logBuffer + lineIdx * charsPerLine;

        /* Null-terminate for safety */
        char lineBuf[64];
        uint16_t len = charsPerLine < sizeof(lineBuf) ? charsPerLine : sizeof(lineBuf) - 1;
        memcpy(lineBuf, lineStart, len);
        lineBuf[len] = '\0';

        /* Trim trailing nulls/spaces */
        while (len > 0 && (lineBuf[len - 1] == '\0' || lineBuf[len - 1] == ' ')) len--;
        lineBuf[len] = '\0';

        if (len > 0) {
            drawString(x, y + i * lineHeight, lineBuf);
        }
    }
}

size_t OLEDDisplay::write(uint8_t c)
{
    if (!logBuffer) return 1;

    uint16_t charsPerLine = logBufferSize / logBufferMaxLines;

    if (c == '\n' || logBufferFilled >= charsPerLine) {
        /* Advance to next line */
        logBufferLine = (logBufferLine + 1) % logBufferMaxLines;
        memset(logBuffer + logBufferLine * charsPerLine, 0, charsPerLine);
        logBufferFilled = 0;
        if (c == '\n') return 1;
    }

    logBuffer[logBufferLine * charsPerLine + logBufferFilled] = (char)c;
    logBufferFilled++;
    return 1;
}

size_t OLEDDisplay::write(const char *s)
{
    if (!s) return 0;
    size_t n = 0;
    while (*s) { write((uint8_t)*s++); n++; }
    return n;
}
