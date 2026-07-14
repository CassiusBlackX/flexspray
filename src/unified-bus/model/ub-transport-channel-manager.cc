// SPDX-License-Identifier: GPL-2.0-only
#include "ub-transport-channel-manager.h"

#include "ub-controller.h"
#include "ub-network-address.h"
#include "ub-switch.h"
#include "ub-tp-connection-manager.h"
#include "ub-utils.h"
#include "protocol/ub-congestion-control.h"
#include "protocol/ub-transaction.h"
#include "protocol/ub-transport.h"

#include "ns3/log.h"
#include "ns3/node.h"

#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("UbTransportChannelManager");
NS_OBJECT_ENSURE_REGISTERED(UbTransportChannelManager);

TypeId
UbTransportChannelManager::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UbTransportChannelManager")
                            .SetParent<Object>()
                            .SetGroupName("UnifiedBus")
                            .AddConstructor<UbTransportChannelManager>();
    return tid;
}

void
UbTransportChannelManager::Init(UbController* controller)
{
    NS_ABORT_MSG_IF(controller == nullptr, "Transport channel manager requires a Controller");
    m_controller = controller;
}

Ptr<UbTransportChannel>
UbTransportChannelManager::GetTp(uint32_t tpn) const
{
    const auto it = m_endpoints.find(tpn);
    return it == m_endpoints.end() ? nullptr : it->second.channel;
}

bool
UbTransportChannelManager::CreateTp(uint32_t src,
                                    uint32_t dest,
                                    uint8_t sport,
                                    uint8_t dport,
                                    UbPriority priority,
                                    uint32_t srcTpn,
                                    uint32_t dstTpn,
                                    Ptr<UbCongestionControl> congestionCtrl)
{
    NS_LOG_FUNCTION(this << src << dest << sport << dport << priority << srcTpn);
    if (m_endpoints.find(srcTpn) != m_endpoints.end())
    {
        NS_LOG_DEBUG("Transport channel already exists");
        return false;
    }

    Ptr<Node> currentNode = m_controller->GetObject<Node>();
    if (!currentNode)
    {
        NS_LOG_WARN("No associated node found for UbController");
        currentNode = CreateObject<Node>();
    }

    const Ipv4Address sip = NodeIdToIp(src, sport);
    const Ipv4Address dip = NodeIdToIp(dest, dport);
    Ptr<UbTransportChannel> tp = CreateObject<UbTransportChannel>();
    tp->SetUbTransport(currentNode->GetId(),
                       src,
                       dest,
                       srcTpn,
                       dstTpn,
                       0,
                       static_cast<uint8_t>(priority),
                       static_cast<uint16_t>(sport),
                       static_cast<uint16_t>(dport),
                       sip,
                       dip,
                       congestionCtrl);
    tp->SetLifecycleStateChangedCallback(
        MakeCallback(&UbTransportChannelManager::NotifyStateChanged, this));
    UbLifecycleCoordinator::GetOrCreate(currentNode)->SetResourceStateChangedCallback(
        tp->GetLifecycleOwner(),
        MakeCallback(&UbTransportChannelManager::NotifyLifecycleStateChanged, this));
    m_controller->GetUbTransaction()->TpInit(tp);
    m_endpoints.emplace(srcTpn, EndpointRecord{tp});
    ++m_transportsCount;
    currentNode->GetObject<UbSwitch>()->RegisterTpWithAllocator(tp, sport, priority);
    utils::UbUtils::Get()->SingleTpTraceConnect(src, srcTpn);
    NS_LOG_DEBUG("Created transport channel success");
    return true;
}

Ptr<UbTransportChannel>
UbTransportChannelManager::CreateReservedTpEndpoint(uint32_t tpn)
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    return CreateReservedTpEndpointLocked(tpn);
}

