# Meshtastic nRF54L15 Port

Meshtastic mesh networking firmware ported to the Nordic nRF54L15 SoC
running Zephyr RTOS, targeting the Seeed Studio LR2021 LoRa Evaluation
Kit V2.

## Hardware

- **MCU:** XIAO nRF54L15 (nRF54L15 SoC, ARM Cortex-M33, 256KB RAM, 1.5MB Flash)
- **Radio:** Wio-LR2021 LoRa Plus Expansion Board (Semtech LR2021, sub-GHz + 2.4 GHz LoRa)
- **Display:** SSD1306 128x64 OLED via I2C (on LoRa Plus board)
- **Button:** User button on P0.0 (active-low, pull-up)
- **Debug:** Built-in SAMD11 CMSIS-DAPv2 (flash + serial over single USB-C)

No external J-Link or debug probe is required.

## Implemented Features

### Core Mesh Stack
| Feature | Status | Notes |
|---------|--------|-------|
| Flooding router | Done | Full dedup + forwarding |
| Reliable routing | Done | ReliableRouter + NextHopRouter |
| AES-128/256-CTR encryption | Done | CRACEN hardware AES via BA411E DMA engine |
| PKI (X25519 + AES-CCM) | Done | 100% CRACEN hardware: X25519 (BA414EP), SHA-256 (BA413), AES-CCM (AES-ECB blocks); Arduino Crypto library fully removed |
| Channel management | Done | Multi-channel support, URL export/import |
| NodeDB | Done | Full upstream implementation with LittleFS persistence |
| AirTime management | Done | Channel utilization tracking |
| PacketHistory + Throttle | Done | Dedup and rate limiting |

### Radio
| Feature | Status | Notes |
|---------|--------|-------|
| LR2021 LoRa transceiver | Done | RadioLib driver via ArduinoHal Zephyr shim |
| Sub-GHz (915 MHz US) | Done | Interop verified with Heltec V4 and Adafruit Muzi Duo |
| 2.4 GHz LoRa | Done | Dual-board tested (XIAO ↔ XIAO) |
| 18 regulatory regions | Done | US default, configurable via phone app |
| Multi-SF bridge | Done | Simultaneous RX on up to 4 spreading factors (LR2021 parallel detection) |

### Multi-SF Bridge

The LR2021's parallel spreading-factor detection allows one node to act as a
transparent bridge between peers running on different LoRa presets (e.g.,
SHORT_FAST SF7, SHORT_SLOW SF8, MEDIUM_FAST SF9, LONG_FAST SF11).

