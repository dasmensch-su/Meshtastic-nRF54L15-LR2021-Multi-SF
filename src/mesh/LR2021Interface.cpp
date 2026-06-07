#ifdef USE_LR2021
#include "LR2021Interface.h"
#include "MeshTypes.h"
#include "NodeSFTracker.h"
#include "configuration.h"
#include "detect/LoRaRadioType.h"
#include "error.h"
#include "NodeDB.h"
#ifdef MESHTASTIC_MULTI_SF_BRIDGE
#include "MultiSFBridge.h"
#include "ShadowChannels.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* ---------- Meshtastic LoRa sync word ------------------------------------ */
#define MESHTASTIC_LORA_SYNCWORD 0x2bu

/* ---------- Constructor / Destructor ------------------------------------- */

LR2021Interface::LR2021Interface(LockingArduinoHal *hal,
                                 RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq,
                                 RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora),
      lora(&module)
{
    LOG_INFO("LR2021Interface: created (RadioLib-based)");
}

LR2021Interface::~LR2021Interface()
{
}

/* ---------- init --------------------------------------------------------- */

bool LR2021Interface::init()
{
    RadioLibInterface::init();

    float freq = getFreq();
    if (freq == 0) freq = 2404.469f;

    /* LR2021 sub-GHz max output is +22 dBm; 2.4 GHz max is +12 dBm. Channel
     * URLs commonly request +30 dBm (legal max in some regions but beyond what
     * this chip can drive), which makes lora.begin() fail with -13. Clamp. */
    int8_t maxPwr = (freq >= 2000.0f) ? 12 : 22;
    if (power > maxPwr) {
        qprintk("LR2021   : clamping tx_power %d -> %d dBm (chip max)\n", power, maxPwr);
        power = maxPwr;
    }

    qprintk("LR2021   : begin freq=%.3f bw=%.0f sf=%u cr=%u pwr=%d\n",
           (double)freq, (double)bw, sf, cr, power);

    /* Wio-LR2021 board uses DIO8 for IRQ (not DIO5 default) */
    lora.irqDioNum = 8;

    /* Increase SPI timeout for BUSY waits during reset/calibration */
    module.spiConfig.timeout = 10000;

    /* Wio-LR2021 board: use XTAL mode (not TCXO control).
     * The board has always-on TCXO but the Semtech reference DTS sets
     * tcxo-wakeup-time=0, treating it as XTAL. Using TCXO mode causes
     * BUSY to stay HIGH after reset, preventing initialization. */
    lora.XTAL = true;

    int16_t res = lora.begin(freq, bw, sf, cr, MESHTASTIC_LORA_SYNCWORD,
                             power, preambleLength, 0.0f);
    if (res != RADIOLIB_ERR_NONE) {
        qprintk("LR2021   : begin FAILED (%d)\n", res);
        return false;
    }



    /* Front-end calibration — Semtech reference does this but RadioLib skips it.
     * Calibration frequencies from the official Wio-LR2021 DTS:
     *   470 MHz (LF), 897.5 MHz (LF), 2441 MHz (HF) */
    {
        uint8_t calData[6] = {
            0x00, 0x76,  /* 470 MHz / 4 MHz = 0x0076 (LF) */
            0x00, 0xE0,  /* 897.5 MHz / 4 MHz = 0x00E0 (LF) */
            0x82, 0x63,  /* 2441 MHz / 4 MHz | 0x8000 = 0x8263 (HF) */
        };
        module.SPIwriteStream(0x0123, calData, sizeof(calData), false, false);
        k_msleep(50);
    }

    res = lora.setCRC(2);
    if (res != RADIOLIB_ERR_NONE)
        LOG_WARN("LR2021: setCRC failed (%d)", res);

    res = lora.setRxBoostedGainMode(true);
    if (res != RADIOLIB_ERR_NONE)
        LOG_WARN("LR2021: setRxBoostedGain failed (%d)", res);

#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE
    /* Phase 1: install side detectors for multi-SF listen. Must happen BEFORE
     * startReceive(). setLoraModulationParams() disables all sides, so this
     * also needs to be re-run after any reconfigure(). */
    configureMultiSFListen();
#endif

#ifdef MULTI_SF_HOME_SF_DEFAULT
    setHomeSf(MULTI_SF_HOME_SF_DEFAULT);
    if (homeSf != 0)
        qprintk("LR2021   : home SF = SF%u\n", homeSf);
#endif

    startReceive();

    qprintk("LR2021   : init complete\n");
    return true;
}

