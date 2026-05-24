/*
 * mesh_zephyr.cpp — Platform layer for Zephyr nRF54L15 Meshtastic port.
 *
 * Provides RadioInterface implementation (stubs + LR2021 factory) and
 * platform-specific globals. NodeDB is the REAL upstream NodeDB.cpp —
 * no stubs here.
 */
#ifdef ARCH_NRF54L15

#include "RadioInterface.h"
#include "MeshRadio.h"
#include "Router.h"
#include "NodeDB.h"
#include "Channels.h"
#include "detect/LoRaRadioType.h"
#include "error.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "SPI.h"
#include "RadioLibInterface.h"
#include "modules/LR2021/LR2021.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/devicetree.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>

class Router;
#include "MeshService.h"
#include "SerialConsole.h"

/* ------------------------------------------------------------------ */
/* LoRa radio type                                                      */
/* ------------------------------------------------------------------ */

LoRaRadioType radioType = NO_RADIO;

/* ------------------------------------------------------------------ */
/* Regulatory regions — minimal table                                  */
/* ------------------------------------------------------------------ */

#define RDEF(name, freq_start, freq_end, duty_cycle, spacing, power_limit, \
             audio_permitted, freq_switching, wide_lora)                    \
    { meshtastic_Config_LoRaConfig_RegionCode_##name,                       \
      freq_start, freq_end, duty_cycle, spacing, power_limit,               \
      audio_permitted, freq_switching, wide_lora, #name }

const RegionInfo regions[] = {
    RDEF(US,      902.0f,  928.0f,  100, 0, 30, true,  false, false),
    RDEF(EU_433,  433.0f,  434.0f,   10, 0, 10, true,  false, false),
    RDEF(EU_868,  869.4f,  869.65f,  10, 0, 27, false, false, false),
    RDEF(CN,      470.0f,  510.0f,  100, 0, 19, true,  false, false),
    RDEF(JP,      920.5f,  923.5f,  100, 0, 16, true,  false, false),
    RDEF(ANZ,     915.0f,  928.0f,  100, 0, 30, true,  false, false),
    RDEF(KR,      920.0f,  923.0f,  100, 0, 14, true,  false, false),
    RDEF(TW,      920.0f,  925.0f,  100, 0, 27, true,  false, false),
    RDEF(RU,      868.7f,  869.2f,  100, 0, 20, true,  false, false),
    RDEF(IN,      865.0f,  867.0f,  100, 0, 30, true,  false, false),
    RDEF(NZ_865,  864.0f,  868.0f,  100, 0, 36, true,  false, false),
    RDEF(TH,      920.0f,  925.0f,  100, 0, 16, true,  false, false),
    RDEF(UA_433,  433.0f,  434.79f, 100, 0, 10, true,  false, false),
    RDEF(UA_868,  868.0f,  868.6f,   1, 0, 14, false, false, false),
    RDEF(MY_433,  433.0f,  435.0f,  100, 0, 10, true,  false, false),
    RDEF(MY_919,  919.0f,  924.0f,  100, 0, 27, true,  false, false),
    RDEF(SG_923,  920.0f,  925.0f,  100, 0, 16, true,  false, false),
    RDEF(LORA_24, 2400.0f, 2483.5f, 100, 0, 10, true,  false, true),
    RDEF(UNSET,   902.0f,  928.0f,  100, 0, 30, true,  false, false), /* last/default */
};

const RegionInfo *myRegion = &regions[0];

/* ------------------------------------------------------------------ */
/* Observables required by RadioInterface member initializers         */
/* ------------------------------------------------------------------ */

Observable<void *> preflightSleep;
Observable<void *> notifyDeepSleep;

/* ------------------------------------------------------------------ */
/* Global pointer stubs — platform-specific                           */
/* ------------------------------------------------------------------ */

Router     *router  = nullptr;

/* SPI lock (SPILock.h extern) */
static concurrency::Lock s_spiLock;
concurrency::Lock *spiLock = &s_spiLock;

/* BLE logging pause flag (PhoneAPI.cpp uses this) */
bool pauseBluetoothLogging = false;

/* CryptoEngine singleton — defined by CryptoEngine.cpp (software) or
 * NRF54L15CryptoEngine.cpp (CRACEN hardware) depending on
 * HAS_CUSTOM_CRYPTO_ENGINE. We only need to provide it here for
 * non-FULL_STACK builds where neither of those files defines it. */
#include "CryptoEngine.h"
#if !IS_ENABLED(CONFIG_MESHTASTIC_FULL_STACK)
CryptoEngine *crypto = nullptr;
#endif

/* Module forward declarations (real .cpp files linked) */
#include "MeshModule.h"

/* ------------------------------------------------------------------ */
/* RTC — Zephyr k_uptime + optional network-synced offset             */
/* ------------------------------------------------------------------ */

#include "gps/RTC.h"
uint32_t rtcOffsetSec = 0;
RTCQuality rtcQuality = RTCQualityNone;

uint32_t getTime(bool /*addClock*/)
{
    return (uint32_t)(k_uptime_get() / 1000) + rtcOffsetSec;
}

uint32_t getValidTime(RTCQuality minQuality, bool addClock)
{
    if (rtcQuality >= minQuality)
        return getTime(addClock);
    return 0;
}

RTCQuality getRTCQuality() { return rtcQuality; }

bool trySetRTCQuality(RTCQuality q)
{
    if (q > rtcQuality) {
        rtcQuality = q;
        return true;
    }
    return false;
}

/* TransmitHistory — real implementation from TransmitHistory.cpp */

/* ------------------------------------------------------------------ */
/* AirTime free-standing helpers                                      */
/* ------------------------------------------------------------------ */

void logAirtime(reportTypes type, uint32_t ms) {
    if (airTime) airTime->logAirtime(type, ms);
}
uint32_t *airtimeReport(reportTypes type) {
    return airTime ? airTime->airtimeReport(type) : nullptr;
}

/* ------------------------------------------------------------------ */
/* Power/status stubs                                                  */
/* ------------------------------------------------------------------ */

/* shouldWakeOnReceivedMessage() — provided by Screen.cpp when HAS_SCREEN=1 */
#if !HAS_SCREEN
bool shouldWakeOnReceivedMessage() { return false; }
#endif

#include "NodeStatus.h"
meshtastic::NodeStatus *nodeStatus = nullptr;

/* ------------------------------------------------------------------ */
/* main.h globals                                                      */
/* ------------------------------------------------------------------ */

#include "graphics/Screen.h"
graphics::Screen *screen        = nullptr;
uint32_t          rebootAtMsec  = 0;
uint32_t          shutdownAtMsec= 0;
bool              runASAP       = false;
bool              isUSBPowered  = false;
uint32_t          timeLastPowered = 0;

const char *getDeviceName()
{
    static char name[32];
    NodeNum myNum = nodeDB ? nodeDB->getNodeNum() : 0;
    snprintf(name, sizeof(name), "Meshtastic_%04X",
             (unsigned)(myNum & 0xFFFFu));
    return name;
}

/* ------------------------------------------------------------------ */
/* SPI / Serial globals (Zephyr shims)                                */
/* ------------------------------------------------------------------ */

SPIClass SPI;
_ZephyrSerial Serial;
_ZephyrSerial Serial1;

/* ------------------------------------------------------------------ */
/* Console-UART RX backend for `Serial`                                */
/* ------------------------------------------------------------------ */
/*
 * WHY: The Arduino Serial shim's `print/printf/write` route to printk
 * (which Zephyr's console driver sends out the chosen UART, here uart20,
 * bridged to the host as /dev/ttyACM0). But the shim's RX side
 * (available/read/peek) was stubbed to "no data, ever." Meshtastic's
 * SerialConsole sits on `Stream` and expects to read ToRadio protobuf
 * frames byte-by-byte; with a permanently-empty stream the wake byte
 * from `meshtastic --port` never arrives, so every CLI command times
 * out with "Timed out waiting for connection completion."
 *
 * HOW: hook an IRQ-driven RX callback onto the same console UART
 * device. The callback drains the device FIFO into a small ring buffer
 * (256 bytes is plenty for ToRadio frames — a single frame is ≤256 B
 * and SerialConsole reads continuously). _zephyr_serial_available/
 * read/peek pull from the ringbuf with no locking other than what
 * Zephyr's ring_buf already provides (single-producer in IRQ ctx,
 * single-consumer in thread ctx is safe).
 *
 * SCOPE: console UART is shared with printk's TX path, but printk only
 * writes — it never reads — so there's no contention on the RX side.
 * The CLI handshake (wake byte → SerialConsole sets canWrite=true →
 * SerialConsole pumps FromRadio frames out via printk) now completes
 * because the wake byte actually reaches the consumer.
 */

#define ZEPHYR_SERIAL_RX_BUF_SIZE 256
RING_BUF_DECLARE(zephyr_serial_rx_rb, ZEPHYR_SERIAL_RX_BUF_SIZE);
static const struct device *zephyr_serial_uart = nullptr;
static int zephyr_serial_peek_byte = -1;

/* SerialConsole's runOnce() returns INT32_MAX when its underlying
 * Stream has no bytes available, then sleeps until nudged. Without
 * an explicit wake-up, bytes our IRQ pushes into the ring buffer
 * would sit there forever. We forward each IRQ to
 * SerialConsole::rxInt() (a small helper that schedules runOnce for
 * "now") so the OSThread polls our Stream the moment data lands. */
extern "C" void zephyr_serial_console_wake(void); /* defined below */

static void zephyr_serial_rx_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);
    if (!uart_irq_update(dev)) return;
    bool got_any = false;
    while (uart_irq_rx_ready(dev)) {
        uint8_t buf[32];
        int n = uart_fifo_read(dev, buf, sizeof(buf));
        if (n <= 0) break;
        /* Drop overflow silently. The CLI re-tries on timeout, and we'd
         * rather lose bytes than block in IRQ context. */
        (void)ring_buf_put(&zephyr_serial_rx_rb, buf, (uint32_t)n);
        got_any = true;
    }
    if (got_any) zephyr_serial_console_wake();
}

