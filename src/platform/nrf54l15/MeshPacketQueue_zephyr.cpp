/*
 * MeshPacketQueue_zephyr.cpp — Zephyr build of MeshPacketQueue.
 *
 * Identical to src/mesh/MeshPacketQueue.cpp except NodeDB.h is NOT
 * included.  isFromUs() and getFrom() are already provided by
 * mesh_zephyr.cpp (declared in MeshTypes.h).
 */
#ifdef ARCH_NRF54L15

#include "MeshPacketQueue.h"
#include "configuration.h"
#include <assert.h>
#include <algorithm>

/// @return the priority of the specified packet
inline uint32_t getPriority(const meshtastic_MeshPacket *p)
{
    return p->priority;
}

/// @return "true" if "p1" is ordered before "p2"
bool CompareMeshPacketFunc(const meshtastic_MeshPacket *p1, const meshtastic_MeshPacket *p2)
{
    assert(p1 && p2);

    if ((bool)p1->tx_after != (bool)p2->tx_after)
        return !p1->tx_after;

    auto p1p = getPriority(p1), p2p = getPriority(p2);
    return (p1p != p2p) ? (p1p > p2p) : (!isFromUs(p1) && isFromUs(p2));
}

MeshPacketQueue::MeshPacketQueue(size_t _maxLen) : maxLen(_maxLen) {}

bool MeshPacketQueue::empty()
{
    return queue.empty();
}

void fixPriority(meshtastic_MeshPacket *p)
{
    if (p->priority == meshtastic_MeshPacket_Priority_UNSET) {
        p->priority = (p->want_ack ? meshtastic_MeshPacket_Priority_RELIABLE
                                   : meshtastic_MeshPacket_Priority_DEFAULT);
        if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            if (p->decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
                p->priority = meshtastic_MeshPacket_Priority_ACK;
            } else if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
                       p->decoded.portnum == meshtastic_PortNum_ADMIN_APP) {
                p->priority = meshtastic_MeshPacket_Priority_HIGH;
            } else if (p->decoded.request_id != 0) {
                p->priority = meshtastic_MeshPacket_Priority_RESPONSE;
            } else if (p->decoded.want_response) {
                p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
            }
        }
    }
}

bool MeshPacketQueue::enqueue(meshtastic_MeshPacket *p, bool *dropped)
{
    if (queue.size() >= maxLen) {
        bool replaced = replaceLowerPriorityPacket(p);
        if (!replaced)
            LOG_WARN("TX queue full, no lower-priority packet to evict for 0x%08x", p->id);
        if (dropped) *dropped = true;
        return replaced;
    }
    if (dropped) *dropped = false;
    auto it = std::upper_bound(queue.begin(), queue.end(), p, CompareMeshPacketFunc);
    queue.insert(it, p);
    return true;
}

meshtastic_MeshPacket *MeshPacketQueue::dequeue()
{
    if (empty()) return NULL;
    auto *p = queue.front();
    queue.erase(queue.begin());
    return p;
}

meshtastic_MeshPacket *MeshPacketQueue::getFront()
{
    if (empty()) return NULL;
    return queue.front();
}

meshtastic_MeshPacket *MeshPacketQueue::getPacketFromQueue(NodeNum from, PacketId id)
{
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        auto p = *it;
        if (getFrom(p) == from && p->id == id)
            return p;
    }
    return NULL;
}

meshtastic_MeshPacket *MeshPacketQueue::remove(NodeNum from, PacketId id,
                                                bool tx_normal, bool tx_late,
                                                uint8_t hop_limit_lt)
{
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        auto p = *it;
        if (getFrom(p) == from && p->id == id &&
            ((tx_normal && !p->tx_after) || (tx_late && p->tx_after)) &&
            (!hop_limit_lt || p->hop_limit < hop_limit_lt)) {
            queue.erase(it);
            return p;
        }
    }
    return NULL;
}

bool MeshPacketQueue::find(const NodeNum from, const PacketId id)
{
    return getPacketFromQueue(from, id) != NULL;
}

bool MeshPacketQueue::replaceLowerPriorityPacket(meshtastic_MeshPacket *p)
{
    if (queue.empty()) return false;

    auto *backPacket = queue.back();
    if (!backPacket->tx_after && backPacket->priority < p->priority) {
        LOG_WARN("Dropping packet 0x%08x for higher-priority 0x%08x", backPacket->id, p->id);
        queue.pop_back();
        packetPool.release(backPacket);
        enqueue(p);
        return true;
    }

    if (backPacket->tx_after) {
        auto it = queue.end();
        auto refPacket = *--it;
        for (; refPacket->tx_after && it != queue.begin(); refPacket = *--it)
            ;
        if (!refPacket->tx_after && refPacket->priority < p->priority) {
            LOG_WARN("Dropping non-late packet 0x%08x for higher-priority 0x%08x", refPacket->id, p->id);
            queue.erase(it);
            packetPool.release(refPacket);
            enqueue(p);
            return true;
        }

        auto now = millis();
        if (backPacket->tx_after < now && (!p->tx_after || backPacket->tx_after > p->tx_after)) {
            LOG_WARN("Dropping late packet 0x%08x for packet 0x%08x", backPacket->id, p->id);
            queue.pop_back();
            packetPool.release(backPacket);
            enqueue(p);
            return true;
        }
    }

    return false;
}

#endif /* ARCH_NRF54L15 */
