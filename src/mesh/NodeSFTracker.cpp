#include "NodeSFTracker.h"

#include <zephyr/sys/printk.h>

NodeSFTracker nodeSFTracker;

/* SF5..SF12 → bit 0..7. Out-of-range SFs are silently ignored. */
static inline uint8_t sfBit(uint8_t sf)
{
    if (sf < 5 || sf > 12) return 0;
    return (uint8_t)(1u << (sf - 5));
}

static inline int popcount8(uint8_t v)
{
    v = v - ((v >> 1) & 0x55);
    v = (v & 0x33) + ((v >> 2) & 0x33);
    return (v + (v >> 4)) & 0x0F;
}

void NodeSFTracker::update(NodeNum from, uint8_t sf, bool direct)
{
    if (from == 0 || sf == 0) return;
    uint8_t bit = sfBit(sf);
    if (bit == 0) return;

    /* Update in place if already present. */
    for (size_t i = 0; i < count; i++) {
        if (entries[i].node == from) {
            /* Never overwrite a direct observation with a relay-path one. */
            if (!direct && entries[i].direct) {
                entries[i].seq = nextSeq++;
                return;
            }
            uint8_t oldMask = entries[i].sfMask;
            if (direct) {
                entries[i].sfMask |= bit;
                entries[i].direct = 1;
            } else {
                /* Relay-path: replace the SF entirely (path may have changed)
                 * rather than accumulating bits from different relay paths. */
                entries[i].sfMask = bit;
            }
            entries[i].lastSf = sf;
            entries[i].seq = nextSeq++;
            if (entries[i].sfMask != oldMask) {
                int n = popcount8(entries[i].sfMask);
                qprintk("SFTrack  : 0x%08x %cSF%u (mask=0x%02x, %d SFs%s)\n",
                       (unsigned)from, direct ? '+' : '~', sf, entries[i].sfMask, n,
                       (n >= 2) ? " multi-SF" : "");
            }
            return;
        }
    }

    /* Append if room. */
    if (count < MAX_NUM_NODES) {
        entries[count].node = from;
        entries[count].lastSf = sf;
        entries[count].sfMask = bit;
        entries[count].peerHash = 0xFF;
        entries[count].direct = direct ? 1 : 0;
        entries[count].seq = nextSeq++;
        count++;
        qprintk("SFTrack  : 0x%08x added %sSF%u (size=%u)\n",
               (unsigned)from, direct ? "" : "relay-path ", sf, (unsigned)count);
        return;
    }

    /* Full — evict the entry with the smallest seq (oldest heard). */
    size_t oldest = 0;
    for (size_t i = 1; i < count; i++) {
        if (entries[i].seq < entries[oldest].seq) oldest = i;
    }
    entries[oldest].node = from;
    entries[oldest].lastSf = sf;
    entries[oldest].sfMask = bit;
    entries[oldest].peerHash = 0xFF;
    entries[oldest].direct = direct ? 1 : 0;
    entries[oldest].seq = nextSeq++;
}

void NodeSFTracker::setHash(NodeNum from, uint8_t channelHash)
{
    if (from == 0) return;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].node == from) {
            if (entries[i].peerHash != channelHash) {
                entries[i].peerHash = channelHash;
                qprintk("SFTrack  : 0x%08x peer-hash=0x%02x (override outgoing unicasts)\n",
                       (unsigned)from, channelHash);
            }
            return;
        }
    }
    /* Not tracked yet. Leave it alone — we only override outbound hash for
     * peers whose SF we also know (i.e. 1-hop neighbors). For multi-hop
     * peers the override is not meaningful since we can't reach them on a
     * matching channel-hash path anyway. */
}

uint8_t NodeSFTracker::getHash(NodeNum node) const
{
    if (node == 0) return 0xFF;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].node == node) return entries[i].peerHash;
    }
    return 0xFF;
}

uint8_t NodeSFTracker::lookup(NodeNum node) const
{
    if (node == 0) return 0;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].node == node) {
            int n = popcount8(entries[i].sfMask);
            if (n == 1) {
                /* Single SF — translate bit position back to SF value. */
                uint8_t m = entries[i].sfMask;
                for (uint8_t sf = 5; sf <= 12; sf++) {
                    if (m & (1u << (sf - 5))) return sf;
                }
                return 0; /* shouldn't happen */
            }
            /* n == 0 shouldn't happen for a present entry; n >= 2 → multi-SF peer. */
            return 0;
        }
    }
    return 0;
}

uint8_t NodeSFTracker::lastSeenSf(NodeNum node) const
{
    if (node == 0) return 0;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].node == node) return entries[i].lastSf;
    }
    return 0;
}

bool NodeSFTracker::isMultiSF(NodeNum node) const
{
    if (node == 0) return false;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].node == node) return popcount8(entries[i].sfMask) >= 2;
    }
    return false;
}

NodeNum NodeSFTracker::findByLastByte(uint8_t lastByte) const
{
    for (size_t i = 0; i < count; i++) {
        if (entries[i].node != 0 && (entries[i].node & 0xFFu) == lastByte) {
            return entries[i].node;
        }
    }
    return 0;
}