static void zephyr_serial_rx_init(void)
{
    /* DT_CHOSEN(zephyr_console) → the same UART that printk targets,
     * which on XIAO nRF54L15 is uart20 routed to host via SAMD11 USB-CDC. */
    zephyr_serial_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (!zephyr_serial_uart || !device_is_ready(zephyr_serial_uart)) {
        printk("Serial   : console UART not ready, RX disabled\n");
        zephyr_serial_uart = nullptr;
        return;
    }
    int err = uart_irq_callback_user_data_set(zephyr_serial_uart,
                                              zephyr_serial_rx_isr, nullptr);
    if (err) {
        printk("Serial   : uart_irq_callback_user_data_set failed (%d)\n", err);
        zephyr_serial_uart = nullptr;
        return;
    }
    uart_irq_rx_enable(zephyr_serial_uart);
    printk("Serial   : RX backend ready on %s\n", zephyr_serial_uart->name);
}

extern "C" int _zephyr_serial_available(void)
{
    return (int)ring_buf_size_get(&zephyr_serial_rx_rb)
           + (zephyr_serial_peek_byte >= 0 ? 1 : 0);
}

extern "C" int _zephyr_serial_read(void)
{
    if (zephyr_serial_peek_byte >= 0) {
        int b = zephyr_serial_peek_byte;
        zephyr_serial_peek_byte = -1;
        return b;
    }
    uint8_t b;
    if (ring_buf_get(&zephyr_serial_rx_rb, &b, 1) == 1) return b;
    return -1;
}