/* ---------- reconfigure -------------------------------------------------- */

bool LR2021Interface::reconfigure()
{
    setStandby();

    float freq = getFreq();
    LOG_INFO("LR2021: reconfigure freq=%.3f bw=%.0f sf=%u cr=%u pwr=%d",
             freq, bw, sf, cr, power);

    int16_t res;
    res = lora.setFrequency(freq);
    if (res != RADIOLIB_ERR_NONE) LOG_ERROR("LR2021: setFrequency failed (%d)", res);

    res = lora.setBandwidth(bw);
    if (res != RADIOLIB_ERR_NONE) LOG_ERROR("LR2021: setBandwidth failed (%d)", res);

    res = lora.setSpreadingFactor(sf);
    if (res != RADIOLIB_ERR_NONE) LOG_ERROR("LR2021: setSpreadingFactor failed (%d)", res);

    res = lora.setCodingRate(cr);
    if (res != RADIOLIB_ERR_NONE) LOG_ERROR("LR2021: setCodingRate failed (%d)", res);

    res = lora.setOutputPower(power);
    if (res != RADIOLIB_ERR_NONE) LOG_ERROR("LR2021: setOutputPower failed (%d)", res);

    res = lora.setPreambleLength(preambleLength);
    if (res != RADIOLIB_ERR_NONE) LOG_ERROR("LR2021: setPreambleLength failed (%d)", res);

    res = lora.setSyncWord(MESHTASTIC_LORA_SYNCWORD);
    if (res != RADIOLIB_ERR_NONE) LOG_ERROR("LR2021: setSyncWord failed (%d)", res);

    res = lora.setCRC(2);
    if (res != RADIOLIB_ERR_NONE) LOG_ERROR("LR2021: setCRC failed (%d)", res);

#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE
    /* Modulation params change wiped side detectors — reinstall before RX. */
    configureMultiSFListen();
#endif

    if (homeSf != 0 && homeSf != sf) {
        bool valid = false;
        for (uint8_t i = 0; i < 3; i++) {
            if (sideSF[i] == homeSf) { valid = true; break; }
        }
        if (!valid) {
            qprintk("LR2021   : home SF%u no longer valid, reverting\n", homeSf);
            homeSf = 0;
        }
    }

    startReceive();
    return true;
}

/* ---------- sleep -------------------------------------------------------- */

bool LR2021Interface::sleep()
{
    disableInterrupt();
    lora.sleep();
    return true;
}

/* ---------- startReceive ------------------------------------------------- */

void LR2021Interface::startReceive()
{
    setStandby();

#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE
    /* Phase 3: if we retargeted TX to a peer's SF, restore main SF + side
     * detectors now (after TX completes, before we re-arm RX). */
    if (savedMainSf != 0) {
        int16_t res = lora.setSpreadingFactor(savedMainSf);
        if (res != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR2021: restore main SF%u failed (%d)", savedMainSf, res);
        }
        savedMainSf = 0;
        /* setSpreadingFactor wipes side detectors per datasheet §9.9.6 — reinstall. */
        configureMultiSFListen();
    }
#endif

    /* Use generic RadioLib IRQ flags — getIrqMapped() converts to chip-specific. */
    uint32_t irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS;
    uint32_t irqMask = RADIOLIB_IRQ_RX_DEFAULT_MASK;

    int16_t res = lora.startReceive(RADIOLIB_LR2021_RX_TIMEOUT_INF, irqFlags, irqMask, 0);
    if (res != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR2021: startReceive failed (%d)", res);
    } else {
        LOG_DEBUG("LR2021: startReceive OK");
    }

    RadioLibInterface::startReceive();

    enableInterrupt(isrRxLevel0);

    /* Edge-triggered GPIO interrupt race: if an RX packet (e.g. an ACK)
     * arrived between lora.startReceive() and enableInterrupt() above,
     * DIO8 went high while the interrupt was detached — the rising edge
     * was lost. Check the chip's IRQ register immediately to catch it. */
    checkRxDoneIrqFlag();
}

