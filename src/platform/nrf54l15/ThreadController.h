/*
 * ThreadController.h — Zephyr-compatible cooperative thread scheduler.
 *
 * API-compatible with the ArduinoThread ThreadController class used by
 * Meshtastic.  Manages a dynamic list of Thread* instances and runs any
 * that are due on each call to run().
 */
#pragma once

#include "Thread.h"
#include <algorithm>
#include <vector>

class ThreadController : public Thread
{
  public:
    ThreadController() : Thread("controller", 0) {}

    bool add(Thread *t)
    {
        threads_.push_back(t);
        return true;
    }

    bool remove(Thread *t)
    {
        auto it = std::find(threads_.begin(), threads_.end(), t);
        if (it == threads_.end()) return false;
        threads_.erase(it);
        return true;
    }

    /* Run all threads that are due now */
    void run() override
    {
        unsigned long now = millis();
        for (Thread *t : threads_) {
            if (t->shouldRun(now)) t->run();
        }
        runned();
    }

    /*
     * Returns the number of milliseconds until the next thread is due.
     * Used by the main loop to compute its sleep duration.
     */
    unsigned long getNextRun()
    {
        unsigned long now  = millis();
        unsigned long next = now + 1000UL; /* 1 s default if nothing is pending */
        for (Thread *t : threads_) {
            if (!t->enabled) continue;
            if (t->runASAP)  return 0;
            if (t->_cached_next_run < next) next = t->_cached_next_run;
        }
        return (next > now) ? (next - now) : 0;
    }

  private:
    std::vector<Thread *> threads_;
};
