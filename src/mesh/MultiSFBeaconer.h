#pragma once

/* MultiSFBeaconer — Phase 4 of the Multi-SF branch.
 *
 * Periodically emits a NodeInfo broadcast on each SF in our rotation
 * [main, side1, side2, side3] so that peers on any of those SFs see us as
 * a normal neighbor. Preserves backward compatibility: each peer just sees
 * a Meshtastic NodeInfo broadcast on its own SF at a normal cadence. */

#include <stdint.h>

#include "concurrency/OSThread.h"

class LR2021Interface;

class MultiSFBeaconer : public concurrency::OSThread
{
  public:
    explicit MultiSFBeaconer(LR2021Interface *iface);

  protected:
    int32_t runOnce() override;

  private:
    /* Rebuild the rotation list from the current main/side SF configuration. */
    void refreshRotation();

    /* Send our NodeInfo directly, bypassing NodeInfoModule's throttle. */
    void sendNodeInfoDirect();

    LR2021Interface *radioIf;
    uint8_t rotation[4] = {0, 0, 0, 0};  /* main + up to 3 sides */
    uint8_t rotationLen = 0;
    uint8_t cursor = 0;
    uint8_t bootBurstRemaining = 0;
};

extern MultiSFBeaconer *multiSFBeaconer;
