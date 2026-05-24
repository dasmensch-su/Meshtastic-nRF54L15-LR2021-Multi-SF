# Hardware Build & Flash Guide

Build and flash Meshtastic firmware on the LR2021 LoRa Evaluation Kit V2
(XIAO nRF54L15 + Wio-LR2021 LoRa Plus Expansion Board).

## Kit Overview

The LR2021 Evaluation Kit V2 consists of:
- **XIAO nRF54L15** — main MCU board (nRF54L15 SoC + SAMD11 interface MCU)
- **LoRa Plus Expansion Board** — carries the Semtech LR2021 LoRa transceiver

The XIAO plugs into the expansion board via its XIAO connector. The LR2021
communicates with the nRF54L15 over SPI.

### Built-in Debug Probe

The XIAO nRF54L15 has a **secondary SAMD11 microcontroller** on-board that
provides a **CMSIS-DAPv2** interface over USB-C. This gives you:

1. **SWD debug/flash** — flash firmware via pyOCD (no external J-Link needed)
2. **USB CDC serial port** — UART console appears as `/dev/ttyACM0` on Linux

Everything goes through the single USB-C cable: power, flashing, and serial console.

## Prerequisites

- Zephyr SDK 0.17.0+ installed with `west` available
- pyOCD installed (`pip install pyocd`)
- USB-C data cable connected to the XIAO nRF54L15

No external debug probe is required.

## Pin Mapping (Wio-LR2021 on XIAO connector)

| Signal | XIAO Pin | nRF54L15 GPIO | Notes |
|--------|----------|---------------|-------|
| SPI SCK | D8 | P2.1 | spi00 |
| SPI MOSI | D10 | P2.2 | spi00 |
| SPI MISO | D9 | P2.4 | spi00 |
| CS | D3 | P1.7 | Active low |
| IRQ (DIO8) | D0 | P1.4 | Active high |
| BUSY | D1 | P1.5 | Active high |
| RESET | D2 | P1.6 | Active low |

## Build

```bash
west build -s zephyr -b xiao_nrf54l15/nrf54l15/cpuapp -d zephyr/build_hardware -- \
  -DOVERLAY_CONFIG=prj_hardware.conf \
  -DDTC_OVERLAY_FILE=boards/xiao_nrf54l15_nrf54l15_cpuapp_hardware.overlay \
  -DCONFIG_MESHTASTIC_FULL_STACK=y
```

Output: `zephyr/build_hardware/zephyr/zephyr.hex` (~505 KB flash, ~141 KB RAM)

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_MESHTASTIC_FULL_STACK` | must set `=y` | Enables full mesh stack (NodeDB, Router, BLE, display) |
| `MESHTASTIC_MULTI_SF_BRIDGE` | `1` (on) | Multi-SF bridge code; set `=0` to compile out |
| `MULTI_SF_BROADCAST_FANOUT` | off | Enable cross-SF broadcast relay (increases airtime) |
| `MULTI_SF_SIDE_LIST` | `8,9,11` | Override side detector SF list |
| `CONFIG_MESHTASTIC_DEBUG_LOGGING` | `y` | SEGGER RTT debug output; set `=n` for production |

### Incremental Rebuild

After modifying source files, just re-run without `-p auto`:

```bash
west build -d zephyr/build_hardware
```

## Flash

### Via pyOCD + built-in CMSIS-DAP

```bash
# Flash (supports multiple boards via unique ID)
pyocd flash -t nrf54l -u <BOARD_UID> zephyr/build_hardware/zephyr/zephyr.hex

# Reset without reflashing
pyocd reset -t nrf54l -u <BOARD_UID>
```

Find your board's unique ID:
```bash
pyocd list
```

The serial port appears at `/dev/serial/by-id/usb-Seeed_Studio_Seeed_Studio_XIAO_nrf54_CMSIS-DAP_<UID>-if02`.

### Multiple Boards

When multiple XIAOs are connected to the same USB hub, use the `-u` flag with
the board's unique ID (visible in `pyocd list` output and in the `/dev/serial/by-id/` path).

## Serial Console

The on-board SAMD11 bridges UART to USB CDC ACM. When connected via USB-C,
a serial port appears automatically.

```bash
# Meshtastic CLI (recommended — protobuf API)
meshtastic --port /dev/serial/by-id/usb-Seeed_Studio_..._<UID>-if02 --info

# Raw serial capture
cat /dev/serial/by-id/usb-Seeed_Studio_..._<UID>-if02
```

### SEGGER RTT (Debug Logging)

When `CONFIG_MESHTASTIC_DEBUG_LOGGING=y`, all LOG_INFO/WARN/ERROR output goes
through SEGGER RTT. Read it via pyOCD:

```bash
pyocd rtt -t nrf54l -u <BOARD_UID>
```

RTT output is separate from the Meshtastic serial CLI — the serial port carries
the protobuf wire protocol for the phone app and `meshtastic` CLI tool.

## Differences from Simulation Build

| Setting | Simulation (prj.conf) | Hardware (prj_hardware.conf) |
|---------|----------------------|------------------------------|
| SPI bus | spi22 (Port 1 pins) | spi00 (Port 2 pins) |
| CS pin | P2.5 | P1.7 (D3) |
| IRQ DIO | DIO5 | DIO8 |
| BUSY/RESET | Not present | P1.5 / P1.6 |
| CRACEN entropy | Disabled | Enabled |
| AES | Software (aes_impl.c) | CRACEN hardware |
| BLE | Disabled | Zephyr BT host + controller |
| Display | None | SSD1306 128x64 OLED |
| Persistence | None | LittleFS (36KB storage_partition) |
| Debug logging | Disabled | SEGGER RTT |

## Troubleshooting

### No serial output over USB-C
- Verify USB-C cable supports data (not charge-only)
- Check `ls /dev/serial/by-id/` — the SAMD11 CDC port should appear
- Verify firmware has `CONFIG_SERIAL=y` and `CONFIG_UART_CONSOLE=y`
- Try unplugging and replugging USB-C

### pyocd flash fails
- Ensure pyOCD is installed: `pip install pyocd`
- Check USB permissions: `sudo usermod -aG dialout $USER` (then re-login)
- Verify SAMD11 is detected: `pyocd list`
- Target name is `nrf54l` (not `nrf54l15`)

### SPI errors / Radio not responding
- Ensure the XIAO is fully seated on the LoRa Plus expansion board
- Look for "SPI not ready" or "BUSY timeout" in serial/RTT logs
- Verify the expansion board has power (check 3.3V rail)

### Board unresponsive after crash
- With `CONFIG_MESHTASTIC_DEBUG_LOGGING=y`, the firmware auto-reboots on fatal
  errors and prints crash diagnostics (PC, LR, thread) on next boot
- Without debug logging, Zephyr's default fatal handler spins forever — the
  board will appear completely dead until power-cycled
- To recover: `pyocd reset -t nrf54l -u <UID>` or unplug/replug USB

### Factory reset (erase all flash)
```bash
pyocd erase -t nrf54l -u <BOARD_UID> --chip
```
This erases firmware + LittleFS (config, NodeDB, PKI keys). Reflash after erasing.