/* ---------- startSend (Phase 3: TX retargeting) ------------------------- */

bool LR2021Interface::startSend(meshtastic_MeshPacket *txp)
{
#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && defined(MULTI_SF_BROADCAST_FANOUT) && MULTI_SF_BROADCAST_FANOUT
    /* Phase 6 (opt-in): also fan out the bridge's OWN broadcasts so peers on
     * other SFs can hear them. Without this, the bridge transmits only on
     * main SF, no peer rebroadcasts on main SF, and the bridge hits max
     * retransmission on every broadcast it originates. The dedup ring in
     * MultiSFBridge prevents double-fanning when we re-hear the relay.
     * Disabled by default — see addReceiveMetadata note above. */
    if (multiSFBridge && txp != NULL && txp->to == NODENUM_BROADCAST &&
        txp->from == nodeDB->getNodeNum() && txp->id != 0) {
        multiSFBridge->scheduleFanout(txp, sf);
    }
#endif

#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE
    uint8_t targetSf = 0;

    bool consumeForced = false;
    if (txp != NULL) {
        /* Phase 6 first: check pointer-keyed force for cross-SF fan-out clones. */
        for (size_t i = 0; i < sizeof(ptrForce) / sizeof(ptrForce[0]); i++) {
            if (ptrForce[i].pkt == txp && ptrForce[i].sf != 0) {
                targetSf = ptrForce[i].sf;
                qprintk("LR2021   : TX fan-out id=0x%08x on SF%u (ptr)\n", (unsigned)txp->id, targetSf);
                ptrForce[i].pkt = nullptr;
                ptrForce[i].sf = 0;
                break;
            }
        }
        /* Phase 4: beaconer's "next broadcast goes on this SF" slot. */
        if (targetSf == 0 && txp->to == NODENUM_BROADCAST && forcedNextTxSf != 0 && forcedNextTxSf != sf) {
            targetSf = forcedNextTxSf;
            consumeForced = true;
            qprintk("LR2021   : TX broadcast on SF%u (forced)\n", targetSf);
        }
        /* Relay-on-received-SF: relayed broadcasts retransmit on the SF they
         * arrived on, preserving per-SF flood domain integrity. Without this,
         * relays silently move side-SF broadcasts to main SF, breaking implicit
         * ACKs for peers on that side SF. Always active with Multi-SF. */
        if (targetSf == 0 && txp->to == NODENUM_BROADCAST && txp->from != nodeDB->getNodeNum()) {
            uint8_t rxSf = lookupRxSf(txp->from, txp->id);
            if (rxSf != 0 && rxSf != sf) {
                targetSf = rxSf;
                qprintk("LR2021   : TX relay broadcast on SF%u (received-SF)\n", targetSf);
            }
        }
        /* Phase 3: unicast to a peer we've heard before, on a different SF.
         * For multi-hop destinations, the relevant SF is the direct-neighbor
         * next_hop's SF, NOT the final destination's last-heard SF (which
         * would be the SF of whatever relay we last heard the destination
         * through). Resolve next_hop (1-byte NodeNum suffix) to a direct
         * neighbor in our tracker; fall back to the destination directly only
         * if no next_hop hint is set. */
        if (targetSf == 0 && txp->to != 0 && txp->to != NODENUM_BROADCAST) {
            NodeNum lookupNode = 0;
            if (txp->next_hop != 0 && txp->next_hop != NO_NEXT_HOP_PREFERENCE) {
                lookupNode = nodeSFTracker.findByLastByte(txp->next_hop);
            }
            if (lookupNode == 0) {
                lookupNode = txp->to;
            }
            uint8_t peerSf = nodeSFTracker.lookup(lookupNode);
            if (peerSf != 0 && peerSf != sf) {
                targetSf = peerSf;
                qprintk("LR2021   : TX retarget SF%u -> SF%u to 0x%08x (via 0x%08x)\n",
                       sf, targetSf, (unsigned)txp->to, (unsigned)lookupNode);
            }
            /* Unknown destination: relay on the SF the DM arrived on. The sender
             * chose that SF because they share a flood domain with the destination;
             * defaulting to main SF would strand the packet on the wrong SF. */
            if (targetSf == 0 && txp->from != nodeDB->getNodeNum()) {
                uint8_t rxSf = lookupRxSf(txp->from, txp->id);
                if (rxSf != 0 && rxSf != sf) {
                    targetSf = rxSf;
                    qprintk("LR2021   : TX relay DM on SF%u (received-SF, dest unknown)\n", targetSf);
                }
            }
        }
        /* Home SF: all originated traffic defaults to home SF instead of main.
         * The beaconer's forcedNextTxSf has higher priority so NodeInfo rotation
         * is unaffected. Known-peer DM retarget also wins — home SF is the
         * fallback when we don't know where a peer is. homeSf=0 means use main. */
        if (targetSf == 0 && homeSf != 0 && homeSf != sf &&
            txp->from == nodeDB->getNodeNum()) {
            targetSf = homeSf;
            qprintk("LR2021   : TX on SF%u (home)\n", targetSf);
        }
    }

    if (consumeForced) {
        forcedNextTxSf = 0;
    }

    if (targetSf != 0) {
        int16_t res = lora.setSpreadingFactor(targetSf);
        if (res == RADIOLIB_ERR_NONE) {
            savedMainSf = sf;
        } else {
            LOG_ERROR("LR2021: TX SF switch to SF%u failed (%d)", targetSf, res);
            /* Leave chip on main SF — packet may not reach peer but state is consistent. */
        }
    }

    /* --- Broadcast-side hash override (fan-out companion to fix #1) --------
     *
     * WHY: When the bridge retargets one of its OWN broadcasts onto a side SF
     * (e.g. rotating NodeInfo on SF8 for ShortSlow peers, or fan-out of a
     * broadcast text on SF11 for LongFast peers), the packet's wire header
     * still carries the bridge's own channel hash (ShortFast → 0x69 in our
     * deployment). Stock peers on a different preset reject that at the
     * channel-hash filter before ever decrypting, so our NodeInfo never
     * lands in their NodeDB and they never learn the bridge's pubkey.
     *
     * The fix is symmetric to fix #1 on the unicast path: we stamp the copy
     * with the hash that a stock peer who *natively* uses this SF would
     * have generated on its own default channel. That's a static lookup
     * into the ShadowChannels preset table via shadowChannelsHashForSf().
     * The AES-CTR payload is unchanged (still encrypted with the default
     * PSK our channel shares with every other default-PSK node), so the
     * receiver decrypts correctly — we're only relabeling the dispatch hint.
     *
     * SCOPE: only applies when
     *   - we are the originator (txp->from == our nodenum),
     *   - it's a broadcast (to == 0xFFFFFFFF),
     *   - we actually retargeted to a side SF (targetSf != 0),
     *   - the side SF has a canonical preset (hashForSf != 0xFF).
     *
     * Rebroadcasts of OTHER peers' broadcasts are deliberately excluded —
     * those keep their original sender's hash so the originator and the
     * originator's same-preset neighbors still recognize the packet as
     * theirs (e.g. implicit ACKs via rebroadcast continue to work). */
    if (targetSf != 0 && txp != NULL && txp->to == NODENUM_BROADCAST &&
        txp->from == nodeDB->getNodeNum()) {
        uint8_t sfHash = shadowChannelsHashForSf(targetSf);
        if (sfHash != 0xFF && sfHash != txp->channel) {
            qprintk("ShadowCh : broadcast hash override 0x%02x -> 0x%02x on SF%u (id=0x%08x)\n",
                   (unsigned)txp->channel, (unsigned)sfHash,
                   (unsigned)targetSf, (unsigned)txp->id);
            txp->channel = sfHash;
        }
    }
#endif /* MESHTASTIC_MULTI_SF_BRIDGE */

    return RadioLibInterface::startSend(txp);
}

