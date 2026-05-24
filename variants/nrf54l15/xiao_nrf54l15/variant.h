/*
 * Meshtastic board variant — XIAO nRF54L15 + Wio-LR2021
 *
 * Pin mapping derived from:
 *   - usp_zephyr/boards/seeed/xiao_nrf54l15/seeed_xiao_connector.dtsi
 *   - usp_zephyr/boards/shields/semtech_wio_lr20xx/semtech_wio_lr20xx_common.dtsi
 *
 * GPIO numbering: port * 32 + pin  (P1.04 = 36, etc.)
 */

#ifndef _VARIANT_XIAO_NRF54L15_
#define _VARIANT_XIAO_NRF54L15_

#define XIAO_NRF54L15

/* ---------------------------------------------------------------
 * LR2021 radio pin assignments (Zephyr logical GPIO numbers)
 * ---------------------------------------------------------------
 *  D0 = gpio1.4  → IRQ (DIO8)
 *  D1 = gpio1.5  → BUSY
 *  D2 = gpio1.6  → RESET
 *  D3 = gpio1.7  → CS
 */
#define LR2021_IRQ_PIN    (1 * 32 + 4)   /* D0 = P1.04 */
#define LR2021_BUSY_PIN   (1 * 32 + 5)   /* D1 = P1.05 */
#define LR2021_RESET_PIN  (1 * 32 + 6)   /* D2 = P1.06 */
#define LR2021_CS_PIN     (1 * 32 + 7)   /* D3 = P1.07 */

/* SPI bus: spi00 (SPIM00) at 16 MHz */
#define LR2021_SPI_DEV    "spi00"
#define LR2021_SPI_FREQ   16000000UL

/* Console UART: uart20 */
#define CONSOLE_UART_DEV  "uart20"

/*
 * LR2021 uses a completely different SPI opcode set from LR1121 — every
 * command opcode differs.  We use our own LR2021Interface (not RadioLib).
 */
#define USE_LR2021
#define LR2021_SPI_NSS_PIN  LR2021_CS_PIN
#define LR2021_IRQ_PIN_NUM  LR2021_IRQ_PIN
#define LR2021_NRESET_PIN   LR2021_RESET_PIN
#define LR2021_BUSY_PIN_NUM LR2021_BUSY_PIN

/* User button on P0.0 — active low with pull-up */
#define BUTTON_PIN          (0 * 32 + 0)   /* P0.00 */
#define BUTTON_ACTIVE_LOW   true
#define BUTTON_ACTIVE_PULLUP true

/* Green user LED on P2.0 — active high, 1.5K series resistor */
#define PIN_LED             (2 * 32 + 0)   /* P2.00 / USR_LED */
#define EXT_NOTIFY_OUT      PIN_LED

/* HW model — use PRIVATE_HW until nRF54L15 is upstreamed in Meshtastic protobuf */
#define HW_VENDOR meshtastic_HardwareModel_PRIVATE_HW

#endif /* _VARIANT_XIAO_NRF54L15_ */
