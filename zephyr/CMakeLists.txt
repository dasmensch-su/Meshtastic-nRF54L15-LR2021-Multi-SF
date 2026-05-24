cmake_minimum_required(VERSION 3.20.0)

# Phase 2: LR2021 radio init via direct SPI22 (matches Renode xiao_nrf54l15.repl).
# The semtech_wio_lr2021 shield uses xiao_spi/spi00 — incompatible with Renode.
# spi22 + lr2021 DTS node is defined in the board overlay instead.

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshtastic_nrf54l15 VERSION 0.1.0)

# APP_VERSION is required by src/configuration.h.
# Format: major * 1000000 + minor * 1000 + patch  (e.g. 2.5.0 → 2005000)
add_compile_definitions(
    APP_VERSION=2007015
    APP_VERSION_STRING="2.7.15.nrf54l15"
    HW_VERSION="nRF54L15-XIAO"
    HW_VENDOR=meshtastic_HardwareModel_PRIVATE_HW
    # Platform identifier — enables ARCH_NRF54L15-guarded code in mesh_zephyr.cpp,
    # Arduino.h, SPI.h, main.h, DebugConfiguration.h, etc.
    ARCH_NRF54L15=1
    # Enable LR2021 radio — enables USE_LR2021-guarded code in LR2021Interface.cpp
    USE_LR2021=1
    # Multi-SF master feature switch. See conditional block below target_compile_definitions.
    # Optional: per-board side-SF override. Build with
    #   west build ... -- -DMULTI_SF_SIDE_LIST=8
    # to restrict side detectors to SF8 only. Default is SF8,SF9,SF11.
    $<$<BOOL:${MULTI_SF_SIDE_LIST}>:MULTI_SF_SIDE_LIST=${MULTI_SF_SIDE_LIST}>
    # Enable SSD1306 OLED display — selects SSD1306Wire in Screen.h
    USE_SSD1306=1
    # Disable message persistence — no filesystem on nRF54L15
    ENABLE_MESSAGE_PERSISTENCE=0
    # Exclude subsystems that have no Zephyr implementation yet
    MESHTASTIC_EXCLUDE_MQTT=1
    MESHTASTIC_EXCLUDE_I2C=1
    # Exclude modules that need significant porting work or are unused
    MESHTASTIC_EXCLUDE_REMOTEHARDWARE=1
    MESHTASTIC_EXCLUDE_POWER_TELEMETRY=1
    MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR=1
    MESHTASTIC_EXCLUDE_HEALTH_TELEMETRY=1
    MESHTASTIC_EXCLUDE_DROPZONE=1
    MESHTASTIC_EXCLUDE_GENERIC_THREAD_MODULE=1
    MESHTASTIC_EXCLUDE_POWERSTRESS=1
)

# Multi-SF master feature switch. Defaults to ON; override with
#   west build ... -- -DMESHTASTIC_MULTI_SF_BRIDGE=0
# to compile out ALL Multi-SF code (side detectors, tracker, TX retarget,
# NodeInfo SF rotation, MultiSFBridge). The port then behaves as a stock
# single-SF Meshtastic node on its main SF. Useful for A/B testing,
# regression checks, or deployments that don't need cross-SF bridging.
if(NOT DEFINED MESHTASTIC_MULTI_SF_BRIDGE)
    set(MESHTASTIC_MULTI_SF_BRIDGE 1)
endif()
target_compile_definitions(app PRIVATE MESHTASTIC_MULTI_SF_BRIDGE=${MESHTASTIC_MULTI_SF_BRIDGE})

# Multi-SF cross-SF broadcast flood relay. Opt-in (default OFF) because it
# amplifies broadcast airtime by N-1 per enabled side SF. When enabled, the
# portnum-aware allowlist (in MultiSFBridge.cpp) limits fan-out to low-rate
# high-value types (NodeInfo, Routing, Admin, Traceroute, KeyVerification).
# Override: west build ... -- -DMULTI_SF_BROADCAST_FANOUT=1
if(MULTI_SF_BROADCAST_FANOUT)
    target_compile_definitions(app PRIVATE MULTI_SF_BROADCAST_FANOUT=${MULTI_SF_BROADCAST_FANOUT})
