/* See ShadowChannels.h for full rationale and design notes. */

#include "ShadowChannels.h"

#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE

#include <string.h>
#include <zephyr/sys/printk.h>

#include "Channels.h"                              /* for defaultpsk[] constant */
#include "CryptoEngine.h"                          /* for `crypto` global + AES API */
#include "MeshTypes.h"
#include "NodeSFTracker.h"                         /* per-peer wire-hash cache */
#include "generated/meshtastic/mesh.pb.h"
#include "generated/meshtastic/portnums.pb.h"
#include "pb_decode.h"

/* -----------------------------------------------------------------------------
 * Preset name table
 * -----------------------------------------------------------------------------
 * These strings MUST match the output of DisplayFormatters::getModemPresetDisplayName
 * exactly, byte-for-byte. Meshtastic's Channels::getName substitutes these when
 * ChannelSettings.name is blank AND config.lora.use_preset is true. The XOR-hash
 * is computed over those exact bytes.
 *
 * If upstream Meshtastic changes a preset's display name, or adds a new preset,
 * this table must be updated for Multi-SF bridging to recognize peers on that
 * preset's default channel. A sentinel "Default" entry is also included to
 * match peers that explicitly set the name to "Default" or came from older
 * Meshtastic builds that used that string.
 *
 * Note: legacy 0.x firmwares used the exact string "Default". Meshtastic's
 * Channels::fixupChannel() now rewrites "Default" to "" on read, so modern
 * peers wouldn't generate a "Default" hash — but including it is cheap and
 * covers any legacy packets we might hear. */
static const char *const kPresetNames[] = {
    "LongFast",       /* ModemPreset = 0 (default in config) */
    "LongSlow",       /* deprecated but still in the enum */
    "VeryLongSlow",   /* deprecated but still in the enum */
    "MediumSlow",
    "MediumFast",
    "ShortSlow",
    "ShortFast",
    "LongMod",        /* DisplayFormatters returns "LongMod" not "LongModerate" */
    "ShortTurbo",
    "LongTurbo",
    "Default",        /* legacy literal name */
};
static const size_t kNumPresetNames = sizeof(kPresetNames) / sizeof(kPresetNames[0]);

/* -----------------------------------------------------------------------------
 * Shadow PSK derivation
 * -----------------------------------------------------------------------------
 * The "default PSK" on the wire is not literally `defaultpsk[]` from Channels.h
 * in all cases. Meshtastic encodes it as a 1-byte placeholder in the protobuf
 * (psk = {N}), where N is the "default PSK index." At expand-time
 * (Channels::getKey), the code starts with defaultpsk[] and ADDS (pskIndex - 1)
 * to the last byte. That means:
 *   - pskIndex = 1 ("Default")  → PSK is literally defaultpsk[], unchanged.
 *   - pskIndex = 2              → last byte += 1.
 *   - pskIndex = N              → last byte += (N - 1).
 *
 * For shadow channels we compute the expanded PSK once at boot and store it
 * in `kShadowPsk`. We only support index=1 (the mainstream "Default") here;
 * indices 2+ are rare in real deployments and can be added later if needed. */
static uint8_t kShadowPsk[sizeof(defaultpsk)];
static bool    kShadowPskReady = false;

static void initShadowPskIfNeeded(void)
{
    if (kShadowPskReady) return;
    memcpy(kShadowPsk, defaultpsk, sizeof(defaultpsk));
    /* pskIndex = 1 → last byte += (1 - 1) = 0, i.e. no change. Intentionally
     * a no-op; kept here (rather than omitted) to make the "we are using the
     * index=1 alias" decision explicit for future readers who extend this to
     * other indices. */
    kShadowPsk[sizeof(defaultpsk) - 1] += (1 - 1);
    kShadowPskReady = true;
}

/* -----------------------------------------------------------------------------
 * Hash computation
 * -----------------------------------------------------------------------------
 * Matches Channels::xorHash / Channels::generateHash — XOR every byte of the
 * channel name, XOR every byte of the PSK, XOR those two results.
 *
 * We re-implement it here (rather than calling Channels::generateHash) because
 * generateHash operates on a real channel slot; we need to compute hashes for
 * hypothetical (name, psk) pairs without polluting the channels[] array. */
static uint8_t xorBytes(const uint8_t *buf, size_t len)
{
    uint8_t h = 0;
    for (size_t i = 0; i < len; i++) h ^= buf[i];
    return h;
}

static uint8_t hashForName(const char *name)
{
    initShadowPskIfNeeded();
    uint8_t h = xorBytes((const uint8_t *)name, strlen(name));
    h ^= xorBytes(kShadowPsk, sizeof(kShadowPsk));
    return h;
}

/* -----------------------------------------------------------------------------
 * One-time log of the table — useful for debugging hash mismatches in a
 * live deployment. Printed on the first call to shadowChannelsTryDecrypt.
 * ----------------------------------------------------------------------------- */
