#pragma once
#ifdef USE_LR2021

#include "RadioLibInterface.h"
#include "modules/LR2021/LR2021.h"

/**
 * Meshtastic RadioInterface adapter for the Semtech LR2021.
 *
 * Extends RadioLibInterface using RadioLib's LR2021 PhysicalLayer driver,
 * following the same pattern as LR11x0Interface and SX1280Interface.
 * Gets collision avoidance, ISR-driven TX/RX, and packet lifecycle for free.
 */
class LR2021Interface : public RadioLibInterface
{
  public:
    LR2021Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs,
                    RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);
    ~LR2021Interface() override;

    /* RadioLibInterface pure virtuals */
    void disableInterrupt() override;
    void enableInterrupt(void (*callback)()) override;
    bool isChannelActive() override;
    bool isActivelyReceiving() override;
    void addReceiveMetadata(meshtastic_MeshPacket *mp) override;

    /* RadioLibInterface overrides */
    bool init() override;
    bool reconfigure() override;
    bool sleep() override;
    void startReceive() override;
    void configHardwareForSend() override;
    void setStandby() override;

    bool wideLora() override;

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override;

    /* Multi-SF TX retargeting (Phase 3). Before TX, looks up the destination's
     * last-heard SF in NodeSFTracker; if it differs from our main SF, temporarily
     * switches modulation so the peer can hear us, then restores main SF + side
     * detectors after TX via startReceive(). Broadcasts keep main SF. */
    bool startSend(meshtastic_MeshPacket *txp) override;

    /* Multi-SF listen (Phase 1 POC) — configures side detectors on top of the
     * current main SF. Side detectors must be LARGER SFs than the main SF and
     * within (maxSF - minSF) ≤ 4. If main SF is 10/11/12, only one side
     * detector is allowed by the chip. No-op if no valid sides can be added.
     * Returns the number of side detectors actually installed (0..3). */
    uint8_t configureMultiSFListen();

    /* Reads the detector index of the last received packet.
     *  0 = main, 1 = side1, 2 = side2, 3 = side3, 0xFF = unknown / read failed.
     * Must be called after a successful RX, before the next startReceive(). */
    uint8_t getLastRxDetector();

    /* Multi-SF broadcast round-robin (Phase 4). Force the next broadcast TX
     * to use the given SF. Used by the NodeInfo beaconer right before it
     * triggers nodeInfoModule->sendOurNodeInfo() — the next broadcast in the
     * queue is its own beacon. */
    void setForcedNextTxSf(uint8_t sf) { forcedNextTxSf = sf; }

    /* Multi-SF cross-SF flood relay (Phase 6). Pointer-keyed force: when
     * startSend() sees a packet whose pointer matches one we've registered,
     * it retargets that specific TX to the registered SF. This avoids the
     * ID collision between a fan-out clone and the FloodingRouter's normal
     * relay (both share the original packet ID). Up to 4 in flight at once. */
    void registerForcedTx(meshtastic_MeshPacket *pkt, uint8_t sf);

    /* Override to protect cross-SF fan-out clones from being canceled by
     * perhapsCancelDupe(). When the bridge re-hears its main-SF relay
     * rebroadcast, Meshtastic calls cancelSending(originalFrom, originalId)
     * to evict pending TXs matching that key — but our fan-out clones share
     * that key on purpose. Refuse the cancel when only fan-out clones match. */
    bool cancelSending(NodeNum from, PacketId id) override;

    /* Current main SF (what the LoRa config says) and the list of active side
     * detector SFs — used by MultiSFBeaconer to build the rotation list. */
    uint8_t getMainSf() const { return sf; }
    uint8_t getSideSf(uint8_t slot) const { return (slot < 3) ? sideSF[slot] : 0; }

    bool setHomeSf(uint8_t sf);
    uint8_t getHomeSf() const { return homeSf; }

  private:
    LR2021 lora;

    float tcxoVoltage = 1.8f; /* Wio-LR2021 board uses 1.8V TCXO */

    /* Phase 1: track what SF each side-detector slot is set to, so RX logs can
     * translate detector index → SF. sideSF[0] = side1, [1] = side2, [2] = side3.
     * Zero = slot unused. */
    uint8_t sideSF[3] = {0, 0, 0};
    uint8_t numSideDets = 0;

    /* Phase 3: if non-zero, we temporarily switched to a peer's SF for a TX;
     * startReceive() needs to restore this as main SF and re-install side
     * detectors. Zero = no temp switch pending. */
    uint8_t savedMainSf = 0;

    /* Phase 4: one-shot forced SF for the next broadcast TX (beaconer). */
    uint8_t forcedNextTxSf = 0;

    /* Phase 6: pointer-keyed force list (cross-SF fan-out clones). */
    struct PtrForce { meshtastic_MeshPacket *pkt; uint8_t sf; };
    PtrForce ptrForce[4] = {};

    /* Relay-on-received-SF: ring buffer tracks which SF each inbound packet
     * arrived on so relays retransmit on that same SF. Covers both broadcasts
     * (preserves per-SF flood domain) and unicast DMs to unknown destinations
     * (the sender's SF is the best guess for reaching the destination). */
    struct RxSfEntry { NodeNum from; PacketId id; uint8_t rxSf; uint32_t whenMs; };
    static constexpr size_t RX_SF_RING_LEN = 128;
    RxSfEntry rxSfRing[RX_SF_RING_LEN] = {};
    size_t rxSfRingCursor = 0;

    void recordRxSf(NodeNum from, PacketId id, uint8_t rxSf);
    uint8_t lookupRxSf(NodeNum from, PacketId id) const;

    /* Home SF: the SF this bridge "lives on" for originated traffic.
     * 0 = use main SF (default). Must be in {main, side1, side2, side3}.
     * Set at build time via -DMULTI_SF_HOME_SF=8 or at runtime via setHomeSf(). */
    uint8_t homeSf = 0;
};

#endif /* USE_LR2021 */
