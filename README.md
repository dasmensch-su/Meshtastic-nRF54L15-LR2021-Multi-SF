# Meshtastic nRF54L15 Port

Meshtastic mesh networking firmware ported to the **Nordic nRF54L15** SoC
running **Zephyr RTOS**, with the **Semtech LR2021** LoRa transceiver.
Targets the Seeed Studio XIAO nRF54L15 + Wio-LR2021 LoRa Plus Expansion Board.

This is a derivative of the [Meshtastic firmware](https://github.com/meshtastic/firmware)
(GPL-3.0), adapted for the nRF54L15 platform with Zephyr RTOS instead of Arduino/PlatformIO.

> ## ⚠️ Required: patched RadioLib for Multi-SF
>
> **The Multi-SF bridge will not work with stock RadioLib.** The entire feature
> depends on knowing *which spreading-factor detector* received each packet, and
> stock RadioLib discards that information. The vendored copy in `lib/RadioLib`
> (based on **RadioLib 7.6.0**) carries a small fork patch that exposes it. If
> you build against an unpatched RadioLib, `getLastRxDetector()` always reports
> the main SF, so the bridge can only transmit on its main SF and **direct
> messages to peers on side SFs silently fail.**
>
> If you replace or update RadioLib (or port this firmware to another framework
> such as Arduino), you must re-apply the patch below.
>
> **1. `lib/RadioLib/src/modules/LR2021/LR2021.h`** — add a 7th `detector`
> parameter to `getLoRaPacketStatus`. (Note this fork also orders the first two
> parameters as `cr, crc`, not stock's `crc, cr`.)
>
> ```cpp
> int16_t getLoRaPacketStatus(uint8_t* cr, bool* crc, uint8_t* packetLen = NULL,
>     float* snrPacket = NULL, float* rssiPacket = NULL,
>     float* rssiSignalPacket = NULL, uint8_t* detector = NULL);
> ```
>
> **2. `lib/RadioLib/src/modules/LR2021/LR2021_cmds_lora.cpp`** — inside
> `getLoRaPacketStatus`, after the `rssiSignalPacket` block and before
> `return(state)`, extract the detector field the chip already returns:
>
> ```cpp
> // detector(3:0) is in buff[5] bits 5:2 — one-hot: 0001=Main, 0010=Side1, 0100=Side2, 1000=Side3
> if(detector) { *detector = (buff[5] >> 2) & 0x0F; }
> ```
>
> **3. Timing contract (just as important as the patch).** The chip's packet-status
> register reflects only the *most recent* packet. `getLastRxDetector()` must be
> read in the same RX-done window as SNR/RSSI — **before** `readData()` /
> `startReceive()` re-arms the receiver. Reading it afterwards returns a stale
> "main" detector. In this firmware that read happens inside
> `LR2021Interface::addReceiveMetadata()`, before RX is re-armed; preserve that
> ordering (or cache the detector atomically alongside SNR/RSSI per packet) in
> any port.

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

This repository contains third-party components under their own licenses.
See [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES) for full license texts.

| Component | License | Location |
|-----------|---------|----------|
| [Meshtastic firmware](https://github.com/meshtastic/firmware) | GPL-3.0 | `src/` (upstream mesh stack) |
| [Zephyr RTOS](https://github.com/zephyrproject-rtos/zephyr) | Apache-2.0 | Linked via west workspace |
| [Nordic nrfx HAL](https://github.com/zephyrproject-rtos/hal_nordic) | BSD-3-Clause | Linked via west workspace |
| CRACEN BA414EP microcode (Nordic) | [Nordic-5-Clause](THIRD_PARTY_LICENSES) | `src/platform/nrf54l15/microcode_binary.h` |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | `lib/RadioLib/` |
| [nanopb](https://github.com/nanopb/nanopb) | zlib | `zephyr/nanopb/` |
| [Arduino Crypto](https://github.com/rweather/arduinolibs) | MIT | `lib/Crypto/` (RNG shim only) |
| [ThingPulse OLED fonts](https://github.com/ThingPulse/esp8266-oled-ssd1306) | MIT | `src/platform/nrf54l15/OLEDDisplayFonts.h` |

**Note:** The CRACEN microcode (`microcode_binary.h`) is licensed under Nordic
Semiconductor's 5-Clause license, which restricts use to Nordic integrated
circuits and prohibits reverse engineering of binary forms. See
[THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES) for the full license text.
