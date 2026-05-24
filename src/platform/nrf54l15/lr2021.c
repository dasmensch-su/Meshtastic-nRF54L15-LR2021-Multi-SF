/*
 * lr2021.c — LR2021 LoRa radio driver for Zephyr
 *
 * Two-phase SPI protocol:
 *   Phase 1: CS LOW  → [opc_msb, opc_lsb, param...] → CS HIGH
 *   Phase 2: CS LOW  → [0x00, 0x00, ...] → read response → CS HIGH
 */

#include "lr2021.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

#define LR2021_NODE DT_NODELABEL(lr2021)

/* Optional BUSY and RESET GPIOs from DTS (only on real hardware) */
#if DT_NODE_HAS_PROP(LR2021_NODE, busy_gpios)
static const struct gpio_dt_spec lr2021_busy =
    GPIO_DT_SPEC_GET(LR2021_NODE, busy_gpios);
#define HAS_BUSY 1
#else
#define HAS_BUSY 0
#endif

#if DT_NODE_HAS_PROP(LR2021_NODE, reset_gpios)
static const struct gpio_dt_spec lr2021_reset =
    GPIO_DT_SPEC_GET(LR2021_NODE, reset_gpios);
#define HAS_RESET 1
#else
#define HAS_RESET 0
#endif

/* Wait for BUSY to go low (radio ready for SPI commands).
 * On real hardware, BUSY is high while the radio processes a command.
 * In Renode (no BUSY pin), this is a no-op. */
static int lr_wait_busy(void)
{
#if HAS_BUSY
    int timeout_ms = 1000;
    while (gpio_pin_get_dt(&lr2021_busy) != 0) {
        k_usleep(100);
        if (--timeout_ms <= 0) {
            return -ETIMEDOUT;
        }
    }
#endif
    return 0;
}

/* Static FIFO buffers — moved off stack to prevent overflow */
#define FIFO_BUF_SIZE 260
static uint8_t fifo_tx_buf[FIFO_BUF_SIZE];
static uint8_t fifo_rx_buf[FIFO_BUF_SIZE];

/* Internal: two-phase SPI command — static buffers to avoid stack overflow */
static uint8_t cmd_tx_buf[66];
static uint8_t cmd_dummy[66];
static uint8_t cmd_resp_buf[66];

static int lr_cmd(const struct spi_dt_spec *spi,
                  uint8_t opc_msb, uint8_t opc_lsb,
                  const uint8_t *params, size_t param_len,
                  uint8_t *resp, size_t resp_len)
{
	/* Wait for radio to be ready before SPI command */
	int busy_ret = lr_wait_busy();
	if (busy_ret) return busy_ret;

	cmd_tx_buf[0] = opc_msb;
	cmd_tx_buf[1] = opc_lsb;
	if (params && param_len > 0) {
		memcpy(cmd_tx_buf + 2, params, param_len);
	}

	struct spi_buf tx_bufs1[] = {{ .buf = cmd_tx_buf, .len = 2 + param_len }};
	struct spi_buf_set tx_set1 = { .buffers = tx_bufs1, .count = 1 };

	/* Phase 1: send opcode + params (CS toggles around this transfer) */
	int ret = spi_transceive_dt(spi, &tx_set1, NULL);
	if (ret) {
		return ret;
	}

	if (resp && resp_len > 0) {
		/* Phase 2: clock out dummy bytes to receive response */
		memset(cmd_dummy, 0, resp_len);
		memset(cmd_resp_buf, 0, resp_len);

		struct spi_buf tx_bufs2[] = {{ .buf = cmd_dummy, .len = resp_len }};
		struct spi_buf rx_bufs[]  = {{ .buf = cmd_resp_buf, .len = resp_len }};
		struct spi_buf_set tx_set2 = { .buffers = tx_bufs2, .count = 1 };
		struct spi_buf_set rx_set  = { .buffers = rx_bufs,  .count = 1 };

		ret = spi_transceive_dt(spi, &tx_set2, &rx_set);
		if (ret) {
			return ret;
		}
		memcpy(resp, cmd_resp_buf, resp_len);
	}

	return 0;
}

/* Internal: direct FIFO write (opcode + data in one frame) */

