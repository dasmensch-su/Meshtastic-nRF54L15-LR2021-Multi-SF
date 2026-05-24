#pragma once
/* =============================================================================
 * ShadowChannels — preset-agnostic channel decryption for a Multi-SF bridge
 * =============================================================================
 *
 * PROBLEM THIS SOLVES
 * -------------------
 * Meshtastic packets carry a 1-byte "channel hash" in the header that tells
 * the receiver which channel PSK to use for decryption. That hash is computed
 * as:
 *
 *     hash = xor(channel_name_bytes) ^ xor(psk_bytes)
 *
 * When a user leaves the channel name blank AND `config.lora.use_preset = true`,
 * Meshtastic (see Channels::getName) substitutes the preset's display name
 * ("LongFast", "ShortFast", "ShortSlow", ...) into the hash computation. This
 * means two nodes sharing the SAME default PSK but running DIFFERENT presets
 * produce DIFFERENT hashes — even though cryptographically they could decrypt
 * each other's traffic just fine.
 *
 * For a Multi-SF BRIDGE this is fatal: the bridge listens on multiple SFs at
 * once, hears packets from peers on different presets, but filters them out
 * at the hash-lookup step before even trying to decrypt. NodeInfo broadcasts
 * (which carry public keys) never reach the bridge's NodeDB, so cross-SF PKI
 * DMs never bootstrap.
 *
 * WHAT THIS MODULE DOES
 * ---------------------
 * At boot, we precompute a small lookup table of:
 *
 *     (preset_name_string, channel_hash)
 *
 * for every standard Meshtastic modem-preset display name, using the
 * well-known default PSK (the public constant in Channels.h, XORed with
 * psk_index=1 per Meshtastic's convention for the standard "Default" PSK).
 *
 * When Router::perhapsDecode fails to match a packet's channel hash against
 * any of the node's configured channels[], it falls through to this module.
 * We try each shadow entry whose hash matches the packet's channel byte, run
 * AES-CTR decryption with the default PSK, and if the output is a
 * well-formed Data protobuf with a valid portnum, we accept it.
 *
 * WHY THIS IS ACCEPTABLE
 * ----------------------
 * - No protocol change. The wire format is unchanged. Other nodes don't know
 *   (or care) that the bridge is doing this.
 * - No new PSK is trusted. We're using the well-known default PSK that the
 *   peer already used to encrypt the packet — just finding it via a different
 *   lookup key. The cryptographic properties are identical to normal decryption.
 * - No UI pollution. The shadow entries do not live in the configured
 *   channels[] array. They are invisible to the phone API / MQTT bridge /
 *   persistence layer.
 * - Scoped to Multi-SF bridge deployments. Gated behind
 *   MESHTASTIC_MULTI_SF_BRIDGE at compile time; when that flag is off, the
 *   whole file compiles out and the extra hook in perhapsDecode is dead code.
 *
 * WHY THIS IS A WORKAROUND
 * ------------------------
 * The RIGHT fix lives upstream in Meshtastic itself:
 *   a) Decouple the channel-identity hash from the display name (e.g. always
 *      use a stable canonical name like "Default" in the hash, and treat the
 *      preset-derived name purely as a UI label); or
 *   b) Drop the channel-byte filter entirely for PKI DMs and NodeInfo, which
 *      are the two packet types whose cross-preset reach actually matters.
 *
 * Either of those would make Multi-SF bridging a first-class feature without
 * every implementation having to ship a shadow-channels workaround. Until
 * then, this file lets our port function in real-world mixed-preset meshes.
 *
 * We are deliberately keeping this self-contained and heavily commented so
 * that if/when the Meshtastic project tackles the underlying problem, the
 * workaround is easy to identify and remove.
 *
 * LIMITATIONS
 * -----------
 * - Only helps peers that use the default PSK (the overwhelming majority of
 *   Meshtastic deployments). Peers on custom PSKs still require manual
 *   channel alignment — that's inherent to the "shared secret" model, not
 *   something any code change can fix.
 * - Only knows the standard preset names baked into Meshtastic at the time
 *   this file was written. If upstream adds a new preset, the bridge will
 *   ignore that preset's peers until we update the table below.
 * - Adds one AES decryption attempt per incoming packet that fails the
 *   normal channel-hash check. Negligible with CRACEN hardware AES.
 *
 * EXPECTED UPSTREAM DISCUSSION
 * ----------------------------
 * If this work is submitted to Meshtastic, expect pushback on the idea of
 * "the bridge recognizes packets I didn't add to my channel list." That's
 * fair — the current channel byte acts as a cheap per-packet authorization
 * check. The counter-argument is that ANY node holding the default PSK can
 * already decrypt any default-channel packet regardless of the hash; the
 * hash is not a security boundary, just a dispatch hint. Shadow channels
 * simply acknowledge that reality for the specific case where a Multi-SF
 * bridge NEEDS to act as a universal translator across preset families.
 * ============================================================================= */

