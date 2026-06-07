/*
 * Meshtastic — nRF54L15 Zephyr platform entry point
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>

#include "architecture.h"
#include "concurrency/OSThread.h"
#include "variant.h"
#include "lr2021.h"

#if IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK)
#include "concurrency/OSThread.h"
#include "RadioInterface.h"
#include "RadioLibInterface.h"
#include "LR2021Interface.h"
#include "MultiSFBeaconer.h"
#ifdef MESHTASTIC_MULTI_SF_BRIDGE
#include "MultiSFBridge.h"
#endif
#include "NodeDB.h"
#include "NodeSFTracker.h"
#include "TransmitHistory.h"
#include "Router.h"
#include "ReliableRouter.h"
#include "CryptoEngine.h"
extern "C" {
#include "cracen_ed25519.h"
#include "cracen_cm.h"
}
#include "MeshService.h"
#include "Channels.h"
#include "airtime.h"
#include "NodeStatus.h"
#include "PowerFSM.h"
/* initLoRa() declared in RadioInterface.h, defined in mesh_zephyr.cpp */
extern "C" void mesh_zephyr_set_nodenum(uint32_t num);
extern void setupModules();
#endif

#if !MESHTASTIC_EXCLUDE_INPUTBROKER
#include "input/InputBroker.h"
#endif
#include "sleep.h"

#if HAS_SCREEN
#include "detect/ScanI2C.h"
#include "graphics/Screen.h"
extern graphics::Screen *screen;
extern ScanI2C::DeviceAddress screen_found;
#endif
/* OLED status display (hardware builds with CONFIG_DISPLAY) */
extern "C" int display_status_init(void);
/* BLE Phone API (hardware builds with CONFIG_BT_PERIPHERAL) */
extern "C" int ble_phone_api_init(void);
/* Filesystem init (LittleFS) */
void fsInit();
/* Crash diagnostics (fault_handler.c) */
extern "C" void crash_check_and_report(void);
/* Debug helpers from mesh_zephyr.cpp */
extern "C" float RadioInterface_getSavedFreq(void);
extern "C" uint32_t RadioInterface_getSavedChannelNum(void);

extern void setup(void) __attribute__((weak));
extern void loop(void)  __attribute__((weak));

extern "C" void zephyr_serial_rx_init_external(void);
extern void consoleInit();

void setup(void) {}
void loop(void)  {
    concurrency::mainController.run();
    if (service) service->loop();
}

/* LR2021 SPI device from DTS:
 * - Renode:   spi22 @ P1.8/P1.9/P1.10, CS=P2.5, DIO5 (simulation overlay)
 * - Hardware: spi00 @ P2.1/P2.2/P2.4,  CS=P1.7, DIO8 (hardware overlay)  */
#define LR2021_NODE DT_NODELABEL(lr2021)

BUILD_ASSERT(DT_NODE_HAS_STATUS(LR2021_NODE, okay),
             "lr2021 SPI node not enabled in DTS overlay");

static const struct spi_dt_spec lr2021_spi =
    SPI_DT_SPEC_GET(LR2021_NODE,
                    SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8),
                    0);

/* Phase 2: radio init */
static int radio_init(void)
{
    int ret = lr2021_init(&lr2021_spi);
    if (ret) {
        printk("Radio    : SPI not ready (%d)\n", ret);
        return ret;
    }

    uint8_t fw_major = 0, fw_minor = 0;
    ret = lr2021_get_version(&lr2021_spi, &fw_major, &fw_minor);
    if (ret) {
        printk("Radio    : GetVersion failed (%d)\n", ret);
        return ret;
    }
    printk("Radio    : GetVersion fw=%02X.%02X\n", fw_major, fw_minor);

    ret = lr2021_set_standby(&lr2021_spi, LR2021_STDBY_RC);
    if (ret) {
        printk("Radio    : SetStandby failed (%d)\n", ret);
        return ret;
    }
    printk("Radio    : SetStandby OK\n");

    ret = lr2021_set_rf_frequency(&lr2021_spi, 915000000UL);
    if (ret) {
        printk("Radio    : SetFrequency failed (%d)\n", ret);
        return ret;
    }
    printk("Radio    : SetFrequency 915 MHz OK\n");

    return 0;
}