static int lr_fifo_write(const struct spi_dt_spec *spi,
                         uint8_t opc_msb, uint8_t opc_lsb,
                         const uint8_t *data, uint8_t len)
{
	int busy_ret = lr_wait_busy();
	if (busy_ret) return busy_ret;

	if (2 + len > FIFO_BUF_SIZE) return -ENOMEM;

	fifo_tx_buf[0] = opc_msb;
	fifo_tx_buf[1] = opc_lsb;
	if (data && len > 0) {
		memcpy(fifo_tx_buf + 2, data, len);
	}

	struct spi_buf tx_bufs[] = {{ .buf = fifo_tx_buf, .len = 2 + len }};
	struct spi_buf_set tx_set = { .buffers = tx_bufs, .count = 1 };

	return spi_transceive_dt(spi, &tx_set, NULL);
}

/* Internal: direct FIFO read (opcode + dummy bytes to clock response) */
static int lr_fifo_read(const struct spi_dt_spec *spi,
                        uint8_t opc_msb, uint8_t opc_lsb,
                        uint8_t *data, uint8_t len)
{
	int busy_ret = lr_wait_busy();
	if (busy_ret) return busy_ret;

	if (2 + len > FIFO_BUF_SIZE) return -ENOMEM;

	memset(fifo_tx_buf, 0, 2 + len);
	fifo_tx_buf[0] = opc_msb;
	fifo_tx_buf[1] = opc_lsb;

	struct spi_buf tx_bufs[] = {{ .buf = fifo_tx_buf, .len = 2 + len }};
	struct spi_buf rx_bufs[] = {{ .buf = fifo_rx_buf, .len = 2 + len }};
	struct spi_buf_set tx_set = { .buffers = tx_bufs, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = rx_bufs, .count = 1 };

	int ret = spi_transceive_dt(spi, &tx_set, &rx_set);
	if (ret) {
		return ret;
	}
	/* Skip the 2 opcode bytes in the RX buffer */
	if (data && len > 0) {
		memcpy(data, fifo_rx_buf + 2, len);
	}
	return 0;
}

/* ---- Public API ---- */

int lr2021_hw_init(const struct spi_dt_spec *spi)
{
	int ret;

#if HAS_BUSY
	if (!gpio_is_ready_dt(&lr2021_busy)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&lr2021_busy, GPIO_INPUT);
	if (ret) return ret;
#endif

#if HAS_RESET
	if (!gpio_is_ready_dt(&lr2021_reset)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&lr2021_reset, GPIO_OUTPUT_ACTIVE);
	if (ret) return ret;

	/* Assert reset (active-low: drive low) */
	gpio_pin_set_dt(&lr2021_reset, 1);
	k_msleep(10);
	/* Deassert reset */
	gpio_pin_set_dt(&lr2021_reset, 0);
	k_msleep(20);  /* Wait for radio to boot */
#endif

	(void)ret;
	return 0;
}

int lr2021_init(const struct spi_dt_spec *spi)
{
	if (!spi_is_ready_dt(spi)) {
		return -ENODEV;
	}

	/* Initialize BUSY/RESET GPIOs if present in DTS */
	int ret = lr2021_hw_init(spi);
	if (ret) return ret;

	return 0;
}

int lr2021_get_version(const struct spi_dt_spec *spi,
                       uint8_t *fw_major, uint8_t *fw_minor)
{
	uint8_t resp[4] = { 0 };
	int ret = lr_cmd(spi, 0x01, 0x01, NULL, 0, resp, 4);
	if (ret) {
		return ret;
	}
	/* resp[0..1] = status word, resp[2] = FwMajor, resp[3] = FwMinor */
	if (fw_major) *fw_major = resp[2];
	if (fw_minor) *fw_minor = resp[3];
	return 0;
}

int lr2021_get_status(const struct spi_dt_spec *spi, uint8_t *chip_mode)
{
	uint8_t resp[2] = { 0 };
	int ret = lr_cmd(spi, 0x01, 0x00, NULL, 0, resp, 2);
	if (ret) {
		return ret;
	}
	/* ChipMode in resp[1] bits [2:0] */
	if (chip_mode) *chip_mode = resp[1] & 0x07;
	return 0;
}

