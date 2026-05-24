/*
 * main.h — nRF54L15 Zephyr platform override for src/main.h.
 *
 * The global src/main.h pulls in Arduino, Screen, BLE, I2C, GPS and
 * a dozen other subsystems.  For the nRF54L15 Zephyr port none of
 * those are compiled, so this file provides only the declarations that
 * files we actually compile (e.g. NotifiedWorkerThread.cpp, Router.cpp)
 * need from main.h.
 *
 * Because src/platform/nrf54l15 is listed first in CMakeLists
 * target_include_directories, this file shadows src/main.h for all
 * translation units in the Zephyr build.
 */
#pragma once

#ifdef ARCH_NRF54L15

#include "Arduino.h"
#include "memGet.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Globals declared in src/main.h that are referenced by mesh files   */
/* ------------------------------------------------------------------ */

/* These are defined in src/platform/nrf54l15/main-nrf54l15.cpp       */
extern uint32_t rebootAtMsec;
extern uint32_t shutdownAtMsec;
extern bool     runASAP;
extern uint32_t serialSinceMsec;
extern bool     suppressRebootBanner;
extern bool     canSleep;

/* Device name helper */
const char *getDeviceName();

/* Screen pointer — created in main-nrf54l15.cpp when HAS_SCREEN=1 */
#include "graphics/Screen.h"
extern graphics::Screen *screen;

/* USB powered flag */
extern bool isUSBPowered;

/* Time last powered */
extern uint32_t timeLastPowered;

/* I2C scan results — no real I2C, everything is ADDRESS_NONE */
#include "detect/ScanI2C.h"
extern ScanI2C::DeviceAddress rtc_found;
extern ScanI2C::DeviceAddress screen_found;
extern ScanI2C::DeviceAddress cardkb_found;
extern bool kb_found;
extern bool osk_found;

/* Accelerometer thread — no motion sensor, minimal stub for MenuHandler */
namespace concurrency {
class AccelerometerThread {
public:
    void setInterval(uint32_t) {}
    void calibrate(int = 0) {}
};
}
extern concurrency::AccelerometerThread *accelerometerThread;

/* GPS beep — provided by real buzz.cpp (tone output is no-op) */

/* PowerStatus — needed by PositionModule's allocAtakPli */
#include "PowerStatus.h"

/* getDeviceMetadata — needed by AdminModule */
#include "mesh/generated/meshtastic/deviceonly.pb.h"
meshtastic_DeviceMetadata getDeviceMetadata();

/* BLE logging pause flag — needed by PhoneAPI.cpp */
extern bool pauseBluetoothLogging;

#endif /* ARCH_NRF54L15 */
