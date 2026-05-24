/*
 * gap15_16_stubs.cpp — Stubs for PositionModule (Gap 15) and AdminModule (Gap 16)
 *
 * Provides symbols needed by PositionModule.cpp and AdminModule.cpp that
 * don't have real implementations on the nRF54L15 Zephyr platform.
 */
#ifdef ARCH_NRF54L15

#include <cstdint>
#include <cstring>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "Observer.h"

/* ------------------------------------------------------------------ */
/* sleep.h stubs                                                       */
/* ------------------------------------------------------------------ */

extern Observable<void *> notifyDeepSleep;  /* defined in mesh_zephyr.cpp */
extern Observable<void *> preflightSleep;   /* defined in mesh_zephyr.cpp */
Observable<void *> notifyReboot;

int bootCount = 0;
bool bluetoothOn = true;

#include "graphics/Screen.h"
extern graphics::Screen *screen;
#include "NodeDB.h"
#include "PowerMon.h"
extern PowerMon *powerMon;

/* Forward declarations for sleep functions */
void cpuDeepSleep(uint32_t msecToWake);

#if IS_ENABLED(CONFIG_POWEROFF)
#include <zephyr/sys/poweroff.h>
#endif

void doDeepSleep(uint32_t msecToWake, bool skipPreflight, bool skipSaveNodeDb)
{
    if (msecToWake == 0xFFFFFFFF) {
        printk("PowerFSM : entering deep sleep forever\n");
    } else {
        printk("PowerFSM : entering deep sleep for %u seconds\n", msecToWake / 1000);
    }

    /* Notify observers (radio shutdown, etc.) */
    notifyDeepSleep.notifyObservers(NULL);

    /* Turn off screen */
    if (screen)
        screen->doDeepSleep();

    /* Save NodeDB to flash */
    if (!skipSaveNodeDb && nodeDB) {
        nodeDB->saveToDisk();
        printk("PowerFSM : NodeDB saved\n");
    }

    /* Enter CPU deep sleep */
    cpuDeepSleep(msecToWake);
}

void cpuDeepSleep(uint32_t msecToWake)
{
    (void)msecToWake;
    printk("PowerFSM : sys_poweroff()\n");

#if IS_ENABLED(CONFIG_POWEROFF)
    sys_poweroff();  /* Never returns — device reboots on wake */
#else
    /* Fallback: just halt if POWEROFF not enabled */
    printk("PowerFSM : CONFIG_POWEROFF not enabled, halting\n");
    k_sleep(K_FOREVER);
#endif
}

void initDeepSleep()
{
    /* Check reset reason — useful for debugging wake source */
    printk("PowerFSM : initDeepSleep (boot #%d)\n", bootCount);
    bootCount++;
}

void setCPUFast(bool on) { (void)on; }   /* nRF54L15 runs at fixed 128MHz */

bool doPreflightSleep()
{
    return preflightSleep.notifyObservers(NULL) == 0;
}

void enableModemSleep() {}   /* ESP32-only */

/* ------------------------------------------------------------------ */
/* target_specific.h — BLE control                                     */
/* ------------------------------------------------------------------ */

#include <zephyr/bluetooth/bluetooth.h>

/* BLE advertising parameters — must match ble_phone_api.cpp */
extern const struct bt_le_adv_param *ble_adv_params;
extern const struct bt_data *ble_ad_data;
extern size_t ble_ad_data_len;
extern const struct bt_data *ble_sd_data;
extern size_t ble_sd_data_len;

void setBluetoothEnable(bool enable)
{
    if (enable == bluetoothOn) return;

    if (enable) {
        printk("PowerFSM : BLE advertising start\n");
        extern void ble_start_advertising(void);
        ble_start_advertising();
    } else {
        printk("PowerFSM : BLE advertising stop\n");
        int err = bt_le_adv_stop();
        if (err && err != -EALREADY) {
            printk("PowerFSM : bt_le_adv_stop failed (%d)\n", err);
        }
    }
    bluetoothOn = enable;
}

void getMacAddr(uint8_t *dmac)
{
    /* Derive from Zephyr's hardware device ID if available */
#if DT_NODE_EXISTS(DT_INST(0, nordic_nrf_ficr))
    /* nRF54L15 FICR has DEVICEADDR registers */
    uint32_t lo = NRF_FICR->DEVICEADDR[0];
    uint32_t hi = NRF_FICR->DEVICEADDR[1];
    dmac[0] = (lo >>  0) & 0xFF;
    dmac[1] = (lo >>  8) & 0xFF;
    dmac[2] = (lo >> 16) & 0xFF;
    dmac[3] = (lo >> 24) & 0xFF;
    dmac[4] = (hi >>  0) & 0xFF;
    dmac[5] = (hi >>  8) & 0xFF;
#else
    /* Fallback: use a hash of the node number */
    uint32_t num = myNodeInfo.my_node_num;
    dmac[0] = (num >> 0) & 0xFF;
    dmac[1] = (num >> 8) & 0xFF;
    dmac[2] = (num >> 16) & 0xFF;
    dmac[3] = (num >> 24) & 0xFF;
    dmac[4] = 0xFE;
    dmac[5] = 0xCA;
#endif
}