int lr2021_set_standby(const struct spi_dt_spec *spi, uint8_t mode)
{
	uint8_t p[1] = { mode };
	return lr_cmd(spi, 0x01, 0x28, p, 1, NULL, 0);
}

int lr2021_set_packet_type(const struct spi_dt_spec *spi, uint8_t pkt_type)
{
	uint8_t p[1] = { pkt_type };
	return lr_cmd(spi, 0x02, 0x07, p, 1, NULL, 0);
}

int lr2021_set_rf_frequency(const struct spi_dt_spec *spi, uint32_t freq_hz)
{
	uint8_t p[4];
	p[0] = (freq_hz >> 24) & 0xFF;
	p[1] = (freq_hz >> 16) & 0xFF;
	p[2] = (freq_hz >> 8)  & 0xFF;
	p[3] =  freq_hz        & 0xFF;
	return lr_cmd(spi, 0x02, 0x00, p, 4, NULL, 0);
}

int lr2021_set_pa_config(const struct spi_dt_spec *spi,
                         uint8_t sel, uint8_t lf_mode,
                         uint8_t lf_duty, uint8_t lf_slices, uint8_t hf_duty)
{
	/* Try raw 5-byte format (TX worked with this before encoding fix) */
	uint8_t p[5] = { sel, lf_mode, lf_duty, lf_slices, hf_duty };
	return lr_cmd(spi, 0x02, 0x02, p, 5, NULL, 0);
}

int lr2021_set_tx_params(const struct spi_dt_spec *spi,
                         int8_t power, uint8_t ramp_time)
{
	uint8_t p[2] = { (uint8_t)power, ramp_time };
	return lr_cmd(spi, 0x02, 0x03, p, 2, NULL, 0);
}

int lr2021_set_lora_mod_params(const struct spi_dt_spec *spi,
                               uint8_t sf, uint8_t bw,
                               uint8_t cr, uint8_t ppm)
{
	/* Try raw 4-byte format (TX worked with this before encoding fix) */
	uint8_t p[4] = { sf, bw, cr, ppm };
	return lr_cmd(spi, 0x02, 0x20, p, 4, NULL, 0);
}

int lr2021_set_lora_pkt_params(const struct spi_dt_spec *spi,
                                uint16_t preamble_len, uint8_t payload_len,
                                uint8_t header_type, uint8_t crc_en)
{
	/* Try raw 5-byte format (TX worked with this before encoding fix) */
	uint8_t p[5];
	p[0] = (preamble_len >> 8) & 0xFF;
	p[1] =  preamble_len       & 0xFF;
	p[2] = payload_len;
	p[3] = header_type;
	p[4] = crc_en;
	return lr_cmd(spi, 0x02, 0x21, p, 5, NULL, 0);
}

int lr2021_set_dio_irq_config(const struct spi_dt_spec *spi,
                              uint8_t dio, uint32_t irq_mask)
{
	uint8_t p[5];
	p[0] = dio;
	p[1] = (irq_mask >> 24) & 0xFF;
	p[2] = (irq_mask >> 16) & 0xFF;
	p[3] = (irq_mask >> 8)  & 0xFF;
	p[4] =  irq_mask        & 0xFF;
	return lr_cmd(spi, 0x01, 0x15, p, 5, NULL, 0);
}

int lr2021_get_and_clear_irq(const struct spi_dt_spec *spi,
                              uint32_t *irq_status)
{
	uint8_t resp[6] = { 0 };
	int ret = lr_cmd(spi, 0x01, 0x17, NULL, 0, resp, 6);
	if (ret) {
		return ret;
	}
	/* resp[0..1] = status, resp[2..5] = 32-bit IRQ status big-endian */
	if (irq_status) {
		*irq_status = ((uint32_t)resp[2] << 24) |
		              ((uint32_t)resp[3] << 16) |
		              ((uint32_t)resp[4] << 8)  |
		               resp[5];
	}
	return 0;
}

int lr2021_write_tx_fifo(const struct spi_dt_spec *spi,
                         const uint8_t *data, uint8_t len)
{
	return lr_fifo_write(spi, 0x00, 0x02, data, len);
}

