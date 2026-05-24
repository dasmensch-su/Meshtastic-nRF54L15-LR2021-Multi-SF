/*
 * OLEDDisplayUi.h — Zephyr-native reimplementation of ThingPulse OLEDDisplayUi.
 *
 * Frame/overlay manager for the Meshtastic display UI.
 * Manages a set of frame callbacks and overlay callbacks, handles
 * frame transitions, and drives the display update loop.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include "OLEDDisplay.h"
#include <stdint.h>
#include <functional>
#include <vector>

/* ---- Enums ---- */

enum AnimationDirection { SLIDE_UP, SLIDE_DOWN, SLIDE_LEFT, SLIDE_RIGHT };
enum IndicatorPosition { TOP, RIGHT, BOTTOM, LEFT };
enum IndicatorDirection { LEFT_RIGHT, RIGHT_LEFT };
enum FrameState { IN_TRANSITION, FIXED };
enum TransitionRelationship {
    TransitionRelationship_NONE,
    TransitionRelationship_INCOMING,
    TransitionRelationship_OUTGOING
};

/* ---- State struct ---- */

struct OLEDDisplayUiState {
    uint64_t lastUpdate = 0;
    uint16_t ticksSinceLastStateSwitch = 0;
    uint16_t ticks = 0;
    FrameState frameState = FIXED;
    uint8_t currentFrame = 0;
    uint8_t transitionFrameTarget = 0;
    TransitionRelationship transitionFrameRelationship = TransitionRelationship_NONE;
    std::vector<uint32_t> notifyingFrames;
    bool isIndicatorDrawn = true;
    int8_t frameTransitionDirection = 1;
    bool manualControl = false;
    void *userData = nullptr;
};

/* ---- Callback types ---- */

typedef std::function<void(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)> FrameCallback;
typedef void (*OverlayCallback)(OLEDDisplay *display, OLEDDisplayUiState *state);

struct LoadingStage {
    const char *process;
    void (*callback)();
};

typedef void (*LoadingDrawFunction)(OLEDDisplay *display, LoadingStage *stage, uint8_t progress);
typedef void (*FrameNotificationCallback)(uint32_t frameNumber, void *ui);

/* ---- OLEDDisplayUi class ---- */

class OLEDDisplayUi {
public:
    OLEDDisplayUi(OLEDDisplay *display);

    /* Initialization */
    void init();

    /* FPS */
    void setTargetFPS(uint8_t fps);

    /* Auto-transition */
    void enableAutoTransition();
    void disableAutoTransition();
    void setAutoTransitionForwards();
    void setAutoTransitionBackwards();
    void setTimePerFrame(uint16_t time);
    void setTimePerTransition(uint16_t time);

    /* Indicators */
    void enableIndicator();
    void disableIndicator();
    void enableAllIndicators();
    void disableAllIndicators();
    void setIndicatorPosition(IndicatorPosition pos);
    void setIndicatorDirection(IndicatorDirection dir);
    void setActiveSymbol(const uint8_t *symbol);
    void setInactiveSymbol(const uint8_t *symbol);

    /* Notifications */
    bool addFrameToNotifications(uint32_t frameToAdd, bool force = false);
    bool removeFrameFromNotifications(uint32_t frameToRemove);
    void setFrameNotificationCallback(FrameNotificationCallback *func);
    uint32_t getFirstNotifyingFrame();

    /* Frame management */
    void setFrameAnimation(AnimationDirection dir);
    void setFrames(FrameCallback *frameFunctions, uint8_t frameCount);
    void setOverlays(OverlayCallback *overlayFunctions, uint8_t overlayCount);

    /* Loading */
    void setLoadingDrawFunction(LoadingDrawFunction fn);
    void runLoadingProcess(LoadingStage *stages, uint8_t stagesCount);

    /* Navigation */
    void nextFrame();
    void previousFrame();
    void switchToFrame(uint8_t frame);
    void transitionToFrame(uint8_t frame);

    /* State */
    OLEDDisplayUiState *getUiState();

    /* Main update — call from loop. Returns ms until next frame. */
    int16_t update();

private:
    OLEDDisplay *display;
    OLEDDisplayUiState state;

    FrameCallback *frameFunctions;
    uint8_t frameCount;

    OverlayCallback *overlayFunctions;
    uint8_t overlayCount;

    uint8_t targetFPS;
    uint16_t ticksPerFrame;
    uint16_t ticksPerTransition;
    bool autoTransition;

    AnimationDirection frameAnimationDirection;
    bool indicatorsEnabled;
    IndicatorPosition indicatorPosition;
    IndicatorDirection indicatorDirection;
    FrameNotificationCallback *frameNotificationCallback;
    LoadingDrawFunction loadingDrawFunction;

    void drawFrame();
    void drawOverlays();
    void tick();
};
