#include "NodeSFTracker.h"

#include <zephyr/sys/printk.h>

#include "FSCommon.h"
#include "SPILock.h"
#include "mesh/Throttle.h"

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
                /* Only direct observations are persisted, so a relay-path mask
                 * change (which never sets direct=1) isn't worth a flash write. */
                if (entries[i].direct) markDirty();
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
        if (direct) markDirty();
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
    /* Evicting a direct entry and/or inserting a new direct one both change the
     * persisted set. (A relay-path eviction of a relay-path entry doesn't, but
     * conflating the two cases isn't worth the branch.) */
    if (direct) markDirty();
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
                /* peerHash is persisted only for direct neighbors (the only
                 * ones we write to disk). */
                if (entries[i].direct) markDirty();
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

#ifdef FSCom

void NodeSFTracker::markDirty()
{
    dirty = true;
    /* Don't hammer flash: flush at most once per SAVE_INTERVAL_MS, but always
     * write the very first change after boot so a crash-reboot loop can't keep
     * losing the table. Mirrors TransmitHistory's policy. */
    if (lastDiskSave == 0 || !Throttle::isWithinTimespanMs(lastDiskSave, SAVE_INTERVAL_MS)) {
        if (saveToDisk()) {
            lastDiskSave = millis();
        }
    }
}

void NodeSFTracker::clear()
{
    count = 0;
    nextSeq = 1;
    dirty = false;
    /* Wipe the file too, so a reboot after factory-reset doesn't repopulate. */
    spiLock->lock();
    if (FSCom.exists(FILENAME)) {
        FSCom.remove(FILENAME);
    }
    spiLock->unlock();
    lastDiskSave = 0;
}

void NodeSFTracker::loadFromDisk()
{
    spiLock->lock();
    auto file = FSCom.open(FILENAME, FILE_O_READ);
    if (file) {
        DiskHeader header{};
        if (file.read((uint8_t *)&header, sizeof(header)) == sizeof(header) && header.magic == MAGIC &&
            header.version == VERSION && header.count <= MAX_DISK_ENTRIES) {
            uint8_t loaded = 0;
            for (uint8_t i = 0; i < header.count && count < MAX_NUM_NODES; i++) {
                DiskEntry de{};
                if (file.read((uint8_t *)&de, sizeof(de)) != sizeof(de)) break;
                if (de.node == 0 || de.sfMask == 0) continue;
                entries[count].node = de.node;
                entries[count].sfMask = de.sfMask;
                entries[count].peerHash = de.peerHash;
                entries[count].direct = 1; /* only direct entries are persisted */
                /* Re-derive lastSf from the lowest set bit in the mask (good
                 * enough for diagnostics; lookup() reads the mask anyway). */
                uint8_t lastSf = 0;
                for (uint8_t sf = 5; sf <= 12; sf++) {
                    if (de.sfMask & (1u << (sf - 5))) { lastSf = sf; break; }
                }
                entries[count].lastSf = lastSf;
                entries[count].seq = nextSeq++;
                count++;
                loaded++;
            }
            qprintk("SFTrack  : loaded %u entries from disk\n", loaded);
        } else {
            qprintk("SFTrack  : invalid file header, starting fresh\n");
        }
        file.close();
    } else {
        qprintk("SFTrack  : no persisted file, starting fresh\n");
    }
    spiLock->unlock();
    dirty = false;
}

bool NodeSFTracker::saveToDisk(bool force)
{
    if (!dirty) return true;
    if (!force && lastDiskSave != 0 && Throttle::isWithinTimespanMs(lastDiskSave, SAVE_INTERVAL_MS)) {
        return true; /* throttled — leave dirty, a later call will flush it */
    }

    spiLock->lock();

    FSCom.mkdir("/prefs");
    if (FSCom.exists(FILENAME)) {
        FSCom.remove(FILENAME);
    }

    auto file = FSCom.open(FILENAME, FILE_O_WRITE);
    if (!file) {
        qprintk("SFTrack  : failed to open file for writing\n");
        spiLock->unlock();
        return false;
    }

    /* Count persistable (direct) entries first so the header is accurate. */
    uint8_t toWrite = 0;
    for (size_t i = 0; i < count && toWrite < MAX_DISK_ENTRIES; i++) {
        if (entries[i].direct && entries[i].node != 0 && entries[i].sfMask != 0) toWrite++;
    }

    DiskHeader header{};
    header.magic = MAGIC;
    header.version = VERSION;
    header.count = toWrite;
    file.write((uint8_t *)&header, sizeof(header));

    uint8_t written = 0;
    for (size_t i = 0; i < count && written < toWrite; i++) {
        if (!entries[i].direct || entries[i].node == 0 || entries[i].sfMask == 0) continue;
        DiskEntry de{};
        de.node = entries[i].node;
        de.sfMask = entries[i].sfMask;
        de.peerHash = entries[i].peerHash;
        file.write((uint8_t *)&de, sizeof(de));
        written++;
    }
    file.flush();
    file.close();
    qprintk("SFTrack  : saved %u entries to disk\n", written);

    dirty = false;
    lastDiskSave = millis();
    spiLock->unlock();
    return true;
}

#else // no filesystem

void NodeSFTracker::markDirty() { dirty = true; }
void NodeSFTracker::clear()
{
    count = 0;
    nextSeq = 1;
    dirty = false;
}
void NodeSFTracker::loadFromDisk() {}
bool NodeSFTracker::saveToDisk(bool) { return true; }

#endif
