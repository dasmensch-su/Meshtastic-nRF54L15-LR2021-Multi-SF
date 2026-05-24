/* RadioLib.h — Minimal shim for nRF54L15 Zephyr port.
 * Shadows the real RadioLib.h (which includes all modules).
 * Only includes the LR2021 module and core components. */
#pragma once

#include "BuildOpt.h"
#include "TypeDef.h"
#include "Module.h"
#include "Hal.h"
#include "hal/Arduino/ArduinoHal.h"
#include "protocols/PhysicalLayer/PhysicalLayer.h"
#include "modules/LR2021/LR2021.h"