/* ---------- configHardwareForSend ---------------------------------------- */

void LR2021Interface::configHardwareForSend()
{
    /* Nothing special needed — RadioLibInterface handles the sequence */
}

/* ---------- setStandby --------------------------------------------------- */

void LR2021Interface::setStandby()
{
    RadioLibInterface::setStandby();
    lora.standby();
}

/* ---------- ISR enable/disable ------------------------------------------- */

void LR2021Interface::disableInterrupt()
{
    lora.clearIrqAction();
}

void LR2021Interface::enableInterrupt(void (*callback)())
{
    lora.setIrqAction(callback);
}

/* ---------- Channel activity detection ----------------------------------- */

bool LR2021Interface::isChannelActive()
{
    /* TODO: implement CAD — lora.scanChannel() blocks on the LR2021. */
    return false;
}

bool LR2021Interface::isActivelyReceiving()
{
    uint32_t irqFlags = lora.getIrqFlags();
    return (irqFlags & RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID) != 0;
}

/* ---------- RX metadata -------------------------------------------------- */

void LR2021Interface::addReceiveMetadata(meshtastic_MeshPacket *mp)
{
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lrintl(lora.getRSSI());

#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE
    /* Multi-SF: log which detector caught this packet and remember the SF the
     * sender used, so unicast replies can be retargeted to that SF in Phase 3. */
    uint8_t det = getLastRxDetector();
    uint8_t rxSf = sf;  // main SF by default
    if (det >= 1 && det <= 3 && sideSF[det - 1] != 0) {
        rxSf = sideSF[det - 1];
    }
    qprintk("LR2021   : RX detector=%u (SF%u) from=0x%08x snr=%.1f rssi=%d\n",
           det, rxSf, (unsigned)mp->from, (double)mp->rx_snr, (int)mp->rx_rssi);

    /* Two-tier SF recording:
     * Tier 1 (direct): hop_start == hop_limit means the originator's TX SF.
     * On some firmware paths hop_limit arrives already decremented by 1 (the
     * receiver-side accounting), so we also accept hop_start == hop_limit + 1
     * as direct. Stored with direct=true, always takes priority.
     * Tier 2 (relay-path): hop_limit < hop_start by 2+ hops → the SF is the
     * last relay's SF. Stored with direct=false — provides a working delivery
     * path but never overwrites a direct observation.
     *
     * Filter out: self-sourced packets, broadcast from addr, hop_start==0 (old
     * firmware — can't determine relay status so skip conservatively). */
    bool isDirect = (mp->hop_start > 0 &&
                     (mp->hop_limit == mp->hop_start ||
                      mp->hop_limit + 1 == mp->hop_start));
    if (mp->from != 0 && mp->from != NODENUM_BROADCAST &&
        mp->from != nodeDB->getNodeNum() &&
        isDirect) {
        nodeSFTracker.update(mp->from, rxSf);
    }

    /* Relay-path SF recording (Option 4): for relayed packets, record the
     * arrival SF as a relay-path observation for the SENDER. The SF isn't the
     * node's own TX SF but it IS a working path — if we need to reach this
     * node, sending on this SF reaches a relay that can forward to them.
     * Direct observations (above) always take priority. */
    if (mp->from != 0 && mp->from != NODENUM_BROADCAST &&
        mp->from != nodeDB->getNodeNum() &&
        mp->hop_start > 0 && !isDirect) {
        nodeSFTracker.update(mp->from, rxSf, false);
    }

    if (mp->from != 0 && mp->from != nodeDB->getNodeNum()) {
        recordRxSf(mp->from, mp->id, rxSf);
    }

#if defined(MULTI_SF_DM_FANOUT) && MULTI_SF_DM_FANOUT
    /* DM fan-out for unknown destinations. When we receive a unicast DM not
     * addressed to us and we don't know the destination's SF, fan it out on
     * all active SFs. The normal relay covers the arrival SF; fan-out covers
     * the rest so the DM reaches the right flood domain. */
    if (multiSFBridge && mp->to != NODENUM_BROADCAST && mp->to != 0 &&
        mp->to != nodeDB->getNodeNum() &&
        mp->from != 0 && mp->from != nodeDB->getNodeNum()) {
        uint8_t destSf = nodeSFTracker.lookup(mp->to);
        if (destSf == 0) {
            multiSFBridge->scheduleDmFanout(mp, rxSf);
        }
    }
#endif

    /* Record relay-path SF for the DESTINATION of relayed unicast DMs. Done
     * AFTER fan-out check so the first DM to an unknown destination still fans
     * out (arrival SF may not be the destination's SF). Subsequent DMs benefit
     * from the relay-path entry and skip fan-out. */
    if (mp->from != 0 && mp->from != NODENUM_BROADCAST &&
        mp->from != nodeDB->getNodeNum() &&
        mp->hop_start > 0 && !isDirect &&
        mp->to != NODENUM_BROADCAST && mp->to != 0 &&
        mp->to != nodeDB->getNodeNum()) {
        nodeSFTracker.update(mp->to, rxSf, false);
    }

#if defined(MULTI_SF_BROADCAST_FANOUT) && MULTI_SF_BROADCAST_FANOUT
    /* Phase 6 (opt-in): cross-SF flood relay. When a broadcast arrives on a
     * side detector (i.e. on an SF other than our main), schedule additional
     * re-transmits on every other enabled SF so peers there also hear it.
     * Skip packets we sent ourselves and skip packets from unknown sources.
     * Disabled by default since 2026-04-20 — cross-SF broadcast flooding burns
     * too much airtime for general deployment. Unicast retarget + NodeInfo
     * beaconer still carry the core Multi-SF value. */
    if (multiSFBridge && mp->to == NODENUM_BROADCAST && mp->from != 0 &&
        mp->from != nodeDB->getNodeNum() && det >= 1 && det <= 3) {
        multiSFBridge->scheduleFanout(mp, rxSf);
    }
#endif
#endif /* MESHTASTIC_MULTI_SF_BRIDGE */
}