Ptr<UbTransportChannel>
UbTransportChannelManager::CreateReservedTpEndpointLocked(uint32_t tpn)
{
    Ptr<UbTransportChannel> existingTp = GetTp(tpn);
    if (existingTp != nullptr)
    {
        return existingTp;
    }

    Ptr<Node> currentNode = m_controller->GetObject<Node>();
    NS_ASSERT_MSG(currentNode != nullptr, "Reserved TP endpoint requires an attached node");
    NS_ASSERT_MSG(m_controller->GetUbTransaction() != nullptr,
                  "Reserved TP endpoint requires UbTransaction");
    Ptr<TpConnectionManager> connections = m_controller->GetTpConnManager();
    NS_ASSERT_MSG(connections != nullptr,
                  "Reserved TP endpoint requires TpConnectionManager");

    Connection conn;
    if (!connections->TryGetLocalConnection(currentNode->GetId(), tpn, conn))
    {
        return nullptr;
    }

    auto congestionCtrl = UbCongestionControl::Create(UB_DEVICE);
    CreateTp(conn.node1,
             conn.node2,
             conn.port1,
             conn.port2,
             static_cast<UbPriority>(conn.priority),
             conn.tpn1,
             conn.tpn2,
             congestionCtrl);
    return GetTp(tpn);
}

Ptr<UbTransportChannel>
UbTransportChannelManager::ResolveInboundTp(uint32_t srcTpn,
                                            uint32_t dstTpn,
                                            Ipv4Address sourceIp,
                                            Ipv4Address destinationIp,
                                            UbPriority priority)
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    Ptr<UbTransportChannel> targetTp = GetTp(dstTpn);

    uint32_t expectedSrcTpn;
    Ipv4Address expectedSourceIp;
    Ipv4Address expectedDestinationIp;
    UbPriority expectedPriority;
    if (targetTp != nullptr)
    {
        expectedSrcTpn = targetTp->GetDestTpn();
        expectedSourceIp = targetTp->GetDip();
        expectedDestinationIp = targetTp->GetSip();
        expectedPriority = static_cast<UbPriority>(targetTp->GetPriority());
    }
    else
    {
        Ptr<Node> currentNode = m_controller->GetObject<Node>();
        NS_ASSERT_MSG(currentNode != nullptr, "Inbound TP resolution requires an attached node");
        Connection conn;
        if (!m_controller->GetTpConnManager()->TryGetLocalConnection(currentNode->GetId(),
                                                                      dstTpn,
                                                                      conn))
        {
            return nullptr;
        }
        expectedSrcTpn = conn.tpn2;
        expectedSourceIp = NodeIdToIp(conn.node2, conn.port2);
        expectedDestinationIp = NodeIdToIp(conn.node1, conn.port1);
        expectedPriority = static_cast<UbPriority>(conn.priority);
    }

    if (srcTpn != expectedSrcTpn || sourceIp != expectedSourceIp ||
        destinationIp != expectedDestinationIp || priority != expectedPriority)
    {
        return nullptr;
    }
    return targetTp != nullptr ? targetTp : CreateReservedTpEndpointLocked(dstTpn);
}

UbDestroyStatus
UbTransportChannelManager::DestroyTp(uint32_t tpn,
                                     UbDestroyReason reason,
                                     UbDestroyCallback callback)
{
    auto it = m_endpoints.find(tpn);
    if (it == m_endpoints.end())
    {
        return UbDestroyStatus::NOT_FOUND;
    }
    EndpointRecord& record = it->second;
    record.state = EndpointState::DRAINING;
    record.reason = reason;
    if (!callback.IsNull())
    {
        record.callbacks.push_back(callback);
    }
    record.channel->FreezeNewWork();
    it = m_endpoints.find(tpn);
    if (it == m_endpoints.end())
    {
        return UbDestroyStatus::COMPLETED;
    }
    if (!CanFinalize(it->second))
    {
        return UbDestroyStatus::PENDING;
    }
    FinalizeDestroy(tpn, false);
    return UbDestroyStatus::COMPLETED;
}

