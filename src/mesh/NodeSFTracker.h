#pragma once

/* NodeSFTracker — RAM-only sidecar map of NodeNum → last-heard spreading factor.
 *
 * Used by the Multi-SF branch to remember which SF each peer last transmitted on,
 * so that unicast replies can be sent back on the peer's SF. Not persisted across
 * reboots; repopulates as peers are heard. Backward-compatible: nothing is added
 * to NodeDB or to the over-the-air protocol. */

#include <stddef.h>
#include <stdint.h>

#include "MeshTypes.h"           // NodeNum
#include "mesh-pb-constants.h"   // MAX_NUM_NODES

class NodeSFTracker
{
  public:
    /* Update (or insert) the last-heard SF for `from`. Silently drops updates
     * with sf==0 or from==0. Evicts the oldest entry when full.
     *
     * `direct=true`: heard this node directly (hop_start==hop_limit). The SF
     * is genuinely the node's TX SF. Always overwrites.
     *
     * `direct=false`: relay-path observation. The SF is the last relay's SF,
     * not necessarily the node's own SF — but it represents a working path to
     * reach the node. Only writes if no entry exists or the existing entry is
     * also relay-path (never overwrites a direct observation). */
    void update(NodeNum from, uint8_t sf, bool direct = true);

    /* Returns the SF to use when sending a unicast to `node`:
     *   0 if unknown                → caller uses main SF
     *   single heard SF              → that SF (retarget to match peer)
     *   multiple heard SFs           → 0 (peer is a multi-SF bridge, main SF
     *                                     is safest — they listen on many)
     * Only direct neighbors are recorded, so this lookup is meaningful only
     * for unicasts destined to a 1-hop peer (or the relay resolved via
     * findByLastByte for multi-hop destinations). */
    uint8_t lookup(NodeNum node) const;

    /* Find a direct-neighbor NodeNum whose low byte matches `lastByte`, used
     * to resolve the next_hop field in a MeshPacket header (which is a 1-byte
     * truncated NodeNum hint). Returns 0 if no match. If multiple neighbors
     * share the same low byte (1/256 chance), returns the first match — the
     * caller retargets to its SF; worst case the wrong neighbor picks up the
     * TX and the FloodingRouter dedup suppresses any accidental re-broadcast. */
    NodeNum findByLastByte(uint8_t lastByte) const;

    /* Raw "last-heard SF" for a node (for logs/diagnostics only — use lookup()
     * for TX retargeting decisions). Returns 0 if unknown. */
    uint8_t lastSeenSf(NodeNum node) const;

    /* Returns true if we've heard this node on 2+ different SFs — likely a
     * multi-SF bridge peer. */
    bool isMultiSF(NodeNum node) const;

    /* Record the wire channel-hash byte we last observed from `from`. Only
     * meaningful when we shadow-decoded the packet (i.e. the hash did NOT
     * match any of our configured channels) — in that case this remembers
     * the peer's own channel hash so we can stamp outgoing unicasts to them
     * with a byte their stock firmware will accept. No-op if `from` is not
     * already tracked (we learn hashes only for peers we've heard SFs from). */
    void setHash(NodeNum from, uint8_t channelHash);

    /* Lookup the cached wire hash for `node`. Returns 0xFF when unknown
     * (caller should NOT override and should fall through to the normal
     * channel-hash stamping). 0x00 is a valid real hash (PKI DM marker)
     * so it cannot be used as the sentinel. */
    uint8_t getHash(NodeNum node) const;

    /* Current number of tracked peers. */
    size_t size() const { return count; }

    /* Empty the tracker (e.g. on factory reset). Also wipes the on-disk file
     * so a reboot doesn't repopulate stale entries. */
    void clear();

    /* Load persisted direct-neighbor SF/hash entries from the filesystem. Call
     * once during init after the filesystem is ready (alongside other on-disk
     * state). No-op if no file exists or the build has no filesystem. */
    void loadFromDisk();

    /* Flush dirty entries to disk. Throttled internally — at most one write per
     * SAVE_INTERVAL_MS — and a no-op when nothing has changed. `force=true`
     * bypasses the throttle (used at shutdown / deep sleep). Returns true if the
     * data is persisted (or there was nothing to write), false on write failure. */
    bool saveToDisk(bool force = false);

  private:
    /* Packed to keep this well under 1.5 KB for MAX_NUM_NODES=100. */
    struct Entry {
        NodeNum node;     /* 4 bytes, 0 means slot unused */
        uint8_t lastSf;   /* last-heard SF, for diagnostics */
        uint8_t sfMask;   /* bitmap, bit (SF-5) set if heard on that SF; SF5..SF12 fit in 8 bits */
        uint8_t peerHash; /* last shadow-decoded wire channel-hash from this
                           * peer; 0xFF = never shadow-decoded (no override) */
        uint8_t direct;   /* 1 = heard directly (hop_start==hop_limit),
                           * 0 = relay-path only (SF may not be node's own) */
        uint32_t seq;     /* insertion order, for LRU-ish eviction */
    };

    Entry entries[MAX_NUM_NODES] = {};
    size_t count = 0;
    uint32_t nextSeq = 1;

    /* --- persistence --- */

    /* Only the fields that are meaningful across a reboot are written. seq is
     * an in-RAM LRU counter (re-seeded on load); lastSf is derived from sfMask;
     * relay-path entries are skipped entirely (the path may have changed). */
    struct __attribute__((packed)) DiskEntry {
        NodeNum node;
        uint8_t sfMask;
        uint8_t peerHash;
    };

    struct __attribute__((packed)) DiskHeader {
        uint32_t magic;
        uint8_t version;
        uint8_t count;
    };

    static constexpr const char *FILENAME = "/prefs/node_sf.dat";
    static constexpr uint32_t MAGIC = 0x5346544b; // "SFTK"
    static constexpr uint8_t VERSION = 1;
    /* Don't persist more than this many peers — bounds the file and write cost.
     * MAX_NUM_NODES is the in-RAM cap; on a busy mesh the LRU keeps the most
     * recently heard, which is what we want to survive a reboot anyway. */
    static constexpr uint8_t MAX_DISK_ENTRIES = 64;
    static constexpr uint32_t SAVE_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes

    bool dirty = false;
    uint32_t lastDiskSave = 0; // millis() of last successful flush

    /* Mark the in-memory state changed; trigger a throttled flush. Called from
     * update()/setHash() whenever a persisted field actually changes. */
    void markDirty();
};

extern NodeSFTracker nodeSFTracker;