/* --------------------------------------------------------- */

int main(void)
{
    crash_check_and_report();

    printk("\n");
    printk("                         Meshtastic\n");
    printk("                   https://meshtastic.org\n");
    printk("\n");
    printk("Platform : nRF54L15 (Zephyr RTOS)\n");
    printk("Board    : XIAO nRF54L15 + Wio-LR2021\n");
    printk("Radio    : LR2021 via LR1121 driver\n");
    printk("\n");

    int err = radio_init();
    if (err) {
        printk("=== Meshtastic boot FAILED (radio err=%d) ===\n", err);
        return err;
    }

#if IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK)
    /* Full Meshtastic stack: cooperative OSThread scheduler + LR2021Interface */

    /* Initialise the cooperative scheduler */
    concurrency::OSThread::setup();
    printk("Mesh     : OSThread scheduler ready\n");

    /* IRQ-driven UART RX → ring buffer → Serial.available()/read(). */
    zephyr_serial_rx_init_external();

    /* SerialConsole gates incoming protobufs on
     *   config.has_lora && config.security.serial_enabled
     * so without this flag set, the wake byte from `meshtastic --port`
     * lands at our IRQ, gets read by SerialConsole, and is silently
     * dropped before any reply is generated. NodeDB's installDefault
     * sets it true, but only on a clean defaults-install path; force
     * it on here too so this works on a stale flash state. */
    config.has_lora = true;
    config.has_security = true;
    config.security.serial_enabled = true;

    /* Wire-protocol console over USB-CDC.
     *
     * SerialConsole inherits StreamAPI + RedirectablePrint + OSThread:
     *   - StreamAPI consumes ToRadio frames from Serial.read()
     *   - It pumps FromRadio frames back via Serial.write() → printk
     *   - As an OSThread it joins the scheduler started above so it
     *     gets ticked from concurrency::mainController.run()
     *
     * Upstream main.cpp::setup() does this inline; on this port we run
     * our own setup, so we instantiate it explicitly here. */
    consoleInit();
    printk("Mesh     : SerialConsole ready (StreamAPI over USB-CDC)\n");

    /* Gap 4: Create CryptoEngine */
#ifndef HAS_CUSTOM_CRYPTO_ENGINE
    crypto = new CryptoEngine();
#endif
#ifdef HAS_CUSTOM_CRYPTO_ENGINE
    cracen_cm_init();
    if (cracen_ed25519_init() == 0) {
        printk("Mesh     : CryptoEngine created (CRACEN: AES+X25519+SHA256 hardware)\n");
    } else {
        printk("Mesh     : CryptoEngine created (CRACEN: AES hardware, X25519 init FAILED)\n");
    }
#else
    printk("Mesh     : CryptoEngine created (software)\n");
#endif

    /* Gap 3: Initialize channels with defaults + compute hashes */
    channels.initDefaults();
#if IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK) && CONFIG_MESHTASTIC_CHANNEL_INDEX != 0
    /* Channel separation: change the primary channel PSK so this node
     * cannot decrypt packets from nodes using the default key. */
    {
        auto &ch0 = channelFile.channels[0];
        ch0.has_settings = true;
        ch0.settings.psk.size = 16;
        /* Use a distinct key (all 0x42) that differs from the default "AQ==" */
        memset(ch0.settings.psk.bytes, 0x42, 16);
        snprintf(ch0.settings.name, sizeof(ch0.settings.name), "alt%u",
                 (unsigned)CONFIG_MESHTASTIC_CHANNEL_INDEX);
    }
#endif
    channels.onConfigChanged();
    printk("Mesh     : Channels initialized (hash=%d)\n", channels.getHash(0));

    /* Initialize LittleFS BEFORE NodeDB — loadFromDisk needs it */
    fsInit();

    /* NodeDB — real upstream implementation, loads config from LittleFS */
    nodeDB = new NodeDB();
    printk("Mesh     : NodeDB created (node=%08X)\n", nodeDB->getNodeNum());

    /* Load persisted sidecar data that lives outside NodeDB */
    TransmitHistory::getInstance()->loadFromDisk();
    nodeSFTracker.loadFromDisk();

    /* Set default region only if not already configured (e.g., from flash) */
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
        initRegion();
        printk("Mesh     : region defaulted to US (915 MHz)\n");
#if !(MESHTASTIC_EXCLUDE_PKI)
        /* Region was UNSET during NodeDB init, so key generation was skipped.
         * Now that region is set, generate Curve25519 key pair. */
        if (config.security.private_key.size != 32) {
            printk("Mesh     : generating Curve25519 key pair (CRACEN X25519)...\n");
            crypto->generateKeyPair(config.security.public_key.bytes,
                                    config.security.private_key.bytes);
            config.security.public_key.size = 32;
            config.security.private_key.size = 32;
            owner.public_key.size = 32;
            memcpy(owner.public_key.bytes, config.security.public_key.bytes, 32);
            nodeDB->saveToDisk();
            printk("Mesh     : PKI keys generated and saved\n");
        }
#endif
    } else {
        printk("Mesh     : region loaded from flash: %d\n", config.lora.region);
    }

    /* Gap 1: Create AirTime instance (duty cycle tracking, runs as OSThread) */
    airTime = new AirTime();
    printk("Mesh     : AirTime created\n");

    /* Gap 11: Create MeshService (needed for sendToMesh/sendToPhone) */
    service = new MeshService();
    service->init();
    printk("Mesh     : MeshService created + init\n");

    /* Gap 6-9: Create real Router (ReliableRouter → FloodingRouter → Router chain) */
    router = new ReliableRouter();
    printk("Mesh     : Router created (ReliableRouter)\n");

    /* Create the LR2021Interface (starts internal Zephyr radio thread) */
    auto radioIf = initLoRa();
    if (!radioIf) {
        printk("=== Meshtastic boot FAILED (initLoRa returned null) ===\n");
        return -EIO;
    }
    printk("Mesh     : LR2021Interface init OK\n");

    /* Stash a typed pointer before ownership moves into the router, so the
     * Multi-SF beaconer can query main/side SFs and force per-TX SF below. */
    LR2021Interface *lr2021If = static_cast<LR2021Interface *>(radioIf.get());

    /* Gap 9: Wire RadioInterface to Router — packets now flow through real routing */
    router->addInterface(std::move(radioIf));
    printk("Mesh     : Router ↔ LR2021Interface wired\n");

    /* Create NodeStatus (needed before PositionModule constructor) */
    if (!nodeStatus)
        nodeStatus = new meshtastic::NodeStatus();
    printk("Mesh     : NodeStatus created\n");

    /* Gap 12+15: Initialize modules (RoutingModule, NodeInfoModule, TextMessageModule, PositionModule, etc.) */
    setupModules();
    printk("Mesh     : modules initialized\n");

