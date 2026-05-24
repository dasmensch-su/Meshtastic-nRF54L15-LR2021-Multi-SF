/*
 * OLEDDisplayUi.cpp — Frame/overlay manager for Meshtastic on Zephyr.
 *
 * Reimplements ThingPulse OLEDDisplayUi: manages frame callbacks,
 * overlay callbacks, frame transitions, and the display update loop.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "OLEDDisplayUi.h"
#include "arduino_compat.h"   /* millis() */

OLEDDisplayUi::OLEDDisplayUi(OLEDDisplay *display)
    : display(display),
      frameFunctions(nullptr), frameCount(0),
      overlayFunctions(nullptr), overlayCount(0),
      targetFPS(30),
      ticksPerFrame(66),       /* ~15 frames at 30fps = 2s per frame */
      ticksPerTransition(15),  /* ~500ms transition */
      autoTransition(false),
      frameAnimationDirection(SLIDE_LEFT),
      indicatorsEnabled(false),
      indicatorPosition(BOTTOM),
      indicatorDirection(LEFT_RIGHT),
      frameNotificationCallback(nullptr),
      loadingDrawFunction(nullptr)
{
}

void OLEDDisplayUi::init()
{
    display->init();
    state.lastUpdate = millis();
    state.ticksSinceLastStateSwitch = 0;
    state.ticks = 0;
    state.frameState = FIXED;
    state.currentFrame = 0;
}

/* ---- FPS ---- */

void OLEDDisplayUi::setTargetFPS(uint8_t fps)
{
    targetFPS = fps > 0 ? fps : 1;
}

/* ---- Auto-transition ---- */

void OLEDDisplayUi::enableAutoTransition() { autoTransition = true; }
void OLEDDisplayUi::disableAutoTransition() { autoTransition = false; }
void OLEDDisplayUi::setAutoTransitionForwards() { state.frameTransitionDirection = 1; }
void OLEDDisplayUi::setAutoTransitionBackwards() { state.frameTransitionDirection = -1; }
void OLEDDisplayUi::setTimePerFrame(uint16_t time) { ticksPerFrame = time; }
void OLEDDisplayUi::setTimePerTransition(uint16_t time) { ticksPerTransition = time; }

/* ---- Indicators ---- */

void OLEDDisplayUi::enableIndicator() { indicatorsEnabled = true; state.isIndicatorDrawn = true; }
void OLEDDisplayUi::disableIndicator() { indicatorsEnabled = false; state.isIndicatorDrawn = false; }
void OLEDDisplayUi::enableAllIndicators() { indicatorsEnabled = true; state.isIndicatorDrawn = true; }
void OLEDDisplayUi::disableAllIndicators() { indicatorsEnabled = false; state.isIndicatorDrawn = false; }
void OLEDDisplayUi::setIndicatorPosition(IndicatorPosition pos) { indicatorPosition = pos; }
void OLEDDisplayUi::setIndicatorDirection(IndicatorDirection dir) { indicatorDirection = dir; }
void OLEDDisplayUi::setActiveSymbol(const uint8_t *symbol) { (void)symbol; }
void OLEDDisplayUi::setInactiveSymbol(const uint8_t *symbol) { (void)symbol; }

/* ---- Notifications ---- */

bool OLEDDisplayUi::addFrameToNotifications(uint32_t frameToAdd, bool force)
{
    (void)force;
    for (auto &f : state.notifyingFrames) {
        if (f == frameToAdd) return false;
    }
    state.notifyingFrames.push_back(frameToAdd);
    return true;
}

bool OLEDDisplayUi::removeFrameFromNotifications(uint32_t frameToRemove)
{
    for (auto it = state.notifyingFrames.begin(); it != state.notifyingFrames.end(); ++it) {
        if (*it == frameToRemove) {
            state.notifyingFrames.erase(it);
            return true;
        }
    }
    return false;
}

void OLEDDisplayUi::setFrameNotificationCallback(FrameNotificationCallback *func)
{
    frameNotificationCallback = func;
}

uint32_t OLEDDisplayUi::getFirstNotifyingFrame()
{
    if (state.notifyingFrames.empty()) return 0;
    return state.notifyingFrames.front();
}

/* ---- Frame management ---- */

void OLEDDisplayUi::setFrameAnimation(AnimationDirection dir)
{
    frameAnimationDirection = dir;
}

void OLEDDisplayUi::setFrames(FrameCallback *fns, uint8_t count)
{
    frameFunctions = fns;
    frameCount = count;
}

void OLEDDisplayUi::setOverlays(OverlayCallback *fns, uint8_t count)
{
    overlayFunctions = fns;
    overlayCount = count;
}

/* ---- Loading ---- */

void OLEDDisplayUi::setLoadingDrawFunction(LoadingDrawFunction fn) { loadingDrawFunction = fn; }

void OLEDDisplayUi::runLoadingProcess(LoadingStage *stages, uint8_t stagesCount)
{
    for (uint8_t i = 0; i < stagesCount; i++) {
        if (stages[i].callback) stages[i].callback();
        if (loadingDrawFunction) {
            display->clear();
            loadingDrawFunction(display, &stages[i], ((uint8_t)(i + 1) * 100) / stagesCount);
            display->display();
        }
    }
}

/* ---- Navigation ---- */

void OLEDDisplayUi::nextFrame()
{
    if (frameCount == 0) return;
    state.ticksSinceLastStateSwitch = 0;
    state.frameState = IN_TRANSITION;
    state.transitionFrameTarget = (state.currentFrame + 1) % frameCount;
    state.transitionFrameRelationship = TransitionRelationship_INCOMING;
}

