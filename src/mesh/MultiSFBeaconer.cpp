#include "MultiSFBeaconer.h"

#include <zephyr/sys/printk.h>

#include "LR2021Interface.h"
#include "MeshService.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "mesh/Default.h"
#include "modules/NodeInfoModule.h"

MultiSFBeaconer *multiSFBeaconer = nullptr;

MultiSFBeaconer::MultiSFBeaconer(LR2021Interface *iface)
    : OSThread("MultiSFBeacon"), radioIf(iface)
{
    refreshRotation();

    if (rotationLen <= 1) {
        qprintk("MultiSFBeacon: disabled (no side detectors active)\n");
        disable();
        return;
    }

    /* After boot, burst a NodeInfo on every SF so peers on all SFs learn our
     * current public key immediately — don't wait for the round-robin to cycle
     * through each one (could take rotationLen × broadcast_interval seconds).
     * Stagger the first burst tick by 60s so NodeInfoModule's own boot send
     * goes out first and populates transmitHistory. */
    bootBurstRemaining = rotationLen;
    setIntervalFromNow(60 * 1000);

    qprintk("MultiSFBeacon: rotation [");
    for (uint8_t i = 0; i < rotationLen; i++) {
        qprintk("SF%u%s", rotation[i], (i + 1 < rotationLen) ? "," : "");
    }
    qprintk("] interval=%us\n",
           (unsigned)(default_node_info_broadcast_secs / rotationLen));
}

void MultiSFBeaconer::refreshRotation()
{
    rotationLen = 0;
    cursor = 0;

    if (radioIf == nullptr) return;

    rotation[rotationLen++] = radioIf->getMainSf();
    for (uint8_t slot = 0; slot < 3; slot++) {
        uint8_t s = radioIf->getSideSf(slot);
        if (s != 0) rotation[rotationLen++] = s;
    }
}

int32_t MultiSFBeaconer::runOnce()
{
    if (!nodeInfoModule || !radioIf) {
        /* Not ready yet — check again in a second. */
        return 1000;
    }

    /* Pick up main-SF changes from runtime config edits. */
    if (rotation[0] != radioIf->getMainSf()) {
        refreshRotation();
        if (rotationLen <= 1) {
            qprintk("MultiSFBeacon: main SF changed, no sides available — sleeping\n");
            return default_node_info_broadcast_secs * 1000;
        }
    }

    uint8_t sfToSend = rotation[cursor];
    cursor = (cursor + 1) % rotationLen;

    /* Let the radio know to retarget the next broadcast to this SF. Main-SF
     * slot is handled naturally (forcedNextTxSf == main → no retarget). */
    radioIf->setForcedNextTxSf(sfToSend);

    if (bootBurstRemaining > 0) {
        bootBurstRemaining--;
        qprintk("MultiSFBeacon: boot burst NodeInfo on SF%u (%u remaining)\n",
               sfToSend, bootBurstRemaining);
        sendNodeInfoDirect();
        return 5 * 1000;
    }

    qprintk("MultiSFBeacon: broadcasting NodeInfo on SF%u (slot %u/%u)\n",
           sfToSend, (unsigned)((cursor == 0) ? rotationLen : cursor), rotationLen);

    nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, false);

    /* Next SF in (node_info_broadcast_secs / rotationLen). */
    return (default_node_info_broadcast_secs * 1000) / rotationLen;
}

void MultiSFBeaconer::sendNodeInfoDirect()
{
    meshtastic_User u = owner;
    strcpy(u.id, nodeDB->getNodeId().c_str());

    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = NODENUM_BROADCAST;
    p->decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                           &meshtastic_User_msg, &u);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}