int lr2021_set_tx(const struct spi_dt_spec *spi, uint32_t timeout_us)
{
	if (timeout_us == 0) {
		/* Use default timeout variant — no params, just opcode */
		return lr_cmd(spi, 0x02, 0x0D, NULL, 0, NULL, 0);
	}
	/* Convert to RTC steps: timeout_ms * 32768 / 1000 */
	uint32_t rtc_steps = (timeout_us / 1000) * 32768 / 1000;
	uint8_t p[3];
	p[0] = (rtc_steps >> 16) & 0xFF;
	p[1] = (rtc_steps >> 8)  & 0xFF;
	p[2] =  rtc_steps        & 0xFF;
	return lr_cmd(spi, 0x02, 0x0D, p, 3, NULL, 0);
}

int lr2021_set_rx(const struct spi_dt_spec *spi, uint32_t timeout_us)
{
	if (timeout_us == 0) {
		/* Continuous RX — no params, just opcode */
		return lr_cmd(spi, 0x02, 0x0C, NULL, 0, NULL, 0);
	}
	uint32_t rtc_steps = (timeout_us / 1000) * 32768 / 1000;
	uint8_t p[3];
	p[0] = (rtc_steps >> 16) & 0xFF;
	p[1] = (rtc_steps >> 8)  & 0xFF;
	p[2] =  rtc_steps        & 0xFF;
	return lr_cmd(spi, 0x02, 0x0C, p, 3, NULL, 0);
}

int lr2021_get_rx_buffer_status(const struct spi_dt_spec *spi,
                                uint8_t *payload_len, uint8_t *rx_start)
{
	/* GetRxFifoLevel (0x011C) returns 2 bytes: FIFO level MSB, LSB.
	 * Our lr_cmd Phase 2 reads [stat0, stat1, data0, data1]. */
	uint8_t resp[4] = { 0 };
	int ret = lr_cmd(spi, 0x01, 0x1C, NULL, 0, resp, 4);
	if (ret) {
		return ret;
	}
	uint16_t level = ((uint16_t)resp[2] << 8) | resp[3];
	printk("LR2021: rx_fifo_level=%u (raw: %02X %02X %02X %02X)\n",
	       level, resp[0], resp[1], resp[2], resp[3]);
	/* Clamp to 255 max (uint8_t) */
	if (payload_len) *payload_len = (level > 255) ? 255 : (uint8_t)level;
	if (rx_start)    *rx_start    = 0;
	return 0;
}

int lr2021_read_rx_fifo(const struct spi_dt_spec *spi,
                        uint8_t *data, uint8_t len)
{
	return lr_fifo_read(spi, 0x00, 0x01, data, len);
}

int lr2021_get_lora_packet_status(const struct spi_dt_spec *spi,
                                   int8_t *rssi_pkt_dBm,
                                   int8_t *snr_pkt_dB)
{
	/* Response: [stat_msb, stat_lsb, rssi_pkt, snr_pkt, signal_rssi] */
	uint8_t resp[5] = { 0 };
	int ret = lr_cmd(spi, 0x02, 0x2A, NULL, 0, resp, 5);
	if (ret) {
		return ret;
	}
	if (rssi_pkt_dBm) *rssi_pkt_dBm = (int8_t)resp[2];
	if (snr_pkt_dB)   *snr_pkt_dB   = (int8_t)resp[3];
	return 0;
}

int lr2021_set_lora_syncword(const struct spi_dt_spec *spi, uint8_t syncword)
{
	uint8_t p[1] = { syncword };
	return lr_cmd(spi, 0x02, 0x23, p, 1, NULL, 0);
}

/* SetRxPath — select LF (sub-GHz) or HF (2.4 GHz) RX path + boost mode */
static int lr2021_set_rx_path(const struct spi_dt_spec *spi,
                               uint8_t path, uint8_t boost)
{
	uint8_t p[2] = { path, boost };
	return lr_cmd(spi, 0x02, 0x01, p, 2, NULL, 0);
}