endif()

# Multi-SF DM fan-out for unknown destinations. Default ON. When a bridge
# relays a unicast DM and doesn't know the destination's SF, it fans the
# DM out on all active SFs so at least one copy reaches the right flood
# domain. Costs up to N-1 extra TXs per unknown-dest DM but is necessary
# for usable cross-SF unicast delivery.
# Override: west build ... -- -DMULTI_SF_DM_FANOUT=0
if(NOT DEFINED MULTI_SF_DM_FANOUT)
    set(MULTI_SF_DM_FANOUT 1)
endif()
if(MULTI_SF_DM_FANOUT)
    target_compile_definitions(app PRIVATE MULTI_SF_DM_FANOUT=1)
endif()

# Home SF: the SF this bridge "lives on" for originated traffic. Default is
# main SF (0 = no override). Relay-on-received-SF is always active.
# Override: west build ... -- -DMULTI_SF_HOME_SF=8
if(MULTI_SF_HOME_SF)
    target_compile_definitions(app PRIVATE MULTI_SF_HOME_SF_DEFAULT=${MULTI_SF_HOME_SF})
endif()

target_sources(app PRIVATE
    # Platform layer
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/main-nrf54l15.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/lr2021.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/fault_handler.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/bt_rand_stub.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/memGet.cpp
    # Mesh stack stubs (replaces RadioInterface.cpp, MeshPacketQueue.cpp, NodeDB.cpp)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/mesh_zephyr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/MeshPacketQueue_zephyr.cpp
    # AirTime (Gap 1)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/airtime.cpp
    # PacketHistory + Throttle (Gap 2/5)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/PacketHistory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/Throttle.cpp
    # CryptoEngine + Channels (Gap 3/4)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/CryptoEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/Channels.cpp
    # Real NodeDB (replaces stubs in mesh_zephyr.cpp)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/NodeDB.cpp
    # meshUtils (memfll, isOneOf, vformat, printBytes)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/meshUtils.cpp
    # Arduino-compat Print (just printf; rest inlines from Print.h)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/Print.cpp
    # SerialConsole + RedirectablePrint  wire-protocol over USB-CDC
    # for `meshtastic --port`. Backed by the IRQ-driven UART RX ring
    # buffer in mesh_zephyr.cpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SerialConsole.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RedirectablePrint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/StreamAPI.cpp
    # TransmitHistory (used by PositionModule, NodeInfoModule)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/TransmitHistory.cpp
    # Concurrency: Lock/LockGuard (needed by CryptoEngine)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/concurrency/Lock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/concurrency/LockGuard.cpp
    # Router chain (Gap 6-8)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/Router.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/FloodingRouter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/ReliableRouter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/NextHopRouter.cpp
    # MeshModule framework + Default utilities (Gap 10)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/MeshModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/Default.cpp
    # MeshService + utilities (Gap 11)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/MeshService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/TypeConversions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/DisplayFormatters.cpp
    # Modules (Gap 12-14)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/Modules.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/RoutingModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/NodeInfoModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/TextMessageModule.cpp
    # PositionModule (Gap 15) + AdminModule (Gap 16)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/PositionModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/AdminModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gps/GeoCoord.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/gap15_16_stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/aes_impl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/display_status.cpp
    # Device telemetry (uptime, channel utilization, air util)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/Telemetry/DeviceTelemetry.cpp
    # PowerFSM — real power state machine
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/PowerFSM.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/StatusLEDModule.cpp
    # ---- Full Meshtastic Graphics UI (HAS_SCREEN=1) ----
    # OLEDDisplay / OLEDDisplayUi / SSD1306Wire — Zephyr-native implementations
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/OLEDDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/OLEDDisplayUi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/SSD1306Wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/OLEDDisplayFonts.cpp
    # Screen core
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/Screen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/SharedUIDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/EmoteRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/emotes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/VirtualKeyboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/TimeFormatters.cpp
    # Draw renderers
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/draw/UIRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/draw/ClockRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/draw/CompassRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/draw/DebugRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/draw/NodeListRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/draw/NotificationRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/draw/MessageRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics/draw/MenuHandler.cpp
    # MessageStore (screen-dependent)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/MessageStore.cpp
    # Modules needed by Screen
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/WaypointModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/OnScreenKeyboardModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/CannedMessageModule.cpp
    # Input subsystem (Screen.cpp references InputBroker, ButtonThread handles user button)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/input/InputBroker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/input/ButtonThread.cpp
    # Rotary encoder (compiled but init() returns false if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/input/RotaryEncoderInterruptBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/input/RotaryEncoderInterruptImpl1.cpp
    # SystemCommands + buzzer (created by Modules.cpp when HAS_BUTTON=1)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/SystemCommandsModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/buzz/BuzzerFeedbackThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/buzz/buzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/input/TouchScreenBase.cpp
    # GpioLogic (included by TFTDisplay.h header)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/GpioLogic.cpp
)

