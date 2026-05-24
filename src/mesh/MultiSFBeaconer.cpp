#include "MultiSFBeaconer.h"

#include <zephyr/sys/printk.h>

#include "LR2021Interface.h"
#include "MeshTypes.h"
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

    /* Stagger first tick by 60 s so boot-time NodeInfo from NodeInfoModule
     * lands first and doesn't collide with our first rotation beacon. */
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

    qprintk("MultiSFBeacon: broadcasting NodeInfo on SF%u (slot %u/%u)\n",
           sfToSend, (unsigned)((cursor == 0) ? rotationLen : cursor), rotationLen);

    nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, false);

    /* Next SF in (node_info_broadcast_secs / rotationLen). */
    return (default_node_info_broadcast_secs * 1000) / rotationLen;
}