/* ---------- Phase 6 cancellation protection ----------------------------- */

bool LR2021Interface::cancelSending(NodeNum from, PacketId id)
{
    /* If any of our protected fan-out clones matches the cancel request,
     * refuse — those clones are deliberate cross-SF copies, not duplicates.
     * Cancellation requests typically arise from perhapsCancelDupe() when we
     * hear our own main-SF relay rebroadcast; we don't want that to also
     * evict the side-SF copies we have queued for fan-out. */
    for (size_t i = 0; i < sizeof(ptrForce) / sizeof(ptrForce[0]); i++) {
        if (ptrForce[i].pkt != nullptr &&
            ptrForce[i].pkt->from == from && ptrForce[i].pkt->id == id) {
            qprintk("LR2021   : cancelSending(0x%08x) refused — protecting fan-out\n", (unsigned)id);
            return false;
        }
    }
    return RadioLibInterface::cancelSending(from, id);
}

/* ---------- Phase 6 pointer-keyed force registration -------------------- */

void LR2021Interface::registerForcedTx(meshtastic_MeshPacket *pkt, uint8_t sf)
{
    if (!pkt || sf == 0) return;
    for (size_t i = 0; i < sizeof(ptrForce) / sizeof(ptrForce[0]); i++) {
        if (ptrForce[i].pkt == nullptr) {
            ptrForce[i].pkt = pkt;
            ptrForce[i].sf = sf;
            return;
        }
    }
    /* All slots full — caller will TX on main SF (best-effort). */
    LOG_WARN("LR2021: ptrForce slots full, fan-out will use main SF");
}