static void logShadowTableOnce(void)
{
    static bool logged = false;
    if (logged) return;
    logged = true;

    qprintk("ShadowCh : initialized table (PSK index=1)\n");
    for (size_t i = 0; i < kNumPresetNames; i++) {
        qprintk("ShadowCh :   %-14s hash=0x%02x\n",
               kPresetNames[i], hashForName(kPresetNames[i]));
    }
}

/* -----------------------------------------------------------------------------
 * shadowChannelsTryDecrypt — the public entry point
 * ----------------------------------------------------------------------------- */
bool shadowChannelsTryDecrypt(meshtastic_MeshPacket *p)
{
    if (!p) return false;
    if (p->which_payload_variant != meshtastic_MeshPacket_encrypted_tag) return false;
    if (p->encrypted.size == 0) return false;

    initShadowPskIfNeeded();
    logShadowTableOnce();

    /* Iterate the shadow table looking for a hash match. Multiple entries
     * could theoretically collide on the same hash (it's only 8 bits), so we
     * try each match until one yields a valid protobuf. */
    for (size_t i = 0; i < kNumPresetNames; i++) {
        if (hashForName(kPresetNames[i]) != p->channel) continue;

        /* Found a candidate. Copy the encrypted bytes to a scratch buffer
         * (because decrypt is in-place) and try AES-CTR with our shadow PSK.
         *
         * CryptoEngine::decrypt uses the channel's "active key" which was set
         * by the last setKey() call. We need to set it to kShadowPsk first.
         * This is the same mechanism Channels::setActiveByIndex uses. */
        uint8_t scratch[sizeof(p->encrypted.bytes)];
        memcpy(scratch, p->encrypted.bytes, p->encrypted.size);

        /* Stage the shadow key into CryptoEngine. */
        CryptoKey tmpKey;
        tmpKey.length = sizeof(kShadowPsk);
        memcpy(tmpKey.bytes, kShadowPsk, sizeof(kShadowPsk));
        crypto->setKey(tmpKey);

        crypto->decrypt(p->from, p->id, p->encrypted.size, scratch);

        /* Parse the plaintext as a Data protobuf. If it fails or the portnum
         * is UNKNOWN_APP, this hash was a false positive — keep looking. */
        meshtastic_Data decodedtmp;
        memset(&decodedtmp, 0, sizeof(decodedtmp));
        if (pb_decode_from_bytes(scratch, p->encrypted.size,
                                 &meshtastic_Data_msg, &decodedtmp) &&
            decodedtmp.portnum != meshtastic_PortNum_UNKNOWN_APP) {
            p->decoded = decodedtmp;
            p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            qprintk("ShadowCh : decoded id=0x%08x via '%s' (hash=0x%02x portnum=%u)\n",
                   (unsigned)p->id, kPresetNames[i],
                   (unsigned)p->channel, (unsigned)decodedtmp.portnum);
            /* Remember this peer's wire channel-hash so future outgoing
             * unicasts to them can be stamped with the same byte (fix #1:
             * per-peer outbound hash override). Only useful for direct
             * neighbors — setHash is a no-op when `from` isn't already
             * tracked (we learn hashes only for peers we've also heard an
             * SF from in `LR2021Interface::addReceiveMetadata`). */
            nodeSFTracker.setHash(p->from, p->channel);
            return true;
        }
    }

    /* No shadow entry's hash matched, or every match yielded garbage. Caller
     * will treat as DECODE_FAILURE and the packet gets dropped. */
    return false;
}

/* -----------------------------------------------------------------------------
 * shadowChannelsHashForSf — per-SF "preset that natively lives here" hash
 * -----------------------------------------------------------------------------
 * See ShadowChannels.h for the full rationale. In brief: the fan-out TX path
 * calls this once per side-SF copy to pick the hash byte that the stock peers
 * who natively use that SF will accept as "a broadcast on my channel."
 *
 * The SF→name table below is the inverse of Meshtastic's preset→(BW,SF) map
 * for the 250 kHz / 125 kHz presets the Multi-SF bridge actually serves. If
 * upstream Meshtastic ever changes a preset's default SF, update this table
 * (and the equivalent entries in kPresetNames if the display string changes
 * too — they are the source of truth for the hash). */
uint8_t shadowChannelsHashForSf(uint8_t sf)
{
    const char *name = nullptr;
    switch (sf) {
        case 7:  name = "ShortFast";   break;   /* SHORT_FAST  — 250 kHz / SF7 */
        case 8:  name = "ShortSlow";   break;   /* SHORT_SLOW  — 250 kHz / SF8 */
        case 9:  name = "MediumFast";  break;   /* MEDIUM_FAST — 250 kHz / SF9 */
        case 10: name = "MediumSlow";  break;   /* MEDIUM_SLOW — 250 kHz / SF10 */
        case 11: name = "LongFast";    break;   /* LONG_FAST   — 250 kHz / SF11 */
        case 12: name = "LongSlow";    break;   /* LONG_SLOW   — 125 kHz / SF12 */
        default: return 0xFF;                   /* no canonical preset for this SF */
    }
    return hashForName(name);
}

#endif /* MESHTASTIC_MULTI_SF_BRIDGE */