extern "C" int _zephyr_serial_peek(void)
{
    if (zephyr_serial_peek_byte >= 0) return zephyr_serial_peek_byte;
    uint8_t b;
    if (ring_buf_get(&zephyr_serial_rx_rb, &b, 1) == 1) {
        zephyr_serial_peek_byte = b;
        return b;
    }
    return -1;
}

/* main-nrf54l15.cpp calls this from its own setup() since it doesn't
 * use the upstream main.cpp::setup() flow that would otherwise call
 * earlyInitVariant() at the right time. */
extern "C" void zephyr_serial_rx_init_external(void) { zephyr_serial_rx_init(); }

/* IRQ-context wake-up for SerialConsole: rxInt() is a tiny forwarder
 * that calls setIntervalFromNow(0) on the OSThread base, just writing
 * a member variable, so calling it from ISR context is safe.
 *
 * NOTE: the console global has C++ linkage (defined in SerialConsole.cpp
 * as `SerialConsole *console;`). Declaring the extern at C++ file scope
 * here rather than inside the extern "C" body keeps the linkage correct;
 * clang-tidy's clang-diagnostic-error on "different language linkage"
 * was the flag. The function itself still needs C linkage so it can be
 * called from IRQ C code. */
extern SerialConsole *console;

extern "C" void zephyr_serial_console_wake(void)
{
    if (console) console->rxInt();
}

