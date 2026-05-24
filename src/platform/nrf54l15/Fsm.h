/*
 * Fsm.h — Working finite state machine for Meshtastic PowerFSM on Zephyr.
 *
 * Reimplements the arduino-fsm library (https://github.com/jonblack/arduino-fsm)
 * used by Meshtastic's PowerFSM. Supports event-triggered transitions,
 * timed transitions, and on_enter/on_state/on_exit callbacks.
 *
 * Timed transitions use millis() and are checked in run_machine().
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include "arduino_compat.h" /* millis() */

#define MAX_TRANSITIONS 64
#define MAX_TIMED_TRANSITIONS 8

class State {
public:
    State(void (*on_enter)() = nullptr,
          void (*on_state)() = nullptr,
          void (*on_exit)() = nullptr,
          const char *name = "")
        : on_enter(on_enter), on_state(on_state), on_exit(on_exit), name(name) {}

    void (*on_enter)();
    void (*on_state)();
    void (*on_exit)();
    const char *name;
};

class Fsm {
public:
    Fsm(State *initial)
        : current(initial), num_transitions(0), num_timed(0), initialized(false) {}

    void add_transition(State *from, State *to, int event,
                        void (*action)(), const char *description = "")
    {
        if (num_transitions >= MAX_TRANSITIONS) return;
        transitions[num_transitions++] = {from, to, event, action, description};
    }

    void add_timed_transition(State *from, State *to, unsigned long ms,
                              void (*action)(), const char *description = "")
    {
        if (num_timed >= MAX_TIMED_TRANSITIONS) return;
        timed[num_timed++] = {from, to, ms, action, description, 0, false};
    }

    void trigger(int event)
    {
        for (int i = 0; i < num_transitions; i++) {
            if (transitions[i].from == current && transitions[i].event == event) {
                transition(transitions[i].to, transitions[i].action);
                return;
            }
        }
    }

    void run_machine()
    {
        /* On first call, execute the initial state's on_enter */
        if (!initialized) {
            initialized = true;
            if (current && current->on_enter)
                current->on_enter();
            resetTimedTransitions();
        }

        /* Check timed transitions */
        unsigned long now = millis();
        for (int i = 0; i < num_timed; i++) {
            if (timed[i].from == current && timed[i].active) {
                if (now - timed[i].start >= timed[i].ms) {
                    transition(timed[i].to, timed[i].action);
                    return; /* state changed, re-check on next call */
                }
            }
        }

        /* Call current state's on_state (idle) callback */
        if (current && current->on_state)
            current->on_state();
    }

    State *get_current_state() { return current; }
    State *getState() { return current; }

private:
    struct Transition {
        State *from;
        State *to;
        int event;
        void (*action)();
        const char *description;
    };

    struct TimedTransition {
        State *from;
        State *to;
        unsigned long ms;
        void (*action)();
        const char *description;
        unsigned long start;
        bool active;
    };

    void transition(State *to, void (*action)())
    {
        if (current && current->on_exit)
            current->on_exit();
        if (action)
            action();
        current = to;
        if (current && current->on_enter)
            current->on_enter();
        resetTimedTransitions();
    }

    void resetTimedTransitions()
    {
        unsigned long now = millis();
        for (int i = 0; i < num_timed; i++) {
            if (timed[i].from == current) {
                timed[i].start = now;
                timed[i].active = true;
            } else {
                timed[i].active = false;
            }
        }
    }

    State *current;
    Transition transitions[MAX_TRANSITIONS];
    int num_transitions;
    TimedTransition timed[MAX_TIMED_TRANSITIONS];
    int num_timed;
    bool initialized;
};
