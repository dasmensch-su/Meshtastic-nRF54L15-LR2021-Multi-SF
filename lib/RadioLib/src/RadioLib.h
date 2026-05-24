#if !defined(_RADIOLIB_H)
#define _RADIOLIB_H

/*!
  \mainpage RadioLib Documentation

  Universal wireless communication library for embedded devices.

  \par Currently Supported Wireless Modules and Protocols
  - CC1101 FSK module
  - LLCC68 LoRa/FSK module
  - LR11x0 LoRa/FSK/LR-FHSS module
  - nRF24 FSK module
  - RF69 FSK module
  - RFM2x FSK module
  - Si443x FSK module
  - SX126x LoRa/FSK module
  - SX127x LoRa/FSK module
  - SX128x LoRa/GFSK/BLE/FLRC module
  - SX1231 FSK module
  - PhysicalLayer protocols
    - RTTY (RTTYClient)
    - Morse Code (MorseClient)
    - AX.25 (AX25Client)
    - SSTV (SSTVClient)
    - Hellschreiber (HellClient)
    - 4-FSK (FSK4Client)
    - APRS (APRSClient)
    - POCSAG (PagerClient)
    - LoRaWAN (LoRaWANNode)

  \par Quick Links
  Documentation for most common methods can be found in its reference page (see the list above).\n
  Some methods (mainly configuration) are also overridden in derived classes, such as SX1272, SX1278, RFM96 etc. for SX127x.\n
  \ref status_codes have their own page.\n
  Some modules implement methods of one or more compatibility layers, loosely based on the ISO/OSI model:
  - PhysicalLayer - FSK and LoRa radio modules

  \see https://github.com/jgromes/RadioLib
  \see https://jgromes.github.io/RadioLib/coverage/src/index.html

  \copyright  Copyright (c) 2019 Jan Gromes
*/

#include "TypeDef.h"
#include "Module.h"

#include "Hal.h"
#if defined(RADIOLIB_BUILD_ARDUINO)
#include "hal/Arduino/ArduinoHal.h"
#endif


// warnings are printed in this file since BuildOpt.h is compiled in multiple places

// check God mode
#if RADIOLIB_GODMODE
  #warning "God mode active, I hope it was intentional. Buckle up, lads."
#endif

// print debug info
#if RADIOLIB_DEBUG
  #pragma message(RADIOLIB_INFO)
#endif

// check unknown/unsupported platform
#if defined(RADIOLIB_UNKNOWN_PLATFORM)
  #warning "RadioLib might not be compatible with this Arduino board - check supported platforms at https://github.com/jgromes/RadioLib!"
#endif

// print warning for low-end platforms
#if defined(RADIOLIB_LOWEND_PLATFORM)
  #warning "Low-end platform detected, stability issues are likely!"
#endif

/* Only include modules that are not excluded */
#if !RADIOLIB_EXCLUDE_CC1101
#include "modules/CC1101/CC1101.h"
#endif
#if !RADIOLIB_EXCLUDE_LLCC68
#include "modules/LLCC68/LLCC68.h"
#endif
#if !RADIOLIB_EXCLUDE_LR11X0
#include "modules/LR11x0/LR1110.h"
#include "modules/LR11x0/LR1120.h"
#include "modules/LR11x0/LR1121.h"
#endif
#if !RADIOLIB_EXCLUDE_LR2021
#include "modules/LR2021/LR2021.h"
#endif
#if !RADIOLIB_EXCLUDE_NRF24
#include "modules/nRF24/nRF24.h"
#endif
#if !RADIOLIB_EXCLUDE_RF69
#include "modules/RF69/RF69.h"
#endif
#if !RADIOLIB_EXCLUDE_RFM2x
#include "modules/RFM2x/RFM22.h"
#include "modules/RFM2x/RFM23.h"
#endif
#if !RADIOLIB_EXCLUDE_SI443X
#include "modules/Si443x/Si4430.h"
#include "modules/Si443x/Si4431.h"
#include "modules/Si443x/Si4432.h"
#endif
#if !RADIOLIB_EXCLUDE_SX123X
#include "modules/SX123x/SX1231.h"
#include "modules/SX123x/SX1233.h"
#endif
#if !RADIOLIB_EXCLUDE_SX126X
#include "modules/SX126x/SX1261.h"
#include "modules/SX126x/SX1262.h"
#include "modules/SX126x/SX1268.h"
#endif
#if !RADIOLIB_EXCLUDE_STM32WLX
#include "modules/SX126x/STM32WLx.h"
#endif
#if !RADIOLIB_EXCLUDE_SX127X
#include "modules/SX127x/SX1272.h"
#include "modules/SX127x/SX1273.h"
#include "modules/SX127x/SX1276.h"
#include "modules/SX127x/SX1277.h"
#include "modules/SX127x/SX1278.h"
#include "modules/SX127x/SX1279.h"
#endif
#if !RADIOLIB_EXCLUDE_SX128X
#include "modules/SX128x/SX1280.h"
#include "modules/SX128x/SX1281.h"
#include "modules/SX128x/SX1282.h"
#endif

// physical layer protocols
#include "protocols/PhysicalLayer/PhysicalLayer.h"
#if !RADIOLIB_EXCLUDE_AFSK
#include "protocols/AFSK/AFSK.h"
#endif
#if !RADIOLIB_EXCLUDE_AX25
#include "protocols/AX25/AX25.h"
#endif
#if !RADIOLIB_EXCLUDE_HELLSCHREIBER
#include "protocols/Hellschreiber/Hellschreiber.h"
#endif
#if !RADIOLIB_EXCLUDE_MORSE
#include "protocols/Morse/Morse.h"
#endif
#if !RADIOLIB_EXCLUDE_PAGER
#include "protocols/Pager/Pager.h"
#endif
#if !RADIOLIB_EXCLUDE_RTTY
#include "protocols/RTTY/RTTY.h"
#endif
#if !RADIOLIB_EXCLUDE_SSTV
#include "protocols/SSTV/SSTV.h"
#endif
#if !RADIOLIB_EXCLUDE_FSK4
#include "protocols/FSK4/FSK4.h"
#endif
#if !RADIOLIB_EXCLUDE_APRS
#include "protocols/APRS/APRS.h"
#endif
#include "protocols/ExternalRadio/ExternalRadio.h"
#include "protocols/Print/Print.h"
#include "protocols/BellModem/BellModem.h"
#include "protocols/LoRaWAN/LoRaWAN.h"
#include "protocols/ADSB/ADSB.h"

// utilities
#include "utils/CRC.h"
#include "utils/Cryptography.h"

#endif