/* _ZephyrSerial::write() - direct UART TX for SerialConsole frames.
 *
 * uart_poll_out() polls the TX FIFO-ready flag once per byte and
 * emits it. Under host back-pressure this is a short spin, bounded
 * by the USB-CDC bridge's flow-control window (typically ~10 ms);
 * far cheaper than going through the deferred log ring buffer and
 * getting dropped on overflow mid-frame. Safe to call from the main
 * thread during the OSThread scheduler tick that runs StreamAPIs
 * writeStream(). */
size_t _ZephyrSerial::write(uint8_t b)
{
    if (!zephyr_serial_uart) return 0;
    uart_poll_out(zephyr_serial_uart, (unsigned char)b);
    return 1;
}

size_t _ZephyrSerial::write(const uint8_t *buf, size_t len)
{
    if (!zephyr_serial_uart || !buf) return 0;
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(zephyr_serial_uart, buf[i]);
    }
    return len;
}

/* ------------------------------------------------------------------ */
/* Log output                                                          */
/* ------------------------------------------------------------------ */
/*
 * With CONFIG_MESHTASTIC_DEBUG_LOGGING=y, logs and printk target
 * SEGGER RTT (see the select chain in zephyr/Kconfig). The UART is
 * reserved for SerialConsole's 0x94c3-framed protobuf traffic so the
 * CLI handshake never contends with debug text on the same wire.
 * Read RTT on the host with `pyocd rtt -u <uid> -t nrf54l`.
 *
 * With CONFIG_MESHTASTIC_DEBUG_LOGGING=n, logLegacy/qprintk are empty
 * stubs, the RTT/LOG subsystems are not linked, and the LOG_* macros
 * in architecture.h expand to no-ops. That drops ~120 KB of flash
 * for production builds.
 */
#if IS_ENABLED(CONFIG_MESHTASTIC_DEBUG_LOGGING)
extern "C" void logLegacy(const char *level, const char *fmt, ...)
{
    if (level) printk("%s ", level);
    va_list ap;
    va_start(ap, fmt);
    vprintk(fmt, ap);
    va_end(ap);
    printk("\n");
}

extern "C" void qprintk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintk(fmt, ap);
    va_end(ap);
}
#else
extern "C" void logLegacy(const char *, const char *, ...) { }
extern "C" void qprintk(const char *, ...) { }
#endif

/* ------------------------------------------------------------------ */
/* mesh_zephyr_set_nodenum — entry point from main                    */
/* ------------------------------------------------------------------ */

extern "C" void mesh_zephyr_set_nodenum(uint32_t num)
{
    if (nodeDB) {
        myNodeInfo.my_node_num = num;
    }
}

/* ================================================================== */
/* RadioInterface — stubs (we don't link RadioInterface.cpp because    */
/* it has SX1262/SX1280/RF95 drivers we don't need)                   */
/* ================================================================== */

bool RadioInterface::uses_default_frequency_slot = true;

/* Debug: track last configured frequency for boot dump */
static float g_lastFreq = 0;
static uint32_t g_lastChNum = 0;
extern "C" float RadioInterface_getSavedFreq(void) { return g_lastFreq; }
extern "C" uint32_t RadioInterface_getSavedChannelNum(void) { return g_lastChNum; }