/* ---------- Multi-SF listen (Phase 1 POC) -------------------------------- */

uint8_t LR2021Interface::configureMultiSFListen()
{
    /* Reset tracking */
    numSideDets = 0;
    sideSF[0] = sideSF[1] = sideSF[2] = 0;

    /* Target side-SF list. Default covers SHORT_SLOW/MEDIUM_FAST/LONG_FAST
     * (with main=SHORT_FAST this hits 4 of the 5 Meshtastic 250 kHz presets).
     * Override at build time with -DMULTI_SF_SIDE_LIST=... for bench tests
     * that need per-board side selection (e.g. waterfall topology where
     * different bridges cover different SFs to force multi-hop routing). */
#ifndef MULTI_SF_SIDE_LIST
#define MULTI_SF_SIDE_LIST 8, 9, 11
#endif
    static const uint8_t targetSFs[] = { MULTI_SF_SIDE_LIST };

    /* Can't run multi-SF on non-250 kHz for now (per Option A scope). */
    if (bw < 249.0f || bw > 251.0f) {
        LOG_INFO("LR2021: multi-SF listen skipped (BW=%.0f != 250 kHz)", (double)bw);
        return 0;
    }

    /* Main SF ≥ 10 → chip only allows 1 side detector. */
    uint8_t maxSides = (sf >= 10) ? 1 : 3;

    LR2021LoRaSideDetector_t cfg[3];
    uint8_t n = 0;
    uint8_t minSf = sf;
    for (size_t i = 0; i < sizeof(targetSFs) && n < maxSides; i++) {
        uint8_t tgt = targetSFs[i];
        if (tgt <= sf) continue;                   /* sides must be > main */
        if (tgt - minSf > 4) continue;             /* span rule */
        cfg[n].sf = tgt;
        cfg[n].ldro = false;
        cfg[n].invertIQ = false;
        cfg[n].syncWord = MESHTASTIC_LORA_SYNCWORD;
        sideSF[n] = tgt;
        n++;
    }

    if (n == 0) {
        LOG_INFO("LR2021: multi-SF listen — no valid side detectors for main SF%u", sf);
        return 0;
    }

    int16_t res = lora.setSideDetector(cfg, n);
    if (res != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR2021: setSideDetector failed (%d)", res);
        sideSF[0] = sideSF[1] = sideSF[2] = 0;
        return 0;
    }
    numSideDets = n;

    qprintk("LR2021   : multi-SF listen active — main=SF%u sides=", sf);
    for (uint8_t i = 0; i < n; i++) qprintk("SF%u%s", sideSF[i], (i + 1 < n) ? "," : "");
    qprintk("\n");
    return n;
}

