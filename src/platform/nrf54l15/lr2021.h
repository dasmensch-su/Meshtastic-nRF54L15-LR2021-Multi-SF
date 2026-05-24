/*
 * lr2021.h — LR2021 LoRa radio driver for Zephyr
 *
 * Implements the Semtech LR2021 2-byte opcode SPI protocol:
 *   Phase 1: CS LOW  → [opc_msb, opc_lsb, params...] → CS HIGH
 *   Phase 2: CS LOW  → [0x00, 0x00, ...]  → read response → CS HIGH
 */

#ifndef LR2021_H
#define LR2021_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DIO number for IRQ output — DIO5 for Renode, DIO8 for real Wio-LR2021 hardware */
#define LR2021_DIO_RENODE  5
#define LR2021_DIO_HW      8

#if IS_ENABLED(CONFIG_ENTROPY_NRF_CRACEN_CTR_DRBG)
#define LR2021_DIO_DEFAULT LR2021_DIO_HW
#else
#define LR2021_DIO_DEFAULT LR2021_DIO_RENODE
#endif

/* ---- IRQ bit masks (32-bit irqStatus, big-endian in response) ---- */
#define LR2021_IRQ_RXDONE     (1u << 18)
#define LR2021_IRQ_TXDONE     (1u << 19)
#define LR2021_IRQ_TIMEOUT    (1u << 21)
#define LR2021_IRQ_CRCERROR   (1u << 22)

/* ---- Packet type ---- */
#define LR2021_PKT_LORA       0x00
#define LR2021_PKT_GFSK       0x01

/* ---- LoRa bandwidth codes (from lr20xx_radio_lora_types.h) ---- */
#define LR2021_BW_125         0x04
#define LR2021_BW_250         0x05
#define LR2021_BW_406         0x0E
#define LR2021_BW_500         0x06
#define LR2021_BW_812         0x0F
#define LR2021_BW_1000        0x07

/* ---- LoRa coding rate codes ---- */
#define LR2021_CR_4_5         0x01
#define LR2021_CR_4_6         0x02
#define LR2021_CR_4_7         0x03
#define LR2021_CR_4_8         0x04

/* ---- PA ramp times ---- */
#define LR2021_RAMP_200U      0x02

/* ---- SetStandby modes ---- */
#define LR2021_STDBY_RC       0x00
#define LR2021_STDBY_XOSC     0x01

/* ---- Init / status ---- */
int  lr2021_init(const struct spi_dt_spec *spi);
int  lr2021_get_version(const struct spi_dt_spec *spi,
                        uint8_t *fw_major, uint8_t *fw_minor);
int  lr2021_get_status(const struct spi_dt_spec *spi, uint8_t *chip_mode);

/* ---- Configuration ---- */
int  lr2021_set_standby(const struct spi_dt_spec *spi, uint8_t mode);
int  lr2021_set_packet_type(const struct spi_dt_spec *spi, uint8_t pkt_type);
int  lr2021_set_rf_frequency(const struct spi_dt_spec *spi, uint32_t freq_hz);
int  lr2021_set_pa_config(const struct spi_dt_spec *spi,
                          uint8_t sel, uint8_t lf_mode,
                          uint8_t lf_duty, uint8_t lf_slices, uint8_t hf_duty);
int  lr2021_set_tx_params(const struct spi_dt_spec *spi,
                          int8_t power, uint8_t ramp_time);
int  lr2021_set_lora_mod_params(const struct spi_dt_spec *spi,
                                uint8_t sf, uint8_t bw,
                                uint8_t cr, uint8_t ldro);
int  lr2021_set_lora_pkt_params(const struct spi_dt_spec *spi,
                                uint16_t preamble_len, uint8_t payload_len,
                                uint8_t header_type, uint8_t crc_en);
int  lr2021_set_dio_irq_config(const struct spi_dt_spec *spi,
                               uint8_t dio, uint32_t irq_mask);

/* ---- TX ---- */
int  lr2021_write_tx_fifo(const struct spi_dt_spec *spi,
                          const uint8_t *data, uint8_t len);
int  lr2021_set_tx(const struct spi_dt_spec *spi, uint32_t timeout_us);

/* ---- RX ---- */
int  lr2021_set_rx(const struct spi_dt_spec *spi, uint32_t timeout_us);
int  lr2021_get_rx_buffer_status(const struct spi_dt_spec *spi,
                                 uint8_t *payload_len, uint8_t *rx_start);
int  lr2021_read_rx_fifo(const struct spi_dt_spec *spi,
                         uint8_t *data, uint8_t len);

/* ---- IRQ ---- */
int  lr2021_get_and_clear_irq(const struct spi_dt_spec *spi,
                               uint32_t *irq_status);

/* ---- Packet status (call after RxDone IRQ) ---- */
int  lr2021_get_lora_packet_status(const struct spi_dt_spec *spi,
                                    int8_t *rssi_pkt_dBm,
                                    int8_t *snr_pkt_dB);

/* ---- Sync word ---- */
int  lr2021_set_lora_syncword(const struct spi_dt_spec *spi, uint8_t syncword);

/* ---- Hardware control (BUSY/RESET — optional, from DTS) ---- */
int  lr2021_hw_init(const struct spi_dt_spec *spi);

/* ---- Full LoRa init sequence ---- */
int  lr2021_lora_init(const struct spi_dt_spec *spi,
                      uint32_t freq_hz, uint8_t sf, uint8_t bw, uint8_t cr,
                      int8_t tx_power, uint8_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* LR2021_H */
