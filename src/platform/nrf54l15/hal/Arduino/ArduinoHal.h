/*
 * ArduinoHal.h — Zephyr shim for RadioLib's ArduinoHal
 *
 * Shadows RadioLib's hal/Arduino/ArduinoHal.h via include path priority.
 * Provides the same ArduinoHal class interface but uses Zephyr SPI/GPIO.
 * LockingArduinoHal in RadioLibInterface.h extends this transparently.
 */
#ifndef _RADIOLIB_ARDUINOHAL_H
#define _RADIOLIB_ARDUINOHAL_H

#include "TypeDef.h"
#include "Hal.h"
#include "SPI.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

/* Global SPI/GPIO specs set by initLoRa before ArduinoHal is used */
extern const struct spi_dt_spec *g_radiolib_spi;
extern const struct gpio_dt_spec *g_radiolib_irq;
extern const struct gpio_dt_spec *g_radiolib_rst;
extern const struct gpio_dt_spec *g_radiolib_busy;

class ArduinoHal : public RadioLibHal {
  public:
    ArduinoHal();
    explicit ArduinoHal(SPIClass& spi, SPISettings spiSettings = SPISettings());

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
    void tone(uint32_t pin, unsigned int frequency, RadioLibTime_t duration = 0) override;
    void noTone(uint32_t pin) override;
    void yield() override;
    uint32_t pinToInterrupt(uint32_t pin) override;

#if !RADIOLIB_GODMODE
  protected:
#endif
    SPIClass* spi = NULL;
    SPISettings spiSettings;
    bool initInterface = false;

  private:
    static void (*isrCallback_)(void);
    static struct gpio_callback gpioCallbackData_;
    static void gpioIsrHandler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
};

#endif /* _RADIOLIB_ARDUINOHAL_H */
