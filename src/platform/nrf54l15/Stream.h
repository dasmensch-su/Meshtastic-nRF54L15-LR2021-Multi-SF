#pragma once
/*
 * Minimal Arduino-compat Stream.h for the nRF54L15 Meshtastic port.
 *
 * StreamAPI (in src/mesh/StreamAPI.h) holds a Stream* and pulls bytes
 * via available()/read(), and pushes bytes via write(buf,len)/flush().
 * That is the entire surface used by SerialConsole through StreamAPI.
 *
 * Stream extends Print  the read side is added here, the write side
 * comes via inheritance. _ZephyrSerial in Arduino.h derives from
 * Stream so it is a valid argument for `new SerialConsole()`.
 *
 * This is deliberately a small header. We do NOT port Arduinos
 * readBytes/setTimeout/etc.  StreamAPI already handles its own polling
 * cadence and timeout via OSThread, so a synchronous read() loop driven
 * by available() is enough.
 */

#include "Print.h"

class Stream : public Print
{
  public:
    virtual int available() = 0;   /* bytes ready to read; 0 = none */
    virtual int read()      = 0;   /* return next byte, or -1 if none */
    virtual int peek()      = 0;   /* like read() but does not consume */
};
