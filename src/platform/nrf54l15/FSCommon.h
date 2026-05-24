/*
 * FSCommon.h — Zephyr nRF54L15 port.
 * Uses LittleFS via ZephyrFS wrapper for Arduino-compatible file I/O.
 *
 * This header is found first (platform dir is first in include path).
 * It defines FSCom/FSBegin, then includes the upstream FSCommon.h
 * which declares all the filesystem functions (fsInit, copyFile, etc.).
 */
#pragma once

#include <strings.h>  /* strcasecmp used by AdminModule */

#include "ZephyrFS.h"

#define FSCom zephyrFS
#define FSBegin() zephyrFS.begin()