RadioInterface::RadioInterface()
{
    assert(sizeof(PacketHeader) == MESHTASTIC_HEADER_LENGTH);
}

bool RadioInterface::init()
{
    LOG_INFO("RadioInterface: Zephyr init");
    applyModemConfig();
    return true;
}

bool RadioInterface::reconfigure()
{
    applyModemConfig();
    return true;
}

int RadioInterface::notifyDeepSleepCb(void *)
{
    sleep();
    return 0;
}

void RadioInterface::applyModemConfig()
{
    bool wideLora = myRegion ? myRegion->wideLora : false;

    meshtastic_Config_LoRaConfig_ModemPreset preset =
        config.lora.modem_preset ? config.lora.modem_preset
                                 : meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    modemPresetToParams(preset, wideLora, bw, sf, cr);

    if (config.lora.spread_factor != 0) sf = config.lora.spread_factor;
    if (config.lora.bandwidth != 0) bw = (float)config.lora.bandwidth;
    if (config.lora.coding_rate != 0) cr = (uint8_t)config.lora.coding_rate;

    power = (config.lora.tx_power != 0) ? (int8_t)config.lora.tx_power : 10;
    preambleLength = 16;

    if (myRegion) {
        /* Match upstream RadioInterface::applyModemConfig frequency calculation */
        uint32_t numCh = (uint32_t)((myRegion->freqEnd - myRegion->freqStart) /
                                    (myRegion->spacing + (bw / 1000.0f)));
        if (numCh == 0) numCh = 1;

        /* Use channel name hash for frequency slot — must match other Meshtastic nodes */
        const char *channelName = channels.getName(channels.getPrimaryIndex());
        uint32_t chNum;
        if (config.lora.channel_num) {
            chNum = (config.lora.channel_num - 1) % numCh;
        } else {
            /* DJB2 hash of channel name — same as upstream hash() */
            uint32_t h = 5381;
            for (const char *s = channelName; s && *s; s++)
                h = ((h << 5) + h) + (unsigned char)*s;
            chNum = h % numCh;
        }

        savedFreq = myRegion->freqStart + (bw / 2000.0f) + (chNum * (bw / 1000.0f));
        savedChannelNum = chNum;
        g_lastFreq = savedFreq;
        g_lastChNum = chNum;
    } else {
        savedFreq = 906.875f;
        savedChannelNum = 0;
    }

    slotTimeMsec = computeSlotTimeMsec();

    LOG_INFO("RadioInterface: SF%u BW%.0fkHz CR4/%u %.3fMHz %ddBm",
             sf, bw, cr, savedFreq, power);
}

static inline float _pow2f(uint8_t e) { return (float)(1u << e); }

uint32_t RadioInterface::computeSlotTimeMsec()
{
    float prop = 0.2f + 0.4f + 7.0f;
    float sym  = _pow2f(sf) / bw;
    float cad  = (myRegion && myRegion->wideLora)
                 ? (float)(NUM_SYM_CAD_24GHZ + (2 * sf + 3) / 32) * sym
                 : max(2.25f, (float)(NUM_SYM_CAD + 0.5f))          * sym;
    return (uint32_t)(cad + prop);
}

void     RadioInterface::saveFreq(float f)       { savedFreq       = f; g_lastFreq = f; }
void     RadioInterface::saveChannelNum(uint32_t ch){ savedChannelNum = ch; g_lastChNum = ch; }
float    RadioInterface::getFreq()               { return savedFreq; }
uint32_t RadioInterface::getChannelNum()         { return savedChannelNum; }

