#include "MultiSFBridge.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "LR2021Interface.h"
#include "MeshTypes.h"
#include "Router.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

MultiSFBridge *multiSFBridge = nullptr;

/* Mutex protecting queue[] across radio-thread (scheduleFanout) and OSThread
 * scheduler (runOnce). Both contexts touch the same array. */
K_MUTEX_DEFINE(bridge_queue_mutex);

/* Portnum-aware fan-out policy.
 *
 * Cross-SF fan-out costs airtime (up to N-1 extra TXs per inbound broadcast).
 * Fanning EVERY broadcast type would flood the bridge in Position/Telemetry
 * traffic and negate the benefit. Instead we fan out only packet types that
 *   (a) are low-frequency,
 *   (b) are high-value for cross-SF reach.
 *
 * Allowlisted (always fan out):
 *   NODEINFO_APP    — discovery + public-key exchange. Essential for PKI DMs
 *                     across SFs; without this no cross-SF DMs are possible.
 *   ROUTING_APP     — routing ACKs / NAKs. Tiny, time-sensitive, rare.
 *   ADMIN_APP       — administrative messages; used sparingly.
 *   TRACEROUTE_APP  — user-initiated troubleshooting, low volume.
 *   KEY_VERIFICATION_APP — PKI verification, low volume.
 *
 * Denylist (never fan out):
 *   POSITION_APP, TELEMETRY_APP, ENVIRONMENTAL_MEASUREMENT_APP,
 *   NEIGHBORINFO_APP, TEXT_MESSAGE_APP, and everything else — chatty or
 *   acceptable to keep single-SF.
 *
 * If decode fails (channel mismatch / unknown PSK), we DON'T fan out.
 * Safer default; the bridge shouldn't amplify traffic it can't verify. */
static inline bool fanoutPortnumAllowed(uint32_t portnum)
{
    switch (portnum) {
    case meshtastic_PortNum_NODEINFO_APP:
    case meshtastic_PortNum_ROUTING_APP:
    case meshtastic_PortNum_ADMIN_APP:
    case meshtastic_PortNum_TRACEROUTE_APP:
    case meshtastic_PortNum_KEY_VERIFICATION_APP:
        return true;
    default:
        return false;
    }
}

/* Probe a received packet's portnum by trying to decrypt a throwaway clone.
 * Returns the portnum on success, or meshtastic_PortNum_UNKNOWN_APP (0) on
 * decode failure (channel mismatch / not our PSK / malformed). */
static uint32_t probePortnum(const meshtastic_MeshPacket *p)
{
    meshtastic_MeshPacket *probe = packetPool.allocCopy(*p);
    if (!probe) return meshtastic_PortNum_UNKNOWN_APP;

    /* perhapsDecode expects encrypted_tag (addReceiveMetadata's hook runs
     * before RadioLibInterface stamps this variant, so set it ourselves). */
    probe->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    uint32_t portnum = meshtastic_PortNum_UNKNOWN_APP;
    if (perhapsDecode(probe) == DecodeState::DECODE_SUCCESS) {
        portnum = probe->decoded.portnum;
    }
    packetPool.release(probe);
    return portnum;
}

static inline uint8_t sfBitFor(uint8_t sf)
{
    if (sf < 5 || sf > 12) return 0;
    return (uint8_t)(1u << (sf - 5));
}

static inline uint8_t bitToSf(uint8_t bit)
{
    for (uint8_t sf = 5; sf <= 12; sf++) {
        if (bit == (uint8_t)(1u << (sf - 5))) return sf;
    }
    return 0;
}

MultiSFBridge::MultiSFBridge(LR2021Interface *iface)
    : OSThread("MultiSFBridge"), radioIf(iface)
{
    setIntervalFromNow(1000);
    qprintk("MultiSFBridge: cross-SF flood relay enabled\n");
}