void OLEDDisplayUi::previousFrame()
{
    if (frameCount == 0) return;
    state.ticksSinceLastStateSwitch = 0;
    state.frameState = IN_TRANSITION;
    state.transitionFrameTarget = (state.currentFrame + frameCount - 1) % frameCount;
    state.transitionFrameRelationship = TransitionRelationship_INCOMING;
}

void OLEDDisplayUi::switchToFrame(uint8_t frame)
{
    if (frame >= frameCount) return;
    state.ticksSinceLastStateSwitch = 0;
    state.currentFrame = frame;
    state.frameState = FIXED;
}

void OLEDDisplayUi::transitionToFrame(uint8_t frame)
{
    if (frame >= frameCount || frame == state.currentFrame) return;
    state.ticksSinceLastStateSwitch = 0;
    state.frameState = IN_TRANSITION;
    state.transitionFrameTarget = frame;
    state.transitionFrameRelationship = TransitionRelationship_INCOMING;
}

/* ---- State query ---- */

OLEDDisplayUiState *OLEDDisplayUi::getUiState()
{
    return &state;
}

/* ---- Main update loop ---- */

int16_t OLEDDisplayUi::update()
{
    uint32_t now = millis();
    uint32_t frameInterval = 1000 / targetFPS;

    if (now - state.lastUpdate < frameInterval) {
        return (int16_t)(frameInterval - (now - state.lastUpdate));
    }

    state.lastUpdate = now;
    state.ticks++;

    display->clear();

    drawFrame();
    drawOverlays();

    display->display();

    tick();

    return 0;
}

void OLEDDisplayUi::drawFrame()
{
    if (!frameFunctions || frameCount == 0) return;

    if (state.frameState == IN_TRANSITION && ticksPerTransition > 0) {
        /* Animate transition */
        state.ticksSinceLastStateSwitch++;
        float progress = (float)state.ticksSinceLastStateSwitch / (float)ticksPerTransition;
        if (progress >= 1.0f) progress = 1.0f;

        int16_t w = display->getWidth();
        int16_t h = display->getHeight();
        int16_t offset = 0;

        switch (frameAnimationDirection) {
        case SLIDE_LEFT:  offset = (int16_t)(-w * progress); break;
        case SLIDE_RIGHT: offset = (int16_t)(w * progress); break;
        case SLIDE_UP:    offset = (int16_t)(-h * progress); break;
        case SLIDE_DOWN:  offset = (int16_t)(h * progress); break;
        }

        /* Draw outgoing frame */
        if (state.currentFrame < frameCount && frameFunctions[state.currentFrame]) {
            state.transitionFrameRelationship = TransitionRelationship_OUTGOING;
            if (frameAnimationDirection == SLIDE_LEFT || frameAnimationDirection == SLIDE_RIGHT) {
                frameFunctions[state.currentFrame](display, &state, offset, 0);
            } else {
                frameFunctions[state.currentFrame](display, &state, 0, offset);
            }
        }

        /* Draw incoming frame */
        if (state.transitionFrameTarget < frameCount && frameFunctions[state.transitionFrameTarget]) {
            state.transitionFrameRelationship = TransitionRelationship_INCOMING;
            if (frameAnimationDirection == SLIDE_LEFT || frameAnimationDirection == SLIDE_RIGHT) {
                int16_t inOffset = offset + (offset < 0 ? w : -w);
                frameFunctions[state.transitionFrameTarget](display, &state, inOffset, 0);
            } else {
                int16_t inOffset = offset + (offset < 0 ? h : -h);
                frameFunctions[state.transitionFrameTarget](display, &state, 0, inOffset);
            }
        }

        if (progress >= 1.0f) {
            state.currentFrame = state.transitionFrameTarget;
            state.frameState = FIXED;
            state.ticksSinceLastStateSwitch = 0;
            state.transitionFrameRelationship = TransitionRelationship_NONE;
        }
    } else if (state.frameState == IN_TRANSITION && ticksPerTransition == 0) {
        /* Instant transition */
        state.currentFrame = state.transitionFrameTarget;
        state.frameState = FIXED;
        state.ticksSinceLastStateSwitch = 0;
        state.transitionFrameRelationship = TransitionRelationship_NONE;
        /* Fall through to draw the new frame */
        if (state.currentFrame < frameCount && frameFunctions[state.currentFrame]) {
            frameFunctions[state.currentFrame](display, &state, 0, 0);
        }
    } else {
        /* FIXED state — draw current frame */
        if (state.currentFrame < frameCount && frameFunctions[state.currentFrame]) {
            frameFunctions[state.currentFrame](display, &state, 0, 0);
        }
    }
}

void OLEDDisplayUi::drawOverlays()
{
    if (!overlayFunctions) return;
    for (uint8_t i = 0; i < overlayCount; i++) {
        if (overlayFunctions[i]) {
            overlayFunctions[i](display, &state);
        }
    }
}

void OLEDDisplayUi::tick()
{
    if (autoTransition && state.frameState == FIXED && frameCount > 1) {
        state.ticksSinceLastStateSwitch++;
        if (state.ticksSinceLastStateSwitch >= ticksPerFrame) {
            if (state.frameTransitionDirection > 0) {
                nextFrame();
            } else {
                previousFrame();
            }
        }
    }
}