size_t RadioInterface::beginSending(meshtastic_MeshPacket *p)
{
    assert(!sendingPacket);
    assert(p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag);

    radioBuffer.header.from       = p->from;
    radioBuffer.header.to         = p->to;
    radioBuffer.header.id         = p->id;
    radioBuffer.header.channel    = p->channel;
    radioBuffer.header.next_hop   = p->next_hop;
    radioBuffer.header.relay_node = p->relay_node;

    if (p->hop_limit > HOP_MAX) {
        p->hop_limit = HOP_RELIABLE;
    }
    radioBuffer.header.flags =
        (uint8_t)(p->hop_limit)
        | (p->want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0u)
        | (p->via_mqtt ? PACKET_FLAGS_VIA_MQTT_MASK : 0u);
    radioBuffer.header.flags |= (uint8_t)((p->hop_start << PACKET_FLAGS_HOP_START_SHIFT)
                                           & PACKET_FLAGS_HOP_START_MASK);

    assert(radioBuffer.header.from);
    assert(p->encrypted.size <= sizeof(radioBuffer.payload));
    memcpy(radioBuffer.payload, p->encrypted.bytes, p->encrypted.size);

    sendingPacket = p;
    return p->encrypted.size + sizeof(PacketHeader);
}

void RadioInterface::deliverToReceiver(meshtastic_MeshPacket *p)
{
    if (router) {
        p->transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
        router->enqueueReceivedMessage(p);
    } else {
        packetPool.release(p);
    }
}

uint32_t RadioInterface::getPacketTime(const meshtastic_MeshPacket *p, bool received)
{
    uint32_t pl;
    if (p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        pl = (uint32_t)p->encrypted.size + (uint32_t)sizeof(PacketHeader);
    } else {
        pl = 100u + (uint32_t)sizeof(PacketHeader);
    }
    return getPacketTime(pl, received);
}

uint32_t RadioInterface::getRetransmissionMsec(const meshtastic_MeshPacket *p)
{
    uint32_t airtime = getPacketTime(p, false);
    return 2u * airtime + (_pow2f(CWmax) + 2u * CWmax +
                           _pow2f((CWmax + CWmin) / 2u)) * slotTimeMsec +
           PROCESSING_TIME_MSEC;
}

uint32_t RadioInterface::getTxDelayMsec()
{
    return 100u + (uint32_t)(millis() % 400u);
}

uint8_t RadioInterface::getCWsize(float snr) { (void)snr; return CWmin; }

uint32_t RadioInterface::getTxDelayMsecWeightedWorst(float snr) { (void)snr; return getTxDelayMsec(); }

bool RadioInterface::shouldRebroadcastEarlyLikeRouter(meshtastic_MeshPacket *p) { (void)p; return false; }

uint32_t RadioInterface::getTxDelayMsecWeighted(meshtastic_MeshPacket *p) { (void)p; return getTxDelayMsec(); }

void RadioInterface::limitPower(int8_t loraMaxPower)
{
    if (loraMaxPower > 0 && power > loraMaxPower)
        power = loraMaxPower;
}

void RadioInterface::bootstrapLoRaConfigFromPreset(meshtastic_Config_LoRaConfig &loraConfig)
{
    /* Must use modemPresetToParams to get correct values for the region.
     * Hardcoding BW=250 is wrong for 2.4 GHz (needs 812.5). */
    bool wideLora = myRegion ? myRegion->wideLora : false;
    float bwKHz = 0; uint8_t sfTmp = 0, crTmp = 0;
    modemPresetToParams(loraConfig.modem_preset ? loraConfig.modem_preset
                        : meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
                        wideLora, bwKHz, sfTmp, crTmp);
    loraConfig.bandwidth     = (uint16_t)bwKHz;
    loraConfig.spread_factor = sfTmp;
    loraConfig.coding_rate   = crTmp;
}

/* ------------------------------------------------------------------ */
/* initLoRa() — LR2021 factory                                        */
/* ------------------------------------------------------------------ */

#ifdef USE_LR2021
#include "LR2021Interface.h"
#endif

