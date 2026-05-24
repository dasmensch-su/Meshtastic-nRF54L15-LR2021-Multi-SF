/*
 * RadioLibHal_Zephyr.h — RadioLib HAL implementation for Zephyr RTOS
 *
 * Provides SPI, GPIO, and timing functions using Zephyr APIs
 * instead of Arduino. Used by RadioLib's Module class.
 */
#pragma once

#include "Hal.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

class RadioLibHal_Zephyr : public RadioLibHal {
public:
    RadioLibHal_Zephyr(const struct spi_dt_spec *spi,
                       const struct gpio_dt_spec *irqPin = nullptr,
                       const struct gpio_dt_spec *rstPin = nullptr,
                       const struct gpio_dt_spec *busyPin = nullptr);

    /* Pure virtuals from RadioLibHal */
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;
    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;

    void init() override;
    void term() override;
    void yield() override;

private:
    const struct spi_dt_spec *spi_;
    const struct gpio_dt_spec *irqPin_;
    const struct gpio_dt_spec *rstPin_;
    const struct gpio_dt_spec *busyPin_;

    /* ISR callback storage */
    static void (*isrCallback_)(void);
    static struct gpio_callback gpioCallbackData_;
    static void gpioIsrHandler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
};