/* ------------------------------------------------------------------ */
/* PowerStatus stub                                                    */
/* ------------------------------------------------------------------ */

#include "PowerStatus.h"
static meshtastic::PowerStatus s_powerStatus;
meshtastic::PowerStatus *powerStatus = &s_powerStatus;

/* ------------------------------------------------------------------ */
/* ScanI2C stubs (no real I2C scanning in simulation)                  */
/* ------------------------------------------------------------------ */

#include "detect/ScanI2C.h"

ScanI2C::DeviceAddress::DeviceAddress() : port(NO_I2C), address(0) {}
ScanI2C::DeviceAddress::DeviceAddress(I2CPort p, uint8_t a) : port(p), address(a) {}
bool ScanI2C::DeviceAddress::operator<(const DeviceAddress &o) const { return address < o.address; }

const ScanI2C::DeviceAddress ScanI2C::ADDRESS_NONE = ScanI2C::DeviceAddress();
ScanI2C::DeviceAddress rtc_found = ScanI2C::ADDRESS_NONE;
ScanI2C::DeviceAddress screen_found = ScanI2C::ADDRESS_NONE;
ScanI2C::DeviceAddress cardkb_found = ScanI2C::ADDRESS_NONE;

/* rmDir — now provided by FSCommon.cpp */

/* ------------------------------------------------------------------ */
/* GPS stub (nullptr — no GPS hardware)                               */
/* ------------------------------------------------------------------ */

#include "GPS.h"
std::unique_ptr<GPS> gps;

/* ------------------------------------------------------------------ */
/* RTC / time stubs                                                    */
/* ------------------------------------------------------------------ */

#include "gps/RTC.h"
uint32_t lastSetFromPhoneNtpOrGps = 0;

/* These are defined in mesh_zephyr.cpp */
extern uint32_t getTime(bool);
extern RTCQuality rtcQuality;
extern uint32_t rtcOffsetSec;

RTCSetResult perhapsSetRTC(RTCQuality q, const struct timeval *tv, bool forceUpdate)
{
    if (!tv) return RTCSetResultNotSet;
    if (q > rtcQuality || forceUpdate) {
        /* Set the offset so getTime() returns correct wall-clock */
        uint32_t uptimeSec = (uint32_t)(k_uptime_get() / 1000);
        rtcOffsetSec = (uint32_t)tv->tv_sec - uptimeSec;
        rtcQuality = q;
        lastSetFromPhoneNtpOrGps = (uint32_t)(k_uptime_get());
        return RTCSetResultSuccess;
    }
    return RTCSetResultNotSet;
}
RTCSetResult perhapsSetRTC(RTCQuality q, const struct tm &t)
{
    (void)q; (void)t;
    return RTCSetResultNotSet;  /* tm overload not commonly used */
}

/* ------------------------------------------------------------------ */
/* Battery stub                                                        */
/* ------------------------------------------------------------------ */

void updateBatteryLevel(uint8_t level) { (void)level; }

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* getDeviceMetadata (declared in upstream main.h)                     */
/* ------------------------------------------------------------------ */

#include "mesh/generated/meshtastic/deviceonly.pb.h"

meshtastic_DeviceMetadata getDeviceMetadata()
{
    meshtastic_DeviceMetadata m = meshtastic_DeviceMetadata_init_default;
    strncpy(m.firmware_version, APP_VERSION_STRING, sizeof(m.firmware_version));
    m.device_state_version = 22;
    m.hasBluetooth = true;
    m.hasWifi = false;
    m.hasEthernet = false;
    m.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    m.hw_model = meshtastic_HardwareModel_PRIVATE_HW;
#if !(MESHTASTIC_EXCLUDE_PKI)
    m.hasPKC = true;
#endif
    return m;
}

/* NodeDB stubs — ONLY for methods that need filesystem (FSCom)       */
/* All other NodeDB methods come from the real NodeDB.cpp now.         */

/* ------------------------------------------------------------------ */
/* PowerMon stub (RadioLibInterface uses it for TX/RX state tracking) */
/* ------------------------------------------------------------------ */

#include "PowerMon.h"