> **Depends on a one-parameter RadioLib patch.** "Parallel RX, tagged with
> originating SF" below requires exposing the LR2021's per-packet detector mask.
> The LR2021 driver is upstream RadioLib, but upstream's `getLoRaPacketStatus`
> discards that field (true even on current `master`); our vendored copy adds a
> single `detector` out-parameter to expose it. Without the patch
> `getLastRxDetector()` always returns the main SF and the bridge can only TX on
> its main SF — side-SF DMs silently fail. The exact patch, the
> diff-against-`master`-not-the-7.6.0-tag caveat, and the timing contract are
> documented at the top of the [README](../README.md#-required-one-radiolib-patch-for-multi-sf).
> Re-apply it whenever you update or replace RadioLib, or port to another framework.

| Feature | Status | Notes |
|---------|--------|-------|
| Parallel RX (main + 3 side SFs) | Done | LR2021 side detectors, tagged with originating SF |
| Per-peer SF tracker | Done | In-RAM map of NodeNum → observed SF |
| TX retargeting | Done | Unicast to single-SF peer switches modulation for that TX |
| NodeInfo round-robin | Done | Bridge advertises on each SF in rotation |
| Cross-SF flood relay | Done | Broadcasts relayed across all enabled SFs with dedup |
| Shadow channels | Done | Preset-agnostic decryption for cross-preset NodeInfo + PKI key exchange |
| Cross-SF DMs (PKI + channel-key) | Done | PKI DMs relay transparently; channel-key fallback if pubkeys not yet exchanged |
| Broadcast fan-out control | Done | OFF by default; opt-in via `MULTI_SF_BROADCAST_FANOUT=1` |
| Bridged-packet sentinel | Done | `next_hop=0xFF` prevents multi-bridge re-amplification |
| Compile-time gating | Done | `MESHTASTIC_MULTI_SF_BRIDGE=0` compiles out all bridge code |

Resource cost: +9 KB flash, +1.2 KB RAM over baseline.

### Cryptography
| Feature | Status | Notes |
|---------|--------|-------|
| CRACEN hardware AES | Done | ECB + CTR modes via BA411E CryptoMaster DMA |
| CRACEN entropy (TRNG) | Done | Hardware RNG via CONFIG_ENTROPY_NRF_CRACEN_CTR_DRBG |
| X25519 ECDH | Done | CRACEN BA414EP hardware (Arduino Crypto Curve25519 removed) |
| SHA-256 | Done | CRACEN BA413 hardware (Arduino Crypto SHA256 removed) |
| AES-CCM authenticated encryption | Done | CRACEN AES-ECB blocks with CCM framing (Arduino Crypto aes-ccm removed) |

### Display
| Feature | Status | Notes |
|---------|--------|-------|
| Full Meshtastic graphics UI | Done | OLEDDisplay reimplemented for Zephyr |
| Node list screens | Done | Last heard, hop/signal, distance, compass |
| Message display | Done | Text message frames with word wrap |
| Debug/system info | Done | LoRa config, battery, GPS status, memory |
| Clock display | Done | Analog and digital clock frames |
| Compass | Done | Bearing to nodes with compass rose |
| Menu system | Done | Navigation via button press |
| Boot logo | Done | Meshtastic logo on startup |
| Screen timeout | Done | Configurable via phone app (default 10min) |
| Screen wake on message | Done | PowerFSM triggers EVENT_RECEIVED_MSG |
| Screen wake on button | Done | PowerFSM triggers EVENT_PRESS |
| Display blanking | Done | SSD1306 hardware on/off via Zephyr blanking API |

### BLE
| Feature | Status | Notes |
|---------|--------|-------|
| Phone API (GATT service) | Done | Full PhoneAPI with protobuf framing |
| Connectable advertising | Done | Zephyr BT peripheral mode |
| Config read/write | Done | All config sections via BLE |
| Text messaging via BLE | Done | Send/receive through phone app |
| BLE advertising control | Done | PowerFSM manages start/stop |
| BLE stays active during serial | Done | nRF54L15 has independent BLE + serial (unlike ESP32) |
| iOS/Android app support | Done | Tested with Meshtastic iOS app |

### Power Management
| Feature | Status | Notes |
|---------|--------|-------|
| PowerFSM state machine | Done | Real Fsm.h engine with timed transitions |
| Screen timeout (POWER→DARK) | Done | Configurable, default 10 minutes |
| Wake on message | Done | DARK→ON on EVENT_RECEIVED_MSG |
| Wake on button press | Done | DARK→ON on EVENT_PRESS |
| BLE control in sleep | Done | Advertising stays active in DARK state |
| Deep sleep (sys_poweroff) | Done | Ready for battery use, ~1µA system-off |
| setCPUFast / enableModemSleep | N/A | nRF54L15 runs at fixed 128MHz, no modem |

### Modules
| Module | Status | Notes |
|--------|--------|-------|
| TextMessage | Done | Core text messaging |
| NodeInfo | Done | Node metadata exchange |
| Position | Done | Position broadcast (fixed position for testing) |
| Admin | Done | Full admin command support |
| Routing | Done | Route discovery and management |
| TraceRoute | Done | Multi-hop route tracing |
| NeighborInfo | Done | Neighbor discovery |
| StoreForward | Compiled, not instantiated | Source built but gated to `ARCH_PORTDUINO` in `src/modules/Modules.cpp` |
| RangeTest | Excluded (no GPS) | Auto-gated via `HAS_GPS=0` |
| ExternalNotification | Done | Notification control (no buzzer hardware) |
| Serial | Compiled, not instantiated | Built but arch guard in `src/modules/Modules.cpp` omits `ARCH_NRF54L15` |
| ATAK | Done | PLI + GeoChat with unishox2 compression |
| ReplyBot | Done | Auto-reply to /ping with hop/RSSI/SNR |
| Status | Done | Periodic status broadcast |
| Waypoint | Done | Create/delete waypoints |
| DetectionSensor | Done | Generic sensor detection |
| KeyVerification | Done | PKI key verification |
| DeviceTelemetry | Done | Uptime, channel utilization, air util |

### Input
| Feature | Status | Notes |
|---------|--------|-------|
| User button (P0.0) | Done | Single/long/multi-click via Zephyr GPIO |
| ButtonThread | Done | Polls OneButton from cooperative thread |
| InputBroker | Done | Event routing to Screen + PowerFSM |
| Screen frame cycling | Done | Button press cycles display frames |

### Storage
| Feature | Status | Notes |
|---------|--------|-------|
| LittleFS persistence | Done | Config, NodeDB, PKI keys survive reboot |
| SafeFile atomic writes | Done | Write-to-tmp + XOR hash + rename |
| XModem file transfer | Done | Upstream XModem over serial/BLE |
| MAX_NUM_NODES | 80 | Up from 18 (was limited by NVS 4KB sector) |
| Message persistence | Disabled | ENABLE_MESSAGE_PERSISTENCE=0 |

## Interoperability

Verified on 915 MHz US, LONG_FAST preset, default PSK:

| Peer Device | Firmware | Broadcasts | DMs (PKI) | BLE DMs | Persistence |
|-------------|----------|------------|-----------|---------|-------------|
| Heltec WiFi LoRa 32 V4 | Stock 2.7.16 | 100% both ways | Yes | 100% | LittleFS survives reboot |
| Adafruit Muzi Duo | Stock 2.7.15 | 100% both ways | Yes | N/A (serial tested) | N/A |

## Not Implemented (Hardware Dependent)

These features require additional hardware not present on the current board:

| Feature | Blocked By | Notes |
|---------|-----------|-------|
| MQTT gateway | No WiFi/Ethernet | Would need external WiFi module (SPI/UART) |
| GPS | No GNSS module | HAS_GPS=0; position set manually for testing |
| Environmental sensors | No I2C sensors | I2C bus available but no sensors connected |
| Health telemetry | No health sensors | Code compiles if sensors added |
| Power telemetry | No battery/ADC | No BATTERY_PIN defined |
| CannedMessages | No keyboard input | MESHTASTIC_EXCLUDE_CANNEDMESSAGES=1 |
| Remote hardware GPIO | No exposed GPIO | MESHTASTIC_EXCLUDE_REMOTEHARDWARE=1 |
| Battery monitoring | No battery circuit | isPowered() always returns true (USB) |

## Build

### Hardware Build (LR2021 EVK)

```bash
west build -s zephyr -b xiao_nrf54l15/nrf54l15/cpuapp -d zephyr/build_hardware -- \
  -DOVERLAY_CONFIG=prj_hardware.conf \
  -DDTC_OVERLAY_FILE=boards/xiao_nrf54l15_nrf54l15_cpuapp_hardware.overlay \
  -DCONFIG_MESHTASTIC_FULL_STACK=y
```

#### Multi-SF Build Options

Multi-SF bridge is enabled by default. To disable:

```bash
west build ... -- -DMESHTASTIC_MULTI_SF_BRIDGE=0
```

To enable broadcast fan-out across SFs (off by default to save airtime):

```bash
west build ... -- -DMULTI_SF_BROADCAST_FANOUT=1
```

To restrict side detectors to specific SFs (default is SF8,SF9,SF11):

```bash
west build ... -- -DMULTI_SF_SIDE_LIST=8
```

### Simulation Build (Renode)

```bash
west build -b xiao_nrf54l15/nrf54l15/cpuapp -d build zephyr
```

### Build Size

| Build | Flash | RAM |
|-------|-------|-----|
| Hardware (full stack + UI + Multi-SF) | 505 KB (34.6%) | 141 KB (73.5%) |

## Flash

```bash
# Flash board by unique ID (supports multiple boards on same USB hub)
pyocd flash -t nrf54l -u <BOARD_UID> zephyr/build_hardware/zephyr/zephyr.hex

# Reset board without reflashing
pyocd reset -t nrf54l -u <BOARD_UID>
```

See [HARDWARE_BUILD.md](HARDWARE_BUILD.md) for detailed pin mapping and debug setup.

## Architecture

Two build configurations share a common source tree:

| | Simulation | Hardware |
|---|---|---|
| Config | `prj.conf` | `prj_hardware.conf` |
| DTS Overlay | default | `..._hardware.overlay` |
| SPI | spi22 (Port 1 pins) | spi00 (Port 2 pins) |
| Radio | Simulated in Renode | LR2021 via SPI |
| BLE | Disabled | Zephyr BT host + controller |
| Display | None | SSD1306 128x64 OLED |
| Crypto | Software fallback | 100% CRACEN hardware (lib/Crypto removed) |
| Entropy | Disabled (Renode) | CRACEN TRNG |
| Persistence | None | LittleFS on storage_partition (36KB) |

### Key Source Files

| File | Purpose |
|------|---------|
| `src/platform/nrf54l15/main-nrf54l15.cpp` | Entry point, main loop, PowerFSM init |
| `src/platform/nrf54l15/NRF54L15CryptoEngine.cpp` | 100% CRACEN hardware crypto (AES, X25519, SHA-256, AES-CCM) |
| `src/platform/nrf54l15/ZephyrFS.h` / `.cpp` | Arduino File/FS wrapper around Zephyr LittleFS |
| `src/platform/nrf54l15/FSCommon.h` | Defines FSCom=zephyrFS, FSBegin() |
| `src/platform/nrf54l15/OLEDDisplay.cpp` | Software framebuffer renderer |
| `src/platform/nrf54l15/SSD1306Wire.cpp` | Zephyr display_write() bridge |
| `src/platform/nrf54l15/OLEDDisplayUi.cpp` | Frame/overlay manager |
| `src/platform/nrf54l15/Fsm.h` | Finite state machine engine |
| `src/platform/nrf54l15/OneButton.h` | Zephyr GPIO button handler |
| `src/platform/nrf54l15/ble_phone_api.cpp` | BLE GATT phone API |
| `src/platform/nrf54l15/gap15_16_stubs.cpp` | Platform stubs + sleep/BLE control |
| `src/platform/nrf54l15/mesh_zephyr.cpp` | Radio interface + platform init |
| `src/platform/nrf54l15/fault_handler.c` | Crash diagnostics (debug builds only) |
| `src/mesh/MultiSFBridge.cpp` | Cross-SF flood relay engine |
| `src/mesh/MultiSFBeaconer.cpp` | NodeInfo round-robin across SFs |
| `src/mesh/NodeSFTracker.cpp` | Per-peer spreading factor tracker |
| `src/mesh/ShadowChannels.cpp` | Preset-agnostic decryption for cross-SF bridge |
| `lib/RadioLib/src/modules/LR2021/` | LR2021 LoRa driver |
| `zephyr/CMakeLists.txt` | Master build file |
| `variants/nrf54l15/xiao_nrf54l15/variant.h` | Pin definitions |

## Third-Party Components & Licenses

### Firmware Dependencies

| Component | Author/Org | License | Usage |
|-----------|-----------|---------|-------|
| [Meshtastic Firmware](https://github.com/meshtastic/firmware) | Meshtastic Project | GPL-3.0 | Upstream mesh stack |
| [RadioLib](https://github.com/jgromes/RadioLib) | Jan Gromes | MIT | LoRa radio abstraction + LR2021 driver |
| [Zephyr RTOS](https://github.com/zephyrproject-rtos/zephyr) | Zephyr Project | Apache-2.0 | OS, BLE, SPI, I2C, LittleFS, display drivers |
| [Nordic HAL](https://github.com/nrfconnect/sdk-nrf) | Nordic Semiconductor | BSD-3-Clause | nRF54L15 hardware abstraction |
| [nanopb](https://github.com/nanopb/nanopb) | Petteri Aimonen | zlib | Protobuf encoding/decoding |
| [Meshtastic Protobufs](https://github.com/meshtastic/protobufs) | Meshtastic Project | GPL-3.0 | Protocol definitions |
| [ThingPulse OLEDDisplay](https://github.com/meshtastic/esp8266-oled-ssd1306) | ThingPulse / Meshtastic | MIT | Font data (ArialMT_Plain_10/16/24) |
| ~~[rweather/Crypto](https://github.com/rweather/arduinolibs)~~ | ~~Rhys Weatherley~~ | ~~MIT~~ | Removed — replaced by CRACEN hardware crypto; headers shimmed in platform dir |

### License

This port is licensed under **GPL-3.0**, consistent with the upstream Meshtastic
firmware. Third-party components retain their original licenses as listed above.