void MultiSFBridge::scheduleFanout(const meshtastic_MeshPacket *p, uint8_t originatingSf)
{
    if (!radioIf || !p) return;

    /* Phase 8: if another Multi-SF bridge already fanned this packet, don't
     * re-fan. Sentinel check prevents amplification when multiple bridges are
     * within radio range of each other. */
    if (p->next_hop == MULTISF_BRIDGED_NEXT_HOP_SENTINEL) {
        qprintk("MultiSFBridge: skip already-bridged id=0x%08x\n", (unsigned)p->id);
        return;
    }

    /* Skip if we've already fanned out this packet ID recently — prevents
     * re-fanning when the same broadcast reaches us more than once (direct +
     * via another relay, or repeats from the originator). */
    if (checkAndRecordDedup(p->id)) {
        qprintk("MultiSFBridge: dedup skip id=0x%08x\n", (unsigned)p->id);
        return;
    }

    /* Phase 9: portnum-aware fan-out policy. Decode a throwaway clone to
     * read the portnum; only fan out high-value, low-frequency packet types
     * (NodeInfo, Routing ACKs, Admin, Traceroute, KeyVerification). Position,
     * Telemetry, and Text broadcasts stay single-SF to save airtime. */
    uint32_t portnum = probePortnum(p);
    if (!fanoutPortnumAllowed(portnum)) {
        qprintk("MultiSFBridge: skip portnum=%u id=0x%08x (not allowlisted)\n",
               (unsigned)portnum, (unsigned)p->id);
        return;
    }
    qprintk("MultiSFBridge: portnum=%u OK for fan-out id=0x%08x\n",
           (unsigned)portnum, (unsigned)p->id);

    /* Build the SF mask: every active side SF + originating SF, minus main
     * (Meshtastic's normal flood already covers main SF). The originating SF
     * is INCLUDED so the originator hears an implicit-ACK rebroadcast — without
     * this, single-peer-on-an-SF deployments get max-retransmission errors. */
    uint8_t mainSf = radioIf->getMainSf();
    uint8_t mask = 0;
    for (uint8_t slot = 0; slot < 3; slot++) {
        uint8_t s = radioIf->getSideSf(slot);
        if (s == 0) continue;
        if (s == mainSf) continue;
        mask |= sfBitFor(s);
    }
    /* Always include originating SF (closes the implicit-ACK loop for the
     * sender). It will already be in the mask if it's one of our side SFs. */
    if (originatingSf != mainSf) {
        mask |= sfBitFor(originatingSf);
    }
    if (mask == 0) return;

    k_mutex_lock(&bridge_queue_mutex, K_FOREVER);
    bool placed = false;
    for (size_t i = 0; i < QUEUE_LEN; i++) {
        if (queue[i].pkt == nullptr) {
            queue[i].pkt = packetPool.allocCopy(*p);
            if (queue[i].pkt) {
                /* addReceiveMetadata runs BEFORE RadioLibInterface sets
                 * which_payload_variant = encrypted_tag, so our copy ends up
                 * with variant=0 (unset) and Router::send would reject it.
                 * Set it explicitly here — at this point the payload is the
                 * raw on-wire encrypted form. */
                queue[i].pkt->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
                queue[i].pendingMask = mask;
                placed = true;
            }
            break;
        }
    }
    k_mutex_unlock(&bridge_queue_mutex);

    if (!placed) {
        qprintk("MultiSFBridge: queue full, dropping fan-out for id 0x%08x\n", (unsigned)p->id);
        return;
    }

    qprintk("MultiSFBridge: queued fan-out id=0x%08x mask=0x%02x (rx on SF%u)\n",
           (unsigned)p->id, mask, originatingSf);
    setIntervalFromNow(50);
}

void MultiSFBridge::scheduleDmFanout(const meshtastic_MeshPacket *p, uint8_t arrivalSf)
{
    if (!radioIf || !p) return;

    if (p->next_hop == MULTISF_BRIDGED_NEXT_HOP_SENTINEL) {
        qprintk("MultiSFBridge: DM skip already-bridged id=0x%08x\n", (unsigned)p->id);
        return;
    }

    if (checkAndRecordDedup(p->id)) {
        qprintk("MultiSFBridge: DM dedup skip id=0x%08x\n", (unsigned)p->id);
        return;
    }

    /* Fan to ALL active SFs (main + sides) except the arrival SF, which the
     * normal relay already covers. We don't know which SF the destination is
     * on, so spray everywhere. */
    uint8_t mainSf = radioIf->getMainSf();
    uint8_t mask = 0;
    mask |= sfBitFor(mainSf);
    for (uint8_t slot = 0; slot < 3; slot++) {
        uint8_t s = radioIf->getSideSf(slot);
        if (s != 0) mask |= sfBitFor(s);
    }
    /* Remove the arrival SF — that copy goes out via normal relay. */
    mask &= ~sfBitFor(arrivalSf);
    if (mask == 0) return;

    k_mutex_lock(&bridge_queue_mutex, K_FOREVER);
    bool placed = false;
    for (size_t i = 0; i < QUEUE_LEN; i++) {
        if (queue[i].pkt == nullptr) {
            queue[i].pkt = packetPool.allocCopy(*p);
            if (queue[i].pkt) {
                queue[i].pkt->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
                queue[i].pendingMask = mask;
                placed = true;
            }
            break;
        }
    }
    k_mutex_unlock(&bridge_queue_mutex);

    if (!placed) {
        qprintk("MultiSFBridge: DM queue full, dropping fan-out for id 0x%08x\n", (unsigned)p->id);
        return;
    }

    qprintk("MultiSFBridge: DM fan-out id=0x%08x mask=0x%02x to=0x%08x (rx SF%u)\n",
           (unsigned)p->id, mask, (unsigned)p->to, arrivalSf);
    setIntervalFromNow(50);
}

