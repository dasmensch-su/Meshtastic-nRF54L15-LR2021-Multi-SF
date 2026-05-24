/*
 * Thread.h — Zephyr-compatible implementation of the ArduinoThread Thread class.
 *
 * Meshtastic's cooperative scheduler (OSThread / NotifiedWorkerThread) pulls
 * Thread and ThreadController from the ArduinoThread PlatformIO library.  That
 * library is not available in the Zephyr build, so we provide an API-compatible
 * replacement here.
 *
 * This is a cooperative scheduling primitive — NOT a real OS thread.  All tasks
 * run on the same Zephyr thread (the one calling ThreadController::run()) and
 * are multiplexed by the interval/shouldRun logic.
 */
#pragma once

#include "arduino_compat.h"
#include <stdint.h>
#include <string>

class Thread
{
  public:
    std::string ThreadName;
    bool        enabled   = true;
    bool        runASAP   = false;  /* set by NotifiedWorkerThread::notifyCommon */
    uint32_t    interval  = 0;      /* run period in ms; 0 = run every cycle */

    Thread(const char *name = nullptr, uint32_t period = 0)
        : ThreadName(name ? name : ""), interval(period)
    {
        _cached_next_run = millis() + period;
    }

    virtual ~Thread() {}

    virtual bool shouldRun(unsigned long time)
    {
        if (!enabled)  return false;
        if (runASAP)   return true;
        return (long)(time - _cached_next_run) >= 0;
    }

    /* Record that we just ran and compute next wake time */
    void runned()
    {
        _cached_next_run = millis() + interval;
        runASAP = false;
    }

    void setInterval(uint32_t t)
    {
        interval = t;
        runned();
    }

    /* Override in subclasses.  Should call runned() when appropriate. */
    virtual void run() = 0;

  protected:
    friend class ThreadController;
    unsigned long _cached_next_run = 0;
};