bool
UbTransportChannelManager::CanFinalize(const EndpointRecord& record) const
{
    const Ptr<UbTransportChannel>& tp = record.channel;
    const UbLifecycleResourceSnapshot snapshot =
        UbLifecycleCoordinator::GetOrCreate(m_controller->GetObject<Node>())
            ->GetSnapshot(tp->GetLifecycleOwner());
    const bool lifecycleIdle = snapshot.inFlightPackets == 0 && snapshot.activeWorkLeases == 0 &&
                               snapshot.activeChildLeases == 0 &&
                               snapshot.activeTransportUseLeases == 0 &&
                               snapshot.activeBindingLeases == 0 &&
                               snapshot.activeTrackedEventLeases == 0 &&
                               snapshot.activeTransportWorkLeases == 0;
    Ptr<UbTransaction> transaction = m_controller->GetUbTransaction();
    return lifecycleIdle && tp->IsSafeForExplicitDestroy() &&
           !transaction->IsTpInUse(tp->GetTpn()) &&
           !transaction->HasPendingTransportWork(tp->GetTpn());
}

void
UbTransportChannelManager::NotifyStateChanged(uint32_t tpn)
{
    const auto it = m_endpoints.find(tpn);
    if (it != m_endpoints.end() && it->second.state == EndpointState::DRAINING &&
        CanFinalize(it->second))
    {
        FinalizeDestroy(tpn, false);
    }
}

void
UbTransportChannelManager::NotifyLifecycleStateChanged(UbLifecycleResourceIdentity identity)
{
    if (identity.kind == UbLifecycleResourceKind::RTP_ENDPOINT &&
        identity.localId <= std::numeric_limits<uint32_t>::max())
    {
        NotifyStateChanged(static_cast<uint32_t>(identity.localId));
    }
}

void
UbTransportChannelManager::FinalizeDestroy(uint32_t tpn, bool force)
{
    const auto it = m_endpoints.find(tpn);
    NS_ABORT_MSG_IF(it == m_endpoints.end(), "Cannot finalize a missing TP endpoint");
    NS_ABORT_MSG_IF(!force && !CanFinalize(it->second), "Cannot finalize a busy TP endpoint");

    Ptr<UbTransportChannel> tp = it->second.channel;
    const UbLifecycleResourceIdentity identity = tp->GetLifecycleOwner();
    std::vector<UbDestroyCallback> callbacks = std::move(it->second.callbacks);
    tp->PrepareForExplicitDestroy();
    Ptr<UbLifecycleCoordinator> coordinator =
        UbLifecycleCoordinator::GetOrCreate(m_controller->GetObject<Node>());
    if (coordinator->IsResourceActive(identity))
    {
        coordinator->SetResourceStateChangedCallback(identity, {});
    }
    if (!force)
    {
        m_controller->GetObject<Node>()->GetObject<UbSwitch>()->RemoveTpFromAllocator(tp);
        m_controller->GetUbTransaction()->TpDeinit(tpn);
        coordinator->RetirePacketOwner(identity);
    }
    m_endpoints.erase(it);
    if (m_transportsCount > 0)
    {
        --m_transportsCount;
    }
    for (const UbDestroyCallback& callback : callbacks)
    {
        callback(identity, UbDestroyStatus::COMPLETED);
    }
}

void
UbTransportChannelManager::ForceDispose()
{
    std::vector<uint32_t> tpns;
    tpns.reserve(m_endpoints.size());
    for (const auto& [tpn, record] : m_endpoints)
    {
        record.channel->MarkOwnerControllerDisposing();
        tpns.push_back(tpn);
    }
    for (uint32_t tpn : tpns)
    {
        FinalizeDestroy(tpn, true);
    }
}

bool
UbTransportChannelManager::Contains(uint32_t tpn) const
{
    return m_endpoints.find(tpn) != m_endpoints.end();
}

uint32_t
UbTransportChannelManager::GetCount() const
{
    return static_cast<uint32_t>(m_endpoints.size());
}

std::map<uint32_t, Ptr<UbTransportChannel>>
UbTransportChannelManager::GetTpnMap() const
{
    std::map<uint32_t, Ptr<UbTransportChannel>> result;
    for (const auto& [tpn, record] : m_endpoints)
    {
        result.emplace(tpn, record.channel);
    }
    return result;
}

} // namespace ns3