uint8_t LR2021Interface::getLastRxDetector()
{
    uint8_t cr_;
    bool crc_;
    uint8_t detMask = 0;
    int16_t res = lora.getLoRaPacketStatus(&cr_, &crc_, NULL, NULL, NULL, NULL, &detMask);
    if (res != RADIOLIB_ERR_NONE) return 0xFF;
    /* One-hot → index. 0001=0, 0010=1, 0100=2, 1000=3. */
    switch (detMask) {
        case 0x01: return 0;
        case 0x02: return 1;
        case 0x04: return 2;
        case 0x08: return 3;
        default:   return 0xFF;  /* unexpected / zero mask */
    }
}

/* ---------- wideLora ----------------------------------------------------- */

bool LR2021Interface::wideLora()
{
    return true;
}

/* ---------- getPacketTime ------------------------------------------------ */

uint32_t LR2021Interface::getPacketTime(uint32_t pl, bool received)
{
    ARG_UNUSED(received);
    /* Airtime is correctly charged at the actual TX SF: completeSending() calls
     * this before startReceive() restores main SF, so lora.getTimeOnAir() uses
     * the peer SF's cached modulation params for retargeted TXs. */
    return (uint32_t)lora.getTimeOnAir(pl) / 1000;
}

/* ---------- Relay-on-received-SF helpers --------------------------------- */

void LR2021Interface::recordRxSf(NodeNum from, PacketId id, uint8_t rxSf)
{
    rxSfRing[rxSfRingCursor] = {from, id, rxSf, (uint32_t)k_uptime_get()};
    rxSfRingCursor = (rxSfRingCursor + 1) % RX_SF_RING_LEN;
}

uint8_t LR2021Interface::lookupRxSf(NodeNum from, PacketId id) const
{
    uint32_t now = (uint32_t)k_uptime_get();
    for (size_t i = 0; i < RX_SF_RING_LEN; i++) {
        if (rxSfRing[i].from == from && rxSfRing[i].id == id &&
            (now - rxSfRing[i].whenMs) < 30000)
            return rxSfRing[i].rxSf;
    }
    return 0;
}

/* ---------- Home SF helpers ---------------------------------------------- */

bool LR2021Interface::setHomeSf(uint8_t newSf)
{
    if (newSf == 0) { homeSf = 0; return true; }
    if (newSf == sf) { homeSf = newSf; return true; }
    for (uint8_t i = 0; i < 3; i++) {
        if (sideSF[i] == newSf) { homeSf = newSf; return true; }
    }
    qprintk("LR2021   : setHomeSf(%u) rejected — not in active SF set\n", newSf);
    return false;
}

#endif /* USE_LR2021 */