int32_t MultiSFBridge::runOnce()
{
    if (!router || !radioIf) return 1000;

    k_mutex_lock(&bridge_queue_mutex, K_FOREVER);
    int sentSf = -1;
    uint32_t sentId = 0;

    for (size_t i = 0; i < QUEUE_LEN; i++) {
        if (queue[i].pkt == nullptr) continue;

        if (queue[i].pendingMask == 0) {
            /* All SFs sent — release the original. */
            packetPool.release(queue[i].pkt);
            queue[i].pkt = nullptr;
            continue;
        }

        /* Pop the lowest set bit. */
        uint8_t bit = queue[i].pendingMask & (uint8_t)(-(int8_t)queue[i].pendingMask);
        uint8_t sf = bitToSf(bit);
        queue[i].pendingMask &= (uint8_t)~bit;

        if (sf == 0) continue;

        /* Clone for sending — Router::send takes ownership and releases. */
        meshtastic_MeshPacket *clone = packetPool.allocCopy(*queue[i].pkt);
        if (!clone) {
            /* Out of pool — restore the bit and try again next tick. */
            queue[i].pendingMask |= bit;
            k_mutex_unlock(&bridge_queue_mutex);
            return 200;
        }

        /* Phase 8: mark this clone so other Multi-SF bridges don't re-fan it. */
        clone->next_hop = MULTISF_BRIDGED_NEXT_HOP_SENTINEL;

        sentId = clone->id;
        sentSf = sf;
        /* Bind the forced SF to THIS clone's pointer. ID alone isn't enough
         * because the FloodingRouter's normal-flood relay shares the original
         * packet ID — pointer-based binding ensures only our clone retargets. */
        radioIf->registerForcedTx(clone, sf);
        router->send(clone);
        break;  /* one fan-out per tick to space out TXs */
    }

    /* Release any entries that finished. */
    for (size_t i = 0; i < QUEUE_LEN; i++) {
        if (queue[i].pkt && queue[i].pendingMask == 0) {
            packetPool.release(queue[i].pkt);
            queue[i].pkt = nullptr;
        }
    }

    /* Anything still pending? */
    bool moreWork = false;
    for (size_t i = 0; i < QUEUE_LEN; i++) {
        if (queue[i].pkt) { moreWork = true; break; }
    }
    k_mutex_unlock(&bridge_queue_mutex);

    if (sentSf >= 0) {
        qprintk("MultiSFBridge: fan-out TX id=0x%08x SF%d\n", (unsigned)sentId, sentSf);
    }

    /* 500 ms between fan-outs lets the previous TX finish + CAD breathe.
     * Long sleep when queue empty. */
    return moreWork ? 500 : 5000;
}

bool MultiSFBridge::checkAndRecordDedup(uint32_t pktId)
{
    constexpr uint32_t DEDUP_TTL_MS = 30 * 1000;
    uint32_t nowMs = (uint32_t)k_uptime_get();

    for (size_t i = 0; i < DEDUP_LEN; i++) {
        if (dedup[i].pktId == pktId && (nowMs - dedup[i].whenMs) < DEDUP_TTL_MS) {
            return true;  /* recently seen */
        }
    }
    /* Record in the next slot (ring overwrite). */
    dedup[dedupCursor].pktId = pktId;
    dedup[dedupCursor].whenMs = nowMs;
    dedupCursor = (dedupCursor + 1) % DEDUP_LEN;
    return false;
}
