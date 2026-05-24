/*
 * ArduinoHal.cpp — Zephyr implementation of RadioLib's ArduinoHal
 */

#include "ArduinoHal.h"
#include <string.h>
#include <zephyr/sys/printk.h>

/* Global SPI/GPIO specs — set by initLoRa() before radio init */
const struct spi_dt_spec *g_radiolib_spi = nullptr;
const struct gpio_dt_spec *g_radiolib_irq = nullptr;
const struct gpio_dt_spec *g_radiolib_rst = nullptr;
const struct gpio_dt_spec *g_radiolib_busy = nullptr;

/* Static ISR storage */
void (*ArduinoHal::isrCallback_)(void) = nullptr;
struct gpio_callback ArduinoHal::gpioCallbackData_;

/* rlb_time_us() is defined in RadioLib's Hal.cpp */

/* RadioLib debug printf — route to Zephyr printk */
void rlb_printf(bool /*addTimestamp*/, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintk(fmt, ap);
    va_end(ap);
}

ArduinoHal::ArduinoHal()
    : RadioLibHal(GPIO_INPUT, GPIO_OUTPUT, 0, 1,
                  GPIO_INT_EDGE_RISING, GPIO_INT_EDGE_FALLING)
{
}

ArduinoHal::ArduinoHal(SPIClass& spi, SPISettings spiSettings)
    : RadioLibHal(GPIO_INPUT, GPIO_OUTPUT, 0, 1,
                  GPIO_INT_EDGE_RISING, GPIO_INT_EDGE_FALLING),
      spi(&spi), spiSettings(spiSettings)
{
}

void ArduinoHal::init()
{
    /* Explicitly configure GPIO pins from DTS */
    if (g_radiolib_rst)
        gpio_pin_configure_dt(g_radiolib_rst, GPIO_OUTPUT_INACTIVE);
    if (g_radiolib_busy)
        gpio_pin_configure_dt(g_radiolib_busy, GPIO_INPUT | GPIO_PULL_UP);
    if (g_radiolib_irq)
        gpio_pin_configure_dt(g_radiolib_irq, GPIO_INPUT);
}
void ArduinoHal::term() { }

void ArduinoHal::pinMode(uint32_t pin, uint32_t mode)
{
    if (g_radiolib_rst && pin == g_radiolib_rst->pin) {
        gpio_pin_configure_dt(g_radiolib_rst, mode == GPIO_OUTPUT ? GPIO_OUTPUT_INACTIVE : GPIO_INPUT);
    }
    if (g_radiolib_busy && pin == g_radiolib_busy->pin) {
        gpio_pin_configure_dt(g_radiolib_busy, GPIO_INPUT);
    }
    if (g_radiolib_irq && pin == g_radiolib_irq->pin) {
        gpio_pin_configure_dt(g_radiolib_irq, GPIO_INPUT);
    }
}

void ArduinoHal::digitalWrite(uint32_t pin, uint32_t value)
{
    if (g_radiolib_rst && pin == g_radiolib_rst->pin) {
        /* RadioLib passes raw pin levels (LOW=0, HIGH=1).
         * gpio_pin_set_raw_dt bypasses the active-low inversion
         * from DTS so the wire level matches what RadioLib expects. */
        gpio_pin_set_raw(g_radiolib_rst->port, g_radiolib_rst->pin, value);
    }
}

uint32_t ArduinoHal::digitalRead(uint32_t pin)
{
    if (g_radiolib_irq && pin == g_radiolib_irq->pin) {
        return gpio_pin_get_dt(g_radiolib_irq);
    }
    if (g_radiolib_busy && pin == g_radiolib_busy->pin) {
        return gpio_pin_get_dt(g_radiolib_busy);
    }
    return 0;
}

void ArduinoHal::gpioIsrHandler(const struct device *dev,
                                struct gpio_callback *cb,
                                uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    if (isrCallback_) {
        isrCallback_();
    }
}

void ArduinoHal::attachInterrupt(uint32_t interruptNum,
                                 void (*interruptCb)(void),
                                 uint32_t mode)
{
    if (!g_radiolib_irq) {
        printk("HAL:attachInterrupt — no irq gpio!\n");
        return;
    }
    isrCallback_ = interruptCb;

    gpio_pin_configure_dt(g_radiolib_irq, GPIO_INPUT);
    gpio_init_callback(&gpioCallbackData_, gpioIsrHandler, BIT(g_radiolib_irq->pin));
    gpio_add_callback(g_radiolib_irq->port, &gpioCallbackData_);
    gpio_pin_interrupt_configure_dt(g_radiolib_irq, GPIO_INT_EDGE_RISING);
}

void ArduinoHal::detachInterrupt(uint32_t interruptNum)
{
    if (!g_radiolib_irq) return;
    gpio_pin_interrupt_configure_dt(g_radiolib_irq, GPIO_INT_DISABLE);
    isrCallback_ = nullptr;
}

void ArduinoHal::delay(RadioLibTime_t ms) { k_msleep((int32_t)ms); }
void ArduinoHal::delayMicroseconds(RadioLibTime_t us) { k_usleep((int32_t)us); }
RadioLibTime_t ArduinoHal::millis() { return (RadioLibTime_t)k_uptime_get(); }
RadioLibTime_t ArduinoHal::micros() { return (RadioLibTime_t)(k_uptime_get() * 1000); }

long ArduinoHal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout)
{
    (void)pin; (void)state; (void)timeout;
    return 0;
}

void ArduinoHal::spiBegin() { }

void ArduinoHal::spiBeginTransaction() { }

void ArduinoHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in)
{
    if (!g_radiolib_spi) return;

    struct spi_buf tx_buf = { .buf = out, .len = len };
    struct spi_buf rx_buf = { .buf = in, .len = len };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    if (in) {
        spi_transceive_dt(g_radiolib_spi, &tx_set, &rx_set);
    } else {
        spi_write_dt(g_radiolib_spi, &tx_set);
    }
}

void ArduinoHal::spiEndTransaction() { }
void ArduinoHal::spiEnd() { }

void ArduinoHal::tone(uint32_t pin, unsigned int frequency, RadioLibTime_t duration)
{
    (void)pin; (void)frequency; (void)duration;
}

void ArduinoHal::noTone(uint32_t pin) { (void)pin; }
void ArduinoHal::yield() { k_yield(); }

uint32_t ArduinoHal::pinToInterrupt(uint32_t pin)
{
    return pin; /* Direct mapping on Zephyr */
}