#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE
    /* Phase 4: multi-SF NodeInfo beaconer — round-robins NodeInfo across main
     * + each active side detector SF so peers on any of those SFs consider us
     * a normal neighbor. Must run after setupModules() so nodeInfoModule exists. */
    multiSFBeaconer = new MultiSFBeaconer(lr2021If);

    /* Phase 6: cross-SF broadcast flood relay. Instantiated whenever Multi-SF
     * is enabled, but actual fan-out behavior is gated separately by
     * MULTI_SF_BROADCAST_FANOUT (off by default since Phase 7). */
    multiSFBridge = new MultiSFBridge(lr2021If);
#else
    (void)lr2021If;  // suppress unused-variable warning when Multi-SF is compiled out
#endif

    /* Node number is now derived from MAC address by real NodeDB::pickNewNodeNum() */
    printk("Mesh     : node number 0x%08X (from MAC)\n", nodeDB->getNodeNum());

#if IS_ENABLED(CONFIG_MESHTASTIC_FIXED_POSITION)
    /* Tier 3: Set a fixed position so PositionModule has data to broadcast */
    {
        config.position.fixed_position = true;
        meshtastic_Position pos = meshtastic_Position_init_default;
        pos.latitude_i = 377490000;   /* 37.7490 N (SF) */
        pos.longitude_i = -1224194000; /* -122.4194 W */
        pos.altitude = 10;
        pos.time = 1711324800;         /* fixed timestamp */
        nodeDB->setLocalPosition(pos);
        printk("Mesh     : fixed position set (37.749N, -122.419W)\n");
    }