#include <stdint.h>
#include <stddef.h>

struct _meshtastic_MeshPacket;
typedef struct _meshtastic_MeshPacket meshtastic_MeshPacket;

#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE

/* Attempt to decrypt a packet using one of the shadow channel entries.
 *
 * Called from Router::perhapsDecode after the normal channels[] iteration
 * has failed to find a matching hash.
 *
 * On success:
 *   - p->which_payload_variant is set to meshtastic_MeshPacket_decoded_tag
 *   - p->decoded is populated with the parsed Data protobuf
 *   - p->channel is set to 0 (the bridge treats shadow-decoded packets as
 *     having arrived on its primary channel for downstream purposes —
 *     p->channel gets overwritten to chIndex right after this function
 *     returns anyway)
 *   - returns true
 *
 * On failure (no shadow hash match, or match found but AES output was
 * garbage): returns false. Caller should treat as DECODE_FAILURE.
 *
 * Implementation is self-contained. Requires `crypto` global (CryptoEngine)
 * to be initialized. */
bool shadowChannelsTryDecrypt(meshtastic_MeshPacket *p);

/* Return the wire channel-hash byte that a stock-firmware peer running the
 * preset canonically associated with spreading factor `sf` would expect to
 * see on its own broadcasts, assuming that peer uses the default PSK.
 *
 * WHY: the bridge's cross-SF fan-out retransmits bridge-originated broadcasts
 * on every side SF so cross-preset peers hear them physically. But the
 * hash byte in the header is still the bridge's own channel hash, which
 * stock peers on a different preset reject at the channel-filter step
 * (that's the same rejection shadow channels removes on the RX side here).
 * Before transmitting each side-SF copy, the fan-out code stamps the copy
 * with the hash returned by this function — i.e. "here's the hash the peer
 * who natively uses this SF would have generated for its own default
 * channel." The payload bytes are unchanged (still AES-CTR-encrypted with
 * the default PSK), so any peer holding the default PSK decrypts
 * successfully regardless of which hash is stamped; the hash is only a
 * dispatch hint, not cryptography.
 *
 * HOW: maps SF → canonical preset display name → precomputed hash from the
 * static shadow table. The mapping is based on the default (bandwidth,
 * spreading-factor) pairs Meshtastic assigns to each preset:
 *
 *     SF7  → ShortFast    SF8  → ShortSlow    SF9  → MediumFast
 *     SF10 → MediumSlow   SF11 → LongFast     SF12 → LongSlow
 *
 * (ShortTurbo/LongTurbo/LongModerate use 500 kHz / 62.5 kHz BW variants
 * that don't line up with a unique SF in the 125 kHz band this bridge
 * actually listens on, so they're not covered here.)
 *
 * Returns 0xFF when `sf` has no canonical preset — callers should skip the
 * override in that case and transmit the fan-out copy with the bridge's
 * own hash. */
uint8_t shadowChannelsHashForSf(uint8_t sf);

#endif /* MESHTASTIC_MULTI_SF_BRIDGE */
