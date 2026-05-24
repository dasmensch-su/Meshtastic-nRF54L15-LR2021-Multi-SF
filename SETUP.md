# meshtastic-nrf54l15 — Setup & Build

This is a port of Meshtastic firmware to the **Seeed XIAO nRF54L15 + Wio-LR2021
LoRa Plus expansion board**, built with Zephyr (NCS-style west workspace).

**Why Zephyr?** The nRF54L15 has no Arduino core or PlatformIO support — Nordic
only provides Zephyr via the nRF Connect SDK. This project provides an Arduino
shim layer so upstream Meshtastic `.cpp` files compile unmodified against Zephyr.

The firmware includes a **Multi-SF** bridge mode that listens on up to 4
spreading factors concurrently using the LR2021's parallel detection hardware.

## 1. Host prerequisites

Tested on Fedora 40 / x86_64. Equivalents exist for Debian/Ubuntu/Arch.

```bash
# Build tools, Python, libusb (for OpenOCD/CMSIS-DAP), device-tree compiler
sudo dnf install -y \
    git cmake ninja-build gperf ccache dfu-util wget xz \
    file python3 python3-pip python3-tkinter \
    make gcc gcc-c++ glibc-devel.i686 libstdc++-devel.i686 \
    SDL2-devel libusb1-devel openocd dtc
# Debian/Ubuntu equivalent:
# sudo apt install -y git cmake ninja-build gperf ccache dfu-util wget xz-utils \
#     file python3 python3-pip python3-tk make gcc g++ libsdl2-dev libusb-1.0-0-dev \
#     openocd device-tree-compiler

# CMSIS-DAP udev rules (lets the XIAO's onboard SAMD11 probe enumerate without sudo)
sudo cp meshtastic-nrf54l15/zephyr/99-xiao-nrf54l15.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
sudo usermod -aG dialout,plugdev "$USER"   # log out/in afterwards
```

## 2. Python: `west` + Meshtastic CLI

```bash
python3 -m pip install --user --upgrade west
python3 -m pip install --user meshtastic pyocd
# Make sure ~/.local/bin is on PATH:
export PATH="$HOME/.local/bin:$PATH"
```

## 3. Zephyr SDK 0.17.0 (toolchain)

```bash
cd ~
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.0/zephyr-sdk-0.17.0_linux-x86_64.tar.xz
wget -O - https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.0/sha256.sum | shasum --check --ignore-missing
tar xvf zephyr-sdk-0.17.0_linux-x86_64.tar.xz
cd zephyr-sdk-0.17.0
./setup.sh -t arm-zephyr-eabi -h -c    # toolchain + host tools + cmake pkg
```

## 4. Zephyr workspace (west init)

The application uses a west workspace rooted at `~/` that pulls in Zephyr
v4.2.0 plus the Semtech `usp` LR2021 driver module. The `meshtastic-nrf54l15`
project lives alongside (not inside) the Zephyr tree:

```bash
cd ~
west init -m https://github.com/<your-fork>/usp_zephyr.git --mr main .
# OR if you already have a local copy of the manifest repo:
# cp -r ~/usp_zephyr ~/usp_zephyr   # (if not already at ~/)
# cd ~ && west init -l usp_zephyr

west update         # ~5 min, clones Zephyr, modules, hal_nordic, usp...
west zephyr-export  # registers the Zephyr CMake package
python3 -m pip install --user -r zephyr/scripts/requirements.txt
```

After `west update`, your home directory should look like:

```
~/
├── meshtastic-nrf54l15/   ← this project
├── usp_zephyr/            ← west manifest repo
├── zephyr/                ← Zephyr kernel + modules
├── zephyr-sdk-0.17.0/     ← toolchain (Section 3)
└── .west/config           ← west workspace root marker
```

The minimum manifest content (if you need to recreate `usp_zephyr/west.yml`):

```yaml
manifest:
  projects:
    - name: zephyr
      url: https://github.com/zephyrproject-rtos/zephyr
      revision: v4.2.0
      import: true
    - name: usp
      url: https://github.com/Lora-net/usp.git
      revision: main
      path: modules/lib/usp
      submodules: true
  self:
    path: application
```

## 5. Unpack this project

```bash
cd ~
tar xzf meshtastic-nrf54l15.tar.gz       # creates ./meshtastic-nrf54l15
```

The git submodules (`protobufs`, `meshtastic`) are **not** required for the
firmware build — generated nanopb sources are committed under
`src/mesh/generated/`. Skip them unless you want to regenerate protos.

## 6. Environment

```bash
export ZEPHYR_BASE=$HOME/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-0.17.0
```