#endif

    printk("\n=== Meshtastic boot OK ===\n");

#if HAS_SCREEN
    /* Initialize full Meshtastic graphics UI on SSD1306 OLED */
    screen_found = ScanI2C::DeviceAddress{ScanI2C::I2CPort::WIRE, 0x3c};
    screen = new graphics::Screen(screen_found,
        meshtastic_Config_DisplayConfig_OledType_OLED_SSD1306,
        GEOMETRY_128_64);
    screen->setup();
    printk("Screen   : full graphics UI initialized\n");
#else
    /* Fallback: simple text status display (no-op if CONFIG_DISPLAY not set) */
    display_status_init();
#endif

    /* Initialize BLE Phone API directly (not in a thread) */
    printk("BLE      : starting phone API init...\n");
    ble_phone_api_init();
    printk("BLE      : phone API init complete\n");

    /* Initialize PowerFSM — manages screen on/off, BLE, and sleep states */
    PowerFSM_setup();
    printk("PowerFSM : initialized\n");

    /* Initialize input subsystem — creates ButtonThread for user button (P0.00),
     * registers with InputBroker. Must come after screen->setup() (button config
     * checks for screen) and PowerFSM_setup() (button events trigger PowerFSM). */
#if !MESHTASTIC_EXCLUDE_INPUTBROKER
    if (inputBroker)
        inputBroker->Init();
    printk("Input    : button handler initialized\n");
#endif

#if IS_ENABLED(CONFIG_MESHTASTIC_SEND_TEST_TEXT)
    /* End-to-end test: send text message(s) via the real stack.
     * Delay briefly to let the other node(s) enter RX first. */
    {
        k_msleep(2000);  /* let receiver boot and enter RX */

        for (int _msg_i = 0; _msg_i < CONFIG_MESHTASTIC_SEND_COUNT; _msg_i++) {
            if (_msg_i > 0)
                k_msleep(3000);  /* spacing between multi-send */

            meshtastic_MeshPacket *p = router->allocForSending();
            if (p) {
                static const char test_text[] = "Hello Mesh!";
                p->to = (uint32_t)CONFIG_MESHTASTIC_SEND_TARGET;
                p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                p->decoded.payload.size = sizeof(test_text) - 1;
                memcpy(p->decoded.payload.bytes, test_text, sizeof(test_text) - 1);
                p->want_ack = false;
                p->channel = (uint8_t)CONFIG_MESHTASTIC_CHANNEL_INDEX;
                p->hop_limit = (uint8_t)CONFIG_MESHTASTIC_SEND_HOP_LIMIT;

                printk("E2E      : sending text '%s' to=0x%08X hop=%u ch=%u\n",
                       test_text, p->to, p->hop_limit, p->channel);
                service->sendToMesh(p, RX_SRC_LOCAL);
                printk("E2E      : text enqueued id=0x%08X\n", p->id);
            } else {
                printk("E2E      : allocForSending failed!\n");
            }
        }
    }
