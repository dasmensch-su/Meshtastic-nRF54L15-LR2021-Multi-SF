# Meshtastic nRF54L15 Port

Meshtastic mesh networking firmware ported to the **Nordic nRF54L15** SoC
running **Zephyr RTOS**, with the **Semtech LR2021** LoRa transceiver.
Targets the Seeed Studio XIAO nRF54L15 + Wio-LR2021 LoRa Plus Expansion Board.

This is a derivative of the [Meshtastic firmware](https://github.com/meshtastic/firmware)
(GPL-3.0), adapted for the nRF54L15 platform with Zephyr RTOS instead of Arduino/PlatformIO.

## Features

- Full Meshtastic mesh stack (flooding router, reliable routing, PKI, channels)
- CRACEN hardware AES acceleration (ECB + CTR via DMA)
- BLE phone API (iOS/Android Meshtastic app support)
- SSD1306 OLED display with full Meshtastic UI
- LittleFS persistence (config, NodeDB, PKI keys survive reboot)
- PowerFSM (screen timeout, wake on message/button, deep sleep)
- **Multi-SF bridge** — simultaneous RX on up to 4 spreading factors, transparent
  cross-SF relay between peers on different LoRa presets
- Interop verified with Heltec WiFi LoRa 32 V4 and Adafruit Muzi Duo at 915 MHz

## Hardware

| Component | Part |
|-----------|------|
| MCU | XIAO nRF54L15 (ARM Cortex-M33, 256KB RAM, 1.5MB Flash) |
| Radio | Wio-LR2021 LoRa Plus Expansion Board (Semtech LR2021) |
| Display | SSD1306 128x64 OLED (I2C, on expansion board) |
| Debug | Built-in SAMD11 CMSIS-DAPv2 (no J-Link needed) |

## Quick Start

### Prerequisites

- [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/) 0.17.0+
- `west` tool (`pip install west`)
- `pyocd` (`pip install pyocd`)

### Build

```bash
west build -s zephyr -b xiao_nrf54l15/nrf54l15/cpuapp -d zephyr/build_hardware -- \
  -DOVERLAY_CONFIG=prj_hardware.conf \
  -DDTC_OVERLAY_FILE=boards/xiao_nrf54l15_nrf54l15_cpuapp_hardware.overlay \
  -DCONFIG_MESHTASTIC_FULL_STACK=y
```

### Flash

```bash
pyocd flash -t nrf54l -u <BOARD_UID> zephyr/build_hardware/zephyr/zephyr.hex
```

### Connect

Use the [Meshtastic](https://meshtastic.org/docs/software/) iOS/Android app via BLE,
or the CLI:

```bash
meshtastic --port /dev/serial/by-id/usb-Seeed_Studio_..._<UID>-if02 --info
```

## Documentation

- [NRF54L15_PORT.md](docs/NRF54L15_PORT.md) — Full feature list, architecture, test results
- [HARDWARE_BUILD.md](docs/HARDWARE_BUILD.md) — Pin mapping, build options, flash guide, troubleshooting

## Build Size

| Metric | Value |
|--------|-------|
| Flash | 505 KB (34.6% of 1.4 MB) |
| RAM | 141 KB (73.5% of 188 KB) |

## License

GPL-3.0 — see [LICENSE](LICENSE).

Third-party components retain their original licenses:
- [RadioLib](https://github.com/jgromes/RadioLib) (MIT) — LoRa radio abstraction
- [Zephyr RTOS](https://github.com/zephyrproject-rtos/zephyr) (Apache-2.0)
- [nanopb](https://github.com/nanopb/nanopb) (zlib)