void PowerMon::setState(meshtastic_PowerMon_State, const char *) {}
void PowerMon::clearState(meshtastic_PowerMon_State, const char *) {}
static PowerMon s_powerMon;
PowerMon *powerMon = &s_powerMon;

/* ------------------------------------------------------------------ */
/* __cxa_guard (C++ static local init — needed for static LockingArduinoHal) */
/* ------------------------------------------------------------------ */

extern "C" {
    int __cxa_guard_acquire(int *guard) { if (*guard) return 0; *guard = 1; return 1; }
    void __cxa_guard_release(int *guard) { (void)guard; }
    void __cxa_guard_abort(int *guard) { (void)guard; }
}

/* PowerFSM — now compiled from real PowerFSM.cpp, no stubs needed */

/* ------------------------------------------------------------------ */
/* XModem stub — only if FSCom is defined (we don't have filesystem)   */
/* ------------------------------------------------------------------ */

/* getFiles — now provided by FSCommon.cpp */

/* printBytes, vformat, isOneOf — now provided by real meshUtils.cpp */

/* ------------------------------------------------------------------ */
/* GPS::enable stub                                                    */
/* ------------------------------------------------------------------ */

void GPS::enable() {}
void GPS::toggleGpsMode() {}

/* ------------------------------------------------------------------ */
/* Screen / graphics stubs                                             */
/* ------------------------------------------------------------------ */

/* Referenced by Screen.cpp for boot screen timing and reboot banner */
uint32_t serialSinceMsec = 0;
bool suppressRebootBanner = false;
bool canSleep = true;

/* error.h — Screen.cpp includes this but we don't have the full error system */
#include <cstdarg>

/* MenuHandler.cpp / CannedMessageModule stubs */
bool kb_found = false;
bool osk_found = false;

/* Accelerometer thread — no motion sensor on nRF54L15 */
namespace concurrency { class AccelerometerThread; }
concurrency::AccelerometerThread *accelerometerThread = nullptr;

/* GPS beep — now provided by real buzz.cpp (tone is no-op) */

/* setenv — not available in picolibc, used by MenuHandler for timezone */
extern "C" int setenv(const char *, const char *, int) { return 0; }

/* Power class stub — PowerFSM.cpp references `extern Power *power` */
#include "power.h"
Power *power = nullptr;

/* GPSStatus — needed by Screen.cpp and DebugRenderer */
#include "GPSStatus.h"
static meshtastic::GPSStatus s_gpsStatus;
meshtastic::GPSStatus *gpsStatus = &s_gpsStatus;

/* CannedMessageModule stubs — MESHTASTIC_EXCLUDE_CANNEDMESSAGES=1 but
 * MenuHandler.cpp directly references cannedMessageModule for text input. */
#include "modules/CannedMessageModule.h"
#if HAS_SCREEN && MESHTASTIC_EXCLUDE_CANNEDMESSAGES
CannedMessageModule *cannedMessageModule = nullptr;
/* Stub the methods referenced by MenuHandler */
void CannedMessageModule::LaunchWithDestination(uint32_t, uint8_t) {}
void CannedMessageModule::LaunchFreetextWithDestination(uint32_t, uint8_t) {}
#endif

/* readFromRTC — used by Screen.cpp for clock display */
RTCSetResult readFromRTC()
{
    return RTCSetResultNotSet; /* no RTC hardware */
}

/* ------------------------------------------------------------------ */
/* Software crypto stubs (dead code — CRACEN overrides all call sites) */
/* ------------------------------------------------------------------ */
/* CryptoEngine.cpp base-class methods reference these symbols via the
 * vtable even though NRF54L15CryptoEngine overrides every path. The
 * linker still needs the symbols, so we provide no-op stubs here to
 * avoid pulling in Curve25519.cpp and aes-ccm.cpp. */

#include <Curve25519.h>
#include <RNG.h>
#include "aes-ccm.h"

RNGClass RNG;

void Curve25519::dh1(uint8_t *, uint8_t *) {}
bool Curve25519::dh2(uint8_t *, uint8_t *) { return false; }
bool Curve25519::eval(uint8_t *, const uint8_t *, const uint8_t *) { return false; }
uint8_t Curve25519::isWeakPoint(const uint8_t *) { return 1; }

#if !MESHTASTIC_EXCLUDE_PKI
int aes_ccm_ae(const uint8_t *, size_t, const uint8_t *, size_t,
               const uint8_t *, size_t, const uint8_t *, size_t,
               uint8_t *, uint8_t *) { return -1; }
bool aes_ccm_ad(const uint8_t *, size_t, const uint8_t *, size_t,
                const uint8_t *, size_t, const uint8_t *, size_t,
                const uint8_t *, uint8_t *) { return false; }
#endif

#endif /* ARCH_NRF54L15 */