/* CalibrateFrontEnd — calibrate RX front-end for current frequency */
static int lr2021_calibrate_front_end(const struct spi_dt_spec *spi)
{
	return lr_cmd(spi, 0x01, 0x23, NULL, 0, NULL, 0);
}

/* SetRegMode — select LDO (0x00) or DC-DC (0x01) regulator */
static int lr2021_set_reg_mode(const struct spi_dt_spec *spi, uint8_t mode)
{
	uint8_t p[1] = { mode };
	return lr_cmd(spi, 0x01, 0x21, p, 1, NULL, 0);
}

int lr2021_lora_init(const struct spi_dt_spec *spi,
                     uint32_t freq_hz, uint8_t sf, uint8_t bw, uint8_t cr,
                     int8_t tx_power, uint8_t payload_len)
{
	int ret;
	/* Determine if 2.4 GHz (HF path) or sub-GHz (LF path) */
	uint8_t is_hf = (freq_hz >= 2000000000UL) ? 1 : 0;

	ret = lr2021_set_standby(spi, LR2021_STDBY_RC);
	if (ret) return ret;

	/* Configure TCXO at 1.8V (required for Wio-LR2021 board) */
	{
		uint8_t tcxo_p[5] = { 0x02, 0x00, 0x00, 0x00, 0x00 }; /* 1.8V, no delay */
		ret = lr_cmd(spi, 0x01, 0x20, tcxo_p, 5, NULL, 0);
		if (ret) return ret;
	}

	/* Set DC-DC regulator mode (more efficient than LDO) */
	ret = lr2021_set_reg_mode(spi, 0x01);
	if (ret) return ret;

	ret = lr2021_set_packet_type(spi, LR2021_PKT_LORA);
	if (ret) return ret;

	ret = lr2021_set_rf_frequency(spi, freq_hz);
	if (ret) return ret;

	/* Set RX path: HF for 2.4 GHz, LF for sub-GHz. Boost mode 7 for max sensitivity. */
	ret = lr2021_set_rx_path(spi, is_hf, 0x07);
	if (ret) return ret;

	/* Calibrate front-end for the configured frequency */
	ret = lr2021_calibrate_front_end(spi);
	if (ret) return ret;
	k_msleep(5); /* Allow calibration to complete */

	/* PA config — values from official Wio-LR2021 shield DTS power tables.
	 * HF (2.4 GHz): pa_sel=HF(1), hf_duty from table, lf fields zeroed.
	 * LF (sub-GHz): pa_sel=LF(0), lf_duty/slices from table, hf_duty zeroed.
	 * tx_power is in dBm; SetTxParams expects half_power (half-dB steps). */
	if (is_hf) {
		/* 2.4 GHz: 10 dBm → half_power=24, hf_duty=30, pa_hp_sel handled by hf_duty */
		ret = lr2021_set_pa_config(spi, 0x01, 0x00, 0x00, 0x00, 30);
	} else {
		/* Sub-GHz: 10 dBm → lf_duty=0x02, lf_slices=0x01 */
		ret = lr2021_set_pa_config(spi, 0x00, 0x00, 0x02, 0x01, 0x00);
	}
	if (ret) return ret;

	/* SetTxParams: power in half-dB steps (10 dBm → 20, or use table value 24 for HF) */
	int8_t half_power = is_hf ? 24 : (int8_t)(tx_power * 2);
	ret = lr2021_set_tx_params(spi, half_power, LR2021_RAMP_200U);
	if (ret) return ret;

	/* PPM offset: use PPM_1_4 for BW >= 812 kHz (2.4 GHz), NO_PPM for sub-GHz */
	uint8_t ppm = (bw >= 0x0E) ? 0x01 : 0x00;
	ret = lr2021_set_lora_mod_params(spi, sf, bw, cr, ppm);
	if (ret) return ret;

	ret = lr2021_set_lora_pkt_params(spi, 16, payload_len, 0x00, 0x01);
	if (ret) return ret;

	ret = lr2021_set_dio_irq_config(spi, LR2021_DIO_DEFAULT,
		LR2021_IRQ_TXDONE | LR2021_IRQ_RXDONE |
		LR2021_IRQ_TIMEOUT | LR2021_IRQ_CRCERROR);
	if (ret) return ret;

	return 0;
}