# CRACEN hardware crypto acceleration (hardware builds only)
# AES-ECB/CTR, X25519 ECDH, SHA-256 — all via CRACEN hardware
if(CONFIG_NRF54L15_HARDWARE_CRYPTO)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/NRF54L15CryptoEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/cracen_ed25519.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/cracen_cm.c
    )
endif()

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/ble_phone_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/ZephyrFS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SafeFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FSCommon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/xmodem.cpp
    # PhoneAPI (BLE phone connection)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/PhoneAPI.cpp
    # Tier 4 modules
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/TraceRouteModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/NeighborInfoModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/DetectionSensorModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/StoreForwardModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/RangeTestModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/ExternalNotificationModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/SerialModule.cpp
    # ATAK plugin module + text compression
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/AtakPluginModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/compression/unishox2.cpp
    # ReplyBot + Status modules
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/ReplyBotModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/StatusMessageModule.cpp
    # Protobuf helpers + generated descriptors (Gap 9 dependency)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/mesh-pb-constants.cpp
)

# Link all generated protobuf .cpp files
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/generated/meshtastic/*.cpp)
target_sources(app PRIVATE ${PROTO_SRCS}
    # Radio interface (RadioLib-based)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/LR2021Interface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/RadioLibInterface.cpp
    # Multi-SF per-peer SF tracker
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/NodeSFTracker.cpp
    # Multi-SF NodeInfo round-robin beaconer
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/MultiSFBeaconer.cpp
    # Multi-SF cross-SF flood relay (opt-in via MESHTASTIC_MULTI_SF_BRIDGE)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/MultiSFBridge.cpp
    # Multi-SF shadow-channel decryption fallback (MESHTASTIC_MULTI_SF_BRIDGE only;
    # compiles to ~0 bytes when the flag is off).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/ShadowChannels.cpp
    # RadioLib library + Zephyr ArduinoHal shim
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15/hal/Arduino/ArduinoHal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/Hal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/Module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/protocols/PhysicalLayer/PhysicalLayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR11x0/LR_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_chip_control.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_radio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_lora.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_misc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_gfsk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_flrc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_ook.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_oqpsk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src/modules/LR2021/LR2021_cmds_ranging.cpp
    # Concurrency layer
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/concurrency/BinarySemaphoreZephyr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/concurrency/OSThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/concurrency/NotifiedWorkerThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/concurrency/InterruptableDelay.cpp
    # nanopb runtime (needed for meshtastic protobuf types)
    ${CMAKE_CURRENT_SOURCE_DIR}/nanopb/pb_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/nanopb/pb_encode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/nanopb/pb_decode.c
    # PKI: key verification module (SHA256 via CRACEN shim in platform dir)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/KeyVerificationModule.cpp
)

# IMPORTANT: src/platform/nrf54l15 must come FIRST so our Arduino.h, SPI.h,
# and main.h stubs shadow the non-existent Arduino toolchain headers when
# files use #include <Arduino.h> or #include "main.h".
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/nrf54l15
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mesh/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gps
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/graphics
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/input
    ${CMAKE_CURRENT_SOURCE_DIR}/../variants/nrf54l15/xiao_nrf54l15
    ${CMAKE_CURRENT_SOURCE_DIR}/nanopb
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/RadioLib/src
)

