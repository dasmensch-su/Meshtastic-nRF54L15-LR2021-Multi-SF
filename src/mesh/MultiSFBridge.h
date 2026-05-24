#pragma once

/* MultiSFBridge — Phase 6 of the Multi-SF branch.
 *
 * When the bridge receives a broadcast on a side detector, schedules additional
 * re-transmissions on every OTHER enabled SF (skipping main, which Meshtastic's
 * normal flooding already covers, and the originating SF since peers on that
 * SF already heard it directly). Lets peers on different SFs hear each other's
 * broadcasts through the bridge — closing the implicit-ACK loop too.
 *
 * Compile-gated behind MESHTASTIC_MULTI_SF_BRIDGE so the airtime amplification
 * is opt-in. Off by default in production builds. */

#include <stdint.h>
#include <stddef.h>

#include "concurrency/OSThread.h"

/* Sentinel value stamped into MeshPacket.next_hop on cross-SF fan-out clones.
 * Any Multi-SF bridge that receives a broadcast with this next_hop value skips
 * re-fanning it — prevents N² amplification when multiple bridges are in range.
 * Legacy (non Multi-SF) nodes treat next_hop=0xFF as "no preference", which is
 * benign. 1/256 collision rate with a legit NodeNum ending in 0xFF is accepted. */
#define MULTISF_BRIDGED_NEXT_HOP_SENTINEL 0xFFu

struct _meshtastic_MeshPacket;
typedef struct _meshtastic_MeshPacket meshtastic_MeshPacket;

class LR2021Interface;

class MultiSFBridge : public concurrency::OSThread
{
  public:
    explicit MultiSFBridge(LR2021Interface *iface);

    /* Called from RX path when a broadcast arrives on a side detector. Enqueues
     * a fan-out task that drains over the next several ticks (one TX per tick
     * to avoid back-to-back radio thrash). Safe to call from radio thread. */
    void scheduleFanout(const meshtastic_MeshPacket *p, uint8_t originatingSf);

    /* Called from startSend when relaying a unicast DM to a destination not in
     * NodeSFTracker. Fans the DM to all active SFs except the arrival SF (which
     * the normal relay path already covers). No portnum filtering — DMs are
     * always high-value. */
    void scheduleDmFanout(const meshtastic_MeshPacket *p, uint8_t arrivalSf);

  protected:
    int32_t runOnce() override;

  private:
    static constexpr size_t QUEUE_LEN = 4;

    struct Entry {
        meshtastic_MeshPacket *pkt;  /* packetPool-allocated; released when mask = 0 */
        uint8_t pendingMask;         /* bit (SF-5) for each SF still to send */
    };

    Entry queue[QUEUE_LEN] = {};
    LR2021Interface *radioIf;

    /* Packet-ID dedup ring — prevents re-fanning the same broadcast when we
     * hear it multiple times (e.g. once direct, once from another relay). */
    static constexpr size_t DEDUP_LEN = 16;
    struct DedupEntry {
        uint32_t pktId;
        uint32_t whenMs;  /* millis() at insertion */
    };
    DedupEntry dedup[DEDUP_LEN] = {};
    size_t dedupCursor = 0;

    /* Returns true if pktId was seen in the last DEDUP_TTL_MS, else records it
     * and returns false. */
    bool checkAndRecordDedup(uint32_t pktId);
};

extern MultiSFBridge *multiSFBridge;