> **Tip:** If `west` is on your `PATH` and you're inside the workspace, `west
> build` finds `ZEPHYR_BASE` automatically from `.west/config`. The exports
> above are only needed if you invoke CMake directly or work outside the
> workspace tree.

## 7. Build the hardware firmware

```bash
cd ~/meshtastic-nrf54l15
west build -p always -b xiao_nrf54l15/nrf54l15/cpuapp zephyr -- \
    -DCONFIG_MESHTASTIC_FULL_STACK=y \
    -DOVERLAY_CONFIG=prj_hardware.conf \
    -DDTC_OVERLAY_FILE=boards/xiao_nrf54l15_nrf54l15_cpuapp_hardware.overlay
```

Output: `build/zephyr/zephyr.hex` (~532 KB flash, ~54% RAM).

> **Important:** `-DCONFIG_MESHTASTIC_FULL_STACK=y` is required. Without it
> the build links a stripped LoRa-only image with no mesh stack.

### Multi-SF bridge profiles

The Multi-SF system is built into the standard firmware. By default the bridge
is disabled (single-SF mode). Enable it with build-time defines:

```bash
# Default profile — single-SF, fan-out OFF (safe drop-in replacement)
west build -p always -b xiao_nrf54l15/nrf54l15/cpuapp zephyr -- \
    -DCONFIG_MESHTASTIC_FULL_STACK=y \
    -DOVERLAY_CONFIG=prj_hardware.conf \
    -DDTC_OVERLAY_FILE=boards/xiao_nrf54l15_nrf54l15_cpuapp_hardware.overlay

# Bridge profile — listens on 4 SFs, broadcast fan-out ON
west build -p always -b xiao_nrf54l15/nrf54l15/cpuapp zephyr -- \
    -DCONFIG_MESHTASTIC_FULL_STACK=y \
    -DOVERLAY_CONFIG=prj_hardware.conf \
    -DDTC_OVERLAY_FILE=boards/xiao_nrf54l15_nrf54l15_cpuapp_hardware.overlay \
    -DMESHTASTIC_MULTI_SF_BRIDGE=1 \
    -DMULTI_SF_BROADCAST_FANOUT=1
```

## 8. Flash

```bash
# Onboard SAMD11 CMSIS-DAP via USB-C — no external probe needed.
# Flash by unique board ID (required when multiple XIAOs are plugged in):
west flash --erase -r pyocd -i <BOARD_UID>

# Find your board UID:
pyocd list

# Or flash directly with pyocd:
pyocd flash -t nrf54l build/zephyr/zephyr.hex
```

> **Note:** Use `--erase` on first flash or when changing crypto keys /
> LittleFS layout. Without `--erase`, persistent storage (NodeDB, channels,
> PKI keys) is preserved across reflashes. After an erase-flash, boards
> generate new CRACEN keypairs — peers must exchange fresh NodeInfo before
> PKI DMs work again.

If `west flash` cannot find the probe, see
`docs/HARDWARE_BUILD.md` (troubleshooting).

## 9. Serial console

```bash
picocom /dev/ttyACM0 -b 115200 | tee uart_log.txt
```

The SAMD11 bridges UART20 (P1.9 TX / P1.8 RX) to USB-CDC.

## 10. Layout reference

| Path                                   | What                                              |
|----------------------------------------|---------------------------------------------------|
| `src/`                                 | Meshtastic application code (cross-platform)      |
| `src/mesh/generated/`                  | Pre-generated nanopb protobufs (committed)        |
| `lib/RadioLib/`                        | RadioLib LR2021 driver (vendored)                 |
| `zephyr/CMakeLists.txt`                | Top-level Zephyr build entry                      |
| `zephyr/prj.conf`                      | Simulation/native_sim Kconfig                     |
| `zephyr/prj_hardware.conf`             | Hardware Kconfig (CRACEN, UART, BLE, RTT)         |
| `zephyr/boards/*.overlay`              | nRF54L15 device-tree overlays                     |
| `zephyr/dts/bindings/seeed,lr2021.yaml`| LR2021 DT binding                                 |
| `boards/`                              | Inherited PlatformIO board jsons (unused for nRF) |
| `docs/HARDWARE_BUILD.md`               | Detailed hardware build guide                     |
| `docs/NRF54L15_PORT.md`                | Porting log / design notes                        |

## 11. Sanity check

```bash
# Build should finish in 60–90 s on a modern laptop.
# Final link line should report something like:
#   FLASH:  531712 B / 1428 KB  (36%)
#   RAM:     54%
ls -lh build/zephyr/zephyr.hex
```