std::unique_ptr<RadioInterface> initLoRa()
{
#ifdef USE_LR2021
    /* Get DTS specs for LR2021 */
    #define LR2021_NODE DT_NODELABEL(lr2021)
    BUILD_ASSERT(DT_NODE_HAS_STATUS(LR2021_NODE, okay), "lr2021 SPI node not enabled");

    static const struct spi_dt_spec lr2021_spi =
        SPI_DT_SPEC_GET(LR2021_NODE, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0);

    #if DT_NODE_HAS_PROP(LR2021_NODE, irq_gpios)
    static const struct gpio_dt_spec lr2021_irq = GPIO_DT_SPEC_GET(LR2021_NODE, irq_gpios);
    #define LR2021_IRQ_PTR &lr2021_irq
    #else
    #define LR2021_IRQ_PTR nullptr
    #endif

    #if DT_NODE_HAS_PROP(LR2021_NODE, busy_gpios)
    static const struct gpio_dt_spec lr2021_busy = GPIO_DT_SPEC_GET(LR2021_NODE, busy_gpios);
    #define LR2021_BUSY_PTR &lr2021_busy
    #else
    #define LR2021_BUSY_PTR nullptr
    #endif

    #if DT_NODE_HAS_PROP(LR2021_NODE, reset_gpios)
    static const struct gpio_dt_spec lr2021_rst = GPIO_DT_SPEC_GET(LR2021_NODE, reset_gpios);
    #define LR2021_RST_PTR &lr2021_rst
    #else
    #define LR2021_RST_PTR nullptr
    #endif

    /* Set global SPI/GPIO specs for the ArduinoHal shim */
    extern const struct spi_dt_spec *g_radiolib_spi;
    extern const struct gpio_dt_spec *g_radiolib_irq;
    extern const struct gpio_dt_spec *g_radiolib_rst;
    extern const struct gpio_dt_spec *g_radiolib_busy;
    g_radiolib_spi = &lr2021_spi;
    g_radiolib_irq = LR2021_IRQ_PTR;
    g_radiolib_rst = LR2021_RST_PTR;
    g_radiolib_busy = LR2021_BUSY_PTR;

    /* Pin numbers for RadioLib Module — use GPIO pin numbers from DTS */
    RADIOLIB_PIN_TYPE irqPin = RADIOLIB_NC, rstPin = RADIOLIB_NC, busyPin = RADIOLIB_NC;
    #if DT_NODE_HAS_PROP(LR2021_NODE, irq_gpios)
    irqPin = lr2021_irq.pin;
    #endif
    #if DT_NODE_HAS_PROP(LR2021_NODE, reset_gpios)
    rstPin = lr2021_rst.pin;
    #endif
    #if DT_NODE_HAS_PROP(LR2021_NODE, busy_gpios)
    busyPin = lr2021_busy.pin;
    #endif

    /* Create LockingArduinoHal (uses our Zephyr ArduinoHal shim) */
    static LockingArduinoHal hal(SPI, SPISettings());

    printk("RadioLib : creating LR2021Interface (stub)...\n");
    auto rIf = std::unique_ptr<RadioInterface>(
        new LR2021Interface(&hal, RADIOLIB_NC, irqPin, rstPin, busyPin));
    if (!rIf->init()) {
        LOG_WARN("No LR2021 radio");
        return nullptr;
    }
    radioType = LR2021_RADIO;
    LOG_INFO("LR2021 init success (RadioLib stub)");
    return rIf;
#else
    return nullptr;
#endif
}

/* ------------------------------------------------------------------ */
/* printPacket — debug helper                                          */
/* ------------------------------------------------------------------ */

void printPacket(const char *prefix, const meshtastic_MeshPacket *p)
{
    LOG_DEBUG("%s from=0x%08X to=0x%08X id=0x%08X hop=%u ch=%u",
              prefix ? prefix : "",
              p->from, p->to, p->id,
              (unsigned)p->hop_limit, (unsigned)p->channel);
}

/* ------------------------------------------------------------------ */
/* initRegion                                                          */
/* ------------------------------------------------------------------ */

void initRegion()
{
    auto regionCode = config.lora.region;
    myRegion = nullptr;
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        if (regions[i].code == regionCode) {
            myRegion = &regions[i];
            break;
        }
    }
    if (!myRegion)
        myRegion = &regions[sizeof(regions) / sizeof(regions[0]) - 1];
    LOG_INFO("Region: %s", myRegion->name);
}

#endif /* ARCH_NRF54L15 */