#endif

    /* Cooperative main loop */
    bool bootDumped = false;
    uint32_t lastRadioMissedIrqPoll = 0;
#if IS_ENABLED(CONFIG_MESHTASTIC_DEBUG_LOGGING)
    uint32_t lastHeartbeat = 0;
#endif
    while (1) {
        concurrency::mainController.run();
        if (service) service->loop();
        powerFSM.run_machine();

        /* Poll for missed radio IRQs — edge-triggered GPIO interrupts can be
         * lost if they fire while irq_lock() is held (e.g. during CRACEN AES). */
        if (RadioLibInterface::instance &&
            (millis() - lastRadioMissedIrqPoll) >= 200) {
            lastRadioMissedIrqPoll = millis();
            RadioLibInterface::instance->pollMissedIrqs();
        }

        uint32_t nextMs = concurrency::mainController.getNextRun();
        if (nextMs > 0)
            concurrency::mainDelay.delay(nextMs);

#if IS_ENABLED(CONFIG_MESHTASTIC_DEBUG_LOGGING)
        if ((millis() - lastHeartbeat) >= 300000) {
            lastHeartbeat = millis();
            auto *st = powerFSM.get_current_state();
            LOG_INFO("heartbeat uptime=%us fsm=%s",
                     (unsigned)(k_uptime_get() / 1000),
                     st ? st->name : "?");
        }
#endif

        /* Check for pending reboot (e.g., after config change from phone app) */
        extern uint32_t rebootAtMsec;
        if (rebootAtMsec && millis() > rebootAtMsec) {
            printk("Rebooting per config change request...\n");
            k_msleep(100);
            sys_reboot(SYS_REBOOT_COLD);
        }

        /* Check for pending shutdown (e.g., long-long-press of user button) */
        extern uint32_t shutdownAtMsec;
        if (shutdownAtMsec && millis() > shutdownAtMsec) {
            shutdownAtMsec = 0;
            printk("Shutting down per user request...\n");
            doDeepSleep(0xFFFFFFFF, false, false);
        }

        /* One-shot: dump boot status at 45s so serial capture can see it */
        if (!bootDumped && k_uptime_get() > 45000) {
            bootDumped = true;
            extern const RegionInfo *myRegion;
            printk("\n=== BOOT STATUS DUMP (45s) ===\n");
            printk("Node     : 0x%08X\n", nodeDB ? nodeDB->getNodeNum() : 0);
            printk("Region   : %s (code=%d, wideLora=%d)\n",
                   myRegion ? myRegion->name : "null",
                   (int)config.lora.region,
                   myRegion ? myRegion->wideLora : -1);
            printk("Channel  : %s (hash=%d)\n",
                   channels.getName(channels.getPrimaryIndex()),
                   channels.getHash(0));
            printk("Peers    : %u\n",
                   nodeDB ? (unsigned)nodeDB->getNumMeshNodes() : 0);
            /* Radio frequency — use extern savedFreq/savedChannelNum from mesh_zephyr.cpp */
            {
                printk("Freq     : %.3f MHz (ch=%u)\n",
                       (double)RadioInterface_getSavedFreq(),
                       (unsigned)RadioInterface_getSavedChannelNum());
            }
            printk("BLE      : peripheral=%d\n",
                   IS_ENABLED(CONFIG_BT_PERIPHERAL));
            extern int ble_init_result;
            printk("BLE      : init_result=%d (-999=never called, 0=ok)\n",
                   ble_init_result);
            /* Try to check if BLE is advertising by reading state */
            printk("Uptime   : %u ms\n", (unsigned)k_uptime_get());
            printk("=== END BOOT STATUS DUMP ===\n\n");
        }
    }
    return 0;

#else
#error "CONFIG_MESHTASTIC_FULL_STACK must be enabled"
#endif
}
