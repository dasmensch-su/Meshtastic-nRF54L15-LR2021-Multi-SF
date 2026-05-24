/*
 * RadioLibHal_Zephyr.cpp — RadioLib HAL implementation for Zephyr RTOS
 */

#include "RadioLibHal_Zephyr.h"
#include <string.h>
#include <zephyr/sys/printk.h>

/* Static members */
void (*RadioLibHal_Zephyr::isrCallback_)(void) = nullptr;
struct gpio_callback RadioLibHal_Zephyr::gpioCallbackData_;

/* Global time function required by RadioLib */
RadioLibTime_t rlb_time_us() {
    return (RadioLibTime_t)(k_uptime_get() * 1000);
}

RadioLibHal_Zephyr::RadioLibHal_Zephyr(const struct spi_dt_spec *spi,
                                       const struct gpio_dt_spec *irqPin,
                                       const struct gpio_dt_spec *rstPin,
                                       const struct gpio_dt_spec *busyPin)
    : RadioLibHal(
        GPIO_INPUT,        /* GpioModeInput */
        GPIO_OUTPUT,       /* GpioModeOutput */
        0,                 /* GpioLevelLow */
        1,                 /* GpioLevelHigh */
        GPIO_INT_EDGE_RISING,  /* GpioInterruptRising */
        GPIO_INT_EDGE_FALLING  /* GpioInterruptFalling */
    ),
    spi_(spi), irqPin_(irqPin), rstPin_(rstPin), busyPin_(busyPin)
{
}

void RadioLibHal_Zephyr::init()
{
    /* SPI is already configured via DTS — nothing to do */
}

void RadioLibHal_Zephyr::term()
{
}

void RadioLibHal_Zephyr::pinMode(uint32_t pin, uint32_t mode)
{
    /* Pin modes are configured via DTS, this is a no-op for Zephyr.
     * RadioLib calls this for IRQ, RST, BUSY pins but we configure
     * them in the DTS overlay. */
    (void)pin;
    (void)mode;
}

void RadioLibHal_Zephyr::digitalWrite(uint32_t pin, uint32_t value)
{
    /* RadioLib uses pin numbers, but we use DTS GPIO specs.
     * Map known pins to our DTS specs. */
    if (rstPin_ && pin == rstPin_->pin) {
        gpio_pin_set_dt(rstPin_, value);
    }
}

uint32_t RadioLibHal_Zephyr::digitalRead(uint32_t pin)
{
    if (irqPin_ && pin == irqPin_->pin) {
        return gpio_pin_get_dt(irqPin_);
    }
    if (busyPin_ && pin == busyPin_->pin) {
        return gpio_pin_get_dt(busyPin_);
    }
    return 0;
}

void RadioLibHal_Zephyr::gpioIsrHandler(const struct device *dev,
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

void RadioLibHal_Zephyr::attachInterrupt(uint32_t interruptNum,
                                         void (*interruptCb)(void),
                                         uint32_t mode)
{
    if (!irqPin_) return;
    isrCallback_ = interruptCb;

    gpio_pin_configure_dt(irqPin_, GPIO_INPUT);
    gpio_init_callback(&gpioCallbackData_, gpioIsrHandler, BIT(irqPin_->pin));
    gpio_add_callback(irqPin_->port, &gpioCallbackData_);
    gpio_pin_interrupt_configure_dt(irqPin_, GPIO_INT_EDGE_RISING);
}

void RadioLibHal_Zephyr::detachInterrupt(uint32_t interruptNum)
{
    if (!irqPin_) return;
    gpio_pin_interrupt_configure_dt(irqPin_, GPIO_INT_DISABLE);
    isrCallback_ = nullptr;
}

void RadioLibHal_Zephyr::delay(RadioLibTime_t ms)
{
    k_msleep((int32_t)ms);
}

void RadioLibHal_Zephyr::delayMicroseconds(RadioLibTime_t us)
{
    k_usleep((int32_t)us);
}

RadioLibTime_t RadioLibHal_Zephyr::millis()
{
    return (RadioLibTime_t)k_uptime_get();
}

RadioLibTime_t RadioLibHal_Zephyr::micros()
{
    return (RadioLibTime_t)(k_uptime_get() * 1000);
}

long RadioLibHal_Zephyr::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout)
{
    (void)pin; (void)state; (void)timeout;
    return 0; /* Not used by LR2021 */
}

void RadioLibHal_Zephyr::spiBegin()
{
    /* SPI is already initialized via DTS */
}

void RadioLibHal_Zephyr::spiBeginTransaction()
{
    /* CS is managed by the SPI driver via DTS cs-gpios */
}

void RadioLibHal_Zephyr::spiTransfer(uint8_t* out, size_t len, uint8_t* in)
{
    struct spi_buf tx_buf = { .buf = out, .len = len };
    struct spi_buf rx_buf = { .buf = in, .len = len };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    if (in) {
        spi_transceive_dt(spi_, &tx_set, &rx_set);
    } else {
        spi_write_dt(spi_, &tx_set);
    }
}

void RadioLibHal_Zephyr::spiEndTransaction()
{
}

void RadioLibHal_Zephyr::spiEnd()
{
}

void RadioLibHal_Zephyr::yield()
{
    k_yield();
}
