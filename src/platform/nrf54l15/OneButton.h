/*
 * OneButton.h — Zephyr GPIO-backed button handler for nRF54L15.
 *
 * Reimplements the OneButton library's key API surface using Zephyr
 * GPIO driver for pin reads. Supports single click, long press,
 * double click, and multi-click detection via polling in tick().
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <stdint.h>
#include "arduino_compat.h" /* millis() */

/* Zephyr GPIO for actual pin reading */
#ifdef ARCH_NRF54L15
#include <zephyr/drivers/gpio.h>
#endif

class OneButton {
public:
    typedef void (*CallbackFunction)(void *);

    OneButton() : _pin(-1), _activeLow(true), _state(0), _idle(true) {}

    OneButton(int pin, bool activeLow = true, bool pullup = true)
        : _pin(pin), _activeLow(activeLow), _state(0), _idle(true),
          _clickMs(400), _pressMs(800), _debounceMs(50),
          _clickFunc(nullptr), _longPressStartFunc(nullptr),
          _longPressStopFunc(nullptr), _doubleClickFunc(nullptr),
          _multiClickFunc(nullptr),
          _clickParam(nullptr), _longPressStartParam(nullptr),
          _longPressStopParam(nullptr), _doubleClickParam(nullptr),
          _multiClickParam(nullptr),
          _lastDebounce(0), _pressStart(0), _clickCount(0),
          _lastClick(0), _longPressed(false), _waitingRelease(false)
    {
        (void)pullup;
#ifdef ARCH_NRF54L15
        /* Configure GPIO pin for input with pull-up */
        _gpioReady = false;
        const struct device *port = nullptr;
        /* Map pin number to Zephyr GPIO port/pin */
        if (pin < 32) {
            port = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpio0));
            _gpioPin = pin;
        } else if (pin < 64) {
            port = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpio1));
            _gpioPin = pin - 32;
        } else {
            port = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpio2));
            _gpioPin = pin - 64;
        }
        if (port && device_is_ready(port)) {
            _gpioPort = port;
            gpio_pin_configure(port, _gpioPin, GPIO_INPUT | (pullup ? GPIO_PULL_UP : 0));
            _gpioReady = true;
        }
#endif
    }

    /* ---- Callback registration (with void* parameter) ---- */

    void attachClick(CallbackFunction f, void *param = nullptr) {
        _clickFunc = f; _clickParam = param;
    }
    void attachDoubleClick(CallbackFunction f, void *param = nullptr) {
        _doubleClickFunc = f; _doubleClickParam = param;
    }
    void attachMultiClick(CallbackFunction f, void *param = nullptr) {
        _multiClickFunc = f; _multiClickParam = param;
    }
    void attachLongPressStart(CallbackFunction f, void *param = nullptr) {
        _longPressStartFunc = f; _longPressStartParam = param;
    }
    void attachLongPressStop(CallbackFunction f, void *param = nullptr) {
        _longPressStopFunc = f; _longPressStopParam = param;
    }

    /* ---- Legacy callback registration (no parameter) ---- */
    void attachClick(void (*f)()) {
        _clickFunc = (CallbackFunction)f; _clickParam = nullptr;
    }
    void attachLongPressStart(void (*f)()) {
        _longPressStartFunc = (CallbackFunction)f; _longPressStartParam = nullptr;
    }
    void attachLongPressStop(void (*f)()) {
        _longPressStopFunc = (CallbackFunction)f; _longPressStopParam = nullptr;
    }

    /* ---- Configuration ---- */

    void setDebounceMs(uint16_t ms) { _debounceMs = ms; }
    void setClickMs(uint16_t ms) { _clickMs = ms; }
    void setPressMs(uint16_t ms) { _pressMs = ms; }

    /* ---- State queries ---- */

    bool isIdle() const { return _idle; }
    bool isLongPressed() const { return _longPressed; }
    int getNumberClicks() const { return _clickCount; }

    /* ---- Main polling function — call from ISR or thread ---- */

    void tick()
    {
        bool rawPressed = readPin();
        unsigned long now = millis();

        /* Debounce */
        if (rawPressed != _lastRaw) {
            _lastDebounce = now;
            _lastRaw = rawPressed;
        }
        if (now - _lastDebounce < _debounceMs) return;

        bool pressed = rawPressed;
        _idle = !pressed && !_waitingRelease && (now - _lastClick > _clickMs);

        switch (_state) {
        case 0: /* IDLE — waiting for press */
            if (pressed) {
                _pressStart = now;
                _state = 1;
                _longPressed = false;
                _idle = false;
            }
            break;

        case 1: /* PRESSED — waiting for release or long press */
            if (!pressed) {
                /* Released — register a click */
                _clickCount++;
                _lastClick = now;
                _state = 2; /* wait for potential double-click */
                _waitingRelease = false;
            } else if (now - _pressStart >= _pressMs && !_longPressed) {
                /* Long press detected */
                _longPressed = true;
                if (_longPressStartFunc)
                    _longPressStartFunc(_longPressStartParam);
                _state = 3; /* in long press */
            }
            break;

        case 2: /* RELEASED — waiting for double-click or timeout */
            if (pressed) {
                /* Another press — potential multi-click */
                _pressStart = now;
                _state = 1;
            } else if (now - _lastClick >= _clickMs) {
                /* Click timeout — dispatch accumulated clicks */
                if (_clickCount == 1 && _clickFunc) {
                    _clickFunc(_clickParam);
                } else if (_clickCount == 2 && _doubleClickFunc) {
                    _doubleClickFunc(_doubleClickParam);
                } else if (_clickCount >= 2 && _multiClickFunc) {
                    _multiClickFunc(_multiClickParam);
                }
                _clickCount = 0;
                _state = 0;
                _idle = true;
            }
            break;

        case 3: /* LONG PRESS — waiting for release */
            if (!pressed) {
                if (_longPressStopFunc)
                    _longPressStopFunc(_longPressStopParam);
                _longPressed = false;
                _clickCount = 0;
                _state = 0;
                _waitingRelease = false;
                _idle = true;
            }
            break;
        }
    }

private:
    bool readPin()
    {
#ifdef ARCH_NRF54L15
        if (_gpioReady) {
            int val = gpio_pin_get(_gpioPort, _gpioPin);
            return _activeLow ? (val == 0) : (val != 0);
        }
#endif
        return false;
    }

    int _pin;
    bool _activeLow;
    int _state;
    bool _idle;

    uint16_t _clickMs;
    uint16_t _pressMs;
    uint16_t _debounceMs;

    CallbackFunction _clickFunc;
    CallbackFunction _longPressStartFunc;
    CallbackFunction _longPressStopFunc;
    CallbackFunction _doubleClickFunc;
    CallbackFunction _multiClickFunc;

    void *_clickParam;
    void *_longPressStartParam;
    void *_longPressStopParam;
    void *_doubleClickParam;
    void *_multiClickParam;

    unsigned long _lastDebounce;
    unsigned long _pressStart;
    int _clickCount;
    unsigned long _lastClick;
    bool _longPressed;
    bool _waitingRelease;
    bool _lastRaw = false;

#ifdef ARCH_NRF54L15
    const struct device *_gpioPort = nullptr;
    uint8_t _gpioPin = 0;
    bool _gpioReady = false;
#endif
};
