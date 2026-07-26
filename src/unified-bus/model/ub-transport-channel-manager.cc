// SPDX-License-Identifier: GPL-2.0-only
#include "ub-transport-channel-manager.h"

#include "protocol/ub-congestion-control.h"
#include "protocol/ub-transaction.h"
#include "protocol/ub-transport.h"
#include "ub-controller.h"
#include "ub-network-address.h"
#include "ub-switch.h"
#include "ub-tp-connection-manager.h"
#include "ub-utils.h"

#include "ns3/log.h"
#ifdef NS3_MTP
#include "ns3/mtp-interface.h"
#endif
#include "ns3/node.h"
#include "ns3/object-factory.h"

#include <limits>

namespace ns3
{

namespace
{

bool
IsTpBackingExecutionOwner(uint32_t nodeId)
{
#ifdef NS3_MTP
    if (MtpInterface::isEnabled())
    {
        if (!MtpInterface::isPartitioned())
        {
            return true;
        }
        const std::optional<uint32_t> ownerLp = MtpInterface::FindNodeLocalLpId(nodeId);
        LogicalProcess* currentLp = MtpInterface::GetSystem();
        if (!ownerLp.has_value() || currentLp == nullptr ||
            *ownerLp != currentLp->GetSystemId())
        {
            return false;
        }
    }
#endif
    return UbParallelLifecycleAdapter::IsExecutionOwner(nodeId);
}

} // namespace

NS_LOG_COMPONENT_DEFINE("UbTransportChannelManager");
NS_OBJECT_ENSURE_REGISTERED(UbTransportChannelManager);

TypeId
UbTransportChannelManager::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UbTransportChannelManager")
                            .SetParent<Object>()
                            .SetGroupName("UnifiedBus")
                            .AddConstructor<UbTransportChannelManager>()
                            .AddAttribute(
                                "EnableAutomaticBackingReclaim",
                                "Release reconstructible TP backing after owner-local lifecycle "
                                "gates pass.",
                                BooleanValue(true),
                                MakeBooleanAccessor(
                                    &UbTransportChannelManager::m_enableAutomaticBackingReclaim),
                                MakeBooleanChecker());
    return tid;
}

void
UbTransportChannelManager::Init(UbController* controller)
{
    NS_ABORT_MSG_IF(controller == nullptr, "Transport channel manager requires a Controller");
    m_controller = controller;
}

Ptr<UbTransportChannel>
UbTransportChannelManager::PeekLiveTp(uint32_t tpn) const
{
    const auto it = m_endpoints.find(tpn);
    return it == m_endpoints.end() ? nullptr : it->second.channel;
}

Ptr<UbTransportChannel>
UbTransportChannelManager::GetTp(uint32_t tpn) const
{
    return PeekLiveTp(tpn);
}

bool
UbTransportChannelManager::TryGetEndpointIdentity(uint32_t tpn,
                                                  UbLifecycleResourceIdentity& identity) const
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    return TryGetEndpointIdentityLocked(tpn, identity);
}

bool
UbTransportChannelManager::TryGetEndpointIdentityLocked(uint32_t tpn,
                                                        UbLifecycleResourceIdentity& identity) const
{
    const auto it = m_endpoints.find(tpn);
    if (it == m_endpoints.end() || it->second.state == EndpointState::DRAINING)
    {
        return false;
    }
    const EndpointRecord& record = it->second;
    if (record.state == EndpointState::LIVE)
    {
        if (record.channel == nullptr)
        {
            return false;
        }
        identity = record.channel->GetLifecycleOwner();
        return true;
    }
    if (record.dormantState == nullptr)
    {
        return false;
    }
    identity = record.dormantState->lifecycleIdentity;
    return true;
}

bool
UbTransportChannelManager::CreateTp(uint32_t src,
                                    uint32_t dest,
                                    uint8_t sport,
                                    uint8_t dport,
                                    UbPriority priority,
                                    uint32_t srcTpn,
                                    uint32_t dstTpn,
                                    Ptr<UbCongestionControl> congestionCtrl,
                                    bool drainOnly)
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
    if (drainOnly)
    {
        tp->FreezeNewWork();
    }
    tp->SetLifecycleStateChangedCallback(
        MakeCallback(&UbTransportChannelManager::NotifyStateChanged, this));
    UbLifecycleCoordinator::GetOrCreate(currentNode)
        ->SetResourceStateChangedCallback(
            tp->GetLifecycleOwner(),
            MakeCallback(&UbTransportChannelManager::NotifyLifecycleStateChanged, this));
    m_controller->GetUbTransaction()->TpInit(tp);
    m_endpoints.emplace(srcTpn, EndpointRecord{tp});
    ++m_transportsCount;
    utils::UbUtils::NotifyTpBackingMaterialized();
    currentNode->GetObject<UbSwitch>()->RegisterTpWithAllocator(tp, sport, priority);
    m_controller->GetTpConnManager()->NotifyEndpointMaterialized(currentNode->GetId(),
                                                                  srcTpn,
                                                                  drainOnly);
    utils::UbUtils::Get()->SingleTpTraceConnect(src, srcTpn);
    NS_LOG_DEBUG("Created transport channel success");
    return true;
}

Ptr<UbTransportChannel>
UbTransportChannelManager::CreateReservedTpEndpoint(uint32_t tpn)
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    Ptr<Node> currentNode = m_controller->GetObject<Node>();
    if (currentNode != nullptr &&
        m_controller->GetTpConnManager()->IsConnectionDestroyRequested(currentNode->GetId(), tpn))
    {
        return nullptr;
    }
    return CreateReservedTpEndpointLocked(tpn);
}

Ptr<UbTransportChannel>
UbTransportChannelManager::CreateReservedTpEndpointLocked(uint32_t tpn, bool drainOnly)
{
    const auto existing = m_endpoints.find(tpn);
    if (existing != m_endpoints.end())
    {
        if (existing->second.state == EndpointState::LIVE)
        {
            return existing->second.channel;
        }
        UbLifecycleResourceIdentity identity;
        return TryGetEndpointIdentityLocked(tpn, identity) ? ResolveLiveTpLocked(tpn, identity)
                                                           : nullptr;
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
             congestionCtrl,
             drainOnly);
    if (drainOnly)
    {
        Simulator::ScheduleNow(&UbTransportChannelManager::BeginDrainOnlyDestroy, this, tpn);
    }
    return PeekLiveTp(tpn);
}

void
UbTransportChannelManager::BeginDrainOnlyDestroy(uint32_t tpn)
{
    Ptr<Node> node = m_controller->GetObject<Node>();
    if (node != nullptr)
    {
        m_controller->GetTpConnManager()->DestroyDrainOnlyEndpoint(node->GetId(), tpn);
    }
}

Ptr<UbTransportChannel>
UbTransportChannelManager::ResolveInboundTp(uint32_t srcTpn,
                                            uint32_t dstTpn,
                                            Ipv4Address sourceIp,
                                            Ipv4Address destinationIp,
                                            UbPriority priority)
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    const auto endpoint = m_endpoints.find(dstTpn);
    if (endpoint != m_endpoints.end())
    {
        const EndpointRecord& record = endpoint->second;
        const UbTransportDormantState* dormant = record.dormantState.get();
        if (record.state == EndpointState::DRAINING ||
            (record.channel == nullptr && dormant == nullptr))
        {
            return nullptr;
        }
        const uint32_t expectedSrcTpn =
            record.channel != nullptr ? record.channel->GetDestTpn() : dormant->dstTpn;
        const Ipv4Address expectedSourceIp =
            record.channel != nullptr ? record.channel->GetDip() : dormant->dip;
        const Ipv4Address expectedDestinationIp =
            record.channel != nullptr ? record.channel->GetSip() : dormant->sip;
        const UbPriority expectedPriority = static_cast<UbPriority>(
            record.channel != nullptr ? record.channel->GetPriority() : dormant->priority);
        if (srcTpn != expectedSrcTpn || sourceIp != expectedSourceIp ||
            destinationIp != expectedDestinationIp || priority != expectedPriority)
        {
            return nullptr;
        }
        UbLifecycleResourceIdentity identity;
        return TryGetEndpointIdentityLocked(dstTpn, identity)
                   ? ResolveLiveTpLocked(dstTpn, identity)
                   : nullptr;
    }

    Ptr<Node> currentNode = m_controller->GetObject<Node>();
    NS_ASSERT_MSG(currentNode != nullptr, "Inbound TP resolution requires an attached node");
    Connection conn;
    if (!m_controller->GetTpConnManager()->TryGetLocalConnection(currentNode->GetId(),
                                                                 dstTpn,
                                                                 conn))
    {
        return nullptr;
    }
    if (srcTpn != conn.tpn2 || sourceIp != NodeIdToIp(conn.node2, conn.port2) ||
        destinationIp != NodeIdToIp(conn.node1, conn.port1) ||
        priority != static_cast<UbPriority>(conn.priority))
    {
        return nullptr;
    }
    const bool drainOnly =
        m_controller->GetTpConnManager()->IsConnectionDestroyRequested(currentNode->GetId(),
                                                                       dstTpn);
    return CreateReservedTpEndpointLocked(dstTpn, drainOnly);
}

bool
UbTransportChannelManager::ReclaimBackingState(uint32_t tpn)
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    return ReclaimBackingStateLocked(tpn);
}

UbTpQuiescenceAssessment
UbTransportChannelManager::AssessTpQuiescence(uint32_t tpn, UbTeardownIntent intent) const
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    const auto it = m_endpoints.find(tpn);
    if (it == m_endpoints.end())
    {
        UbTpQuiescenceAssessment assessment;
        assessment.tpn = tpn;
        assessment.intent = intent;
        assessment.AddBlocker(UbTpQuiescenceBlocker::ENDPOINT_MISSING);
        return assessment;
    }
    return AssessTpQuiescenceLocked(tpn, it->second, intent);
}

UbTpQuiescenceAssessment
UbTransportChannelManager::AssessTpQuiescenceLocked(uint32_t tpn,
                                                    const EndpointRecord& endpoint,
                                                    UbTeardownIntent intent) const
{
    static_assert(static_cast<uint8_t>(UbTpQuiescenceBlocker::COUNT) <= 64);

    UbTpQuiescenceAssessment assessment;
    assessment.tpn = tpn;
    assessment.intent = intent;

    if (intent == UbTeardownIntent::BACKING_RECLAIM && !m_enableAutomaticBackingReclaim)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::AUTOMATIC_RECLAIM_DISABLED);
        return assessment;
    }
    if (m_controller == nullptr)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::CONTROLLER_UNAVAILABLE);
        return assessment;
    }
    if (m_controller->IsDisposing())
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::CONTROLLER_DISPOSING);
        return assessment;
    }

    Ptr<Node> node = m_controller->GetObject<Node>();
    if (node == nullptr)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::NODE_UNAVAILABLE);
        return assessment;
    }
    if (!IsTpBackingExecutionOwner(node->GetId()))
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::WRONG_EXECUTION_OWNER);
        return assessment;
    }

    if (endpoint.state == EndpointState::DORMANT)
    {
        if (endpoint.dormantState == nullptr || endpoint.allocatorPlaceholder == nullptr)
        {
            assessment.AddBlocker(UbTpQuiescenceBlocker::BACKING_UNAVAILABLE);
            return assessment;
        }
        assessment.identity = endpoint.dormantState->lifecycleIdentity;
        return assessment;
    }
    if (intent == UbTeardownIntent::BACKING_RECLAIM && endpoint.state == EndpointState::DRAINING)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ENDPOINT_DRAINING);
        return assessment;
    }
    if (intent == UbTeardownIntent::EXPLICIT_DESTROY && endpoint.state != EndpointState::DRAINING)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ENDPOINT_NOT_DRAINING);
    }

    const Ptr<UbTransportChannel>& tp = endpoint.channel;
    if (tp == nullptr)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::BACKING_UNAVAILABLE);
        return assessment;
    }
    assessment.identity = tp->GetLifecycleOwner();
    if (assessment.identity.ownerNodeId != node->GetId())
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::LIFECYCLE_OWNER_MISMATCH);
        return assessment;
    }

    Ptr<UbLifecycleCoordinator> coordinator = node->GetObject<UbLifecycleCoordinator>();
    if (coordinator == nullptr)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::LIFECYCLE_UNAVAILABLE);
        return assessment;
    }
    if (!coordinator->IsResourceActive(assessment.identity))
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::LIFECYCLE_INACTIVE);
        return assessment;
    }
    assessment.diagnostics.lifecycle = coordinator->GetSnapshot(assessment.identity);
    const UbLifecycleResourceSnapshot& lifecycle = assessment.diagnostics.lifecycle;
    if (lifecycle.inFlightPackets != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::IN_FLIGHT_PACKETS);
    }
    if (lifecycle.activeWorkLeases != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ACTIVE_WORK_LEASE);
    }
    if (lifecycle.activeChildLeases != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ACTIVE_CHILD_LEASE);
    }
    if (lifecycle.activeTransportUseLeases != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ACTIVE_TRANSPORT_USE_LEASE);
    }
    if (lifecycle.activeBindingLeases != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ACTIVE_BINDING_LEASE);
    }
    if (lifecycle.activeTrackedEventLeases != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ACTIVE_TRACKED_EVENT_LEASE);
    }
    if (lifecycle.activeTransportWorkLeases != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ACTIVE_TRANSPORT_WORK_LEASE);
    }

    Ptr<UbTransaction> transaction = m_controller->GetUbTransaction();
    if (transaction == nullptr)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::TRANSACTION_UNAVAILABLE);
    }
    else
    {
        assessment.diagnostics.transactionReferenced = transaction->IsTpInUse(tpn);
        assessment.diagnostics.pendingTransportWork = transaction->HasPendingTransportWork(tpn);
        if (assessment.diagnostics.transactionReferenced)
        {
            assessment.AddBlocker(UbTpQuiescenceBlocker::TRANSACTION_REFERENCE);
        }
        if (assessment.diagnostics.pendingTransportWork)
        {
            assessment.AddBlocker(UbTpQuiescenceBlocker::PENDING_TRANSPORT_WORK);
        }
    }

    if (intent == UbTeardownIntent::BACKING_RECLAIM)
    {
        Ptr<UbSwitch> sw = node->GetObject<UbSwitch>();
        if (sw == nullptr)
        {
            assessment.AddBlocker(UbTpQuiescenceBlocker::SWITCH_UNAVAILABLE);
        }
        else if (sw->GetAllocator() == nullptr)
        {
            assessment.AddBlocker(UbTpQuiescenceBlocker::ALLOCATOR_UNAVAILABLE);
        }
        else
        {
            assessment.diagnostics.allocatorWork = sw->GetAllocator()->HasPendingOrActiveWork(tp);
            if (assessment.diagnostics.allocatorWork)
            {
                assessment.AddBlocker(UbTpQuiescenceBlocker::ALLOCATOR_WORK);
            }
        }
    }

    assessment.diagnostics.transport = tp->GetQuiescenceSnapshot();
    const UbTransportQuiescenceSnapshot& transport = assessment.diagnostics.transport;
    if (transport.pendingAckPackets != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::ACK_QUEUE);
    }
    if (transport.pendingCnpPackets != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::CNP_QUEUE);
    }
    if (transport.pendingWqeSegments != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::WQE_SEGMENT_QUEUE);
    }
    if (transport.pendingRemoteRequests != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::REMOTE_REQUEST);
    }
    if (transport.pendingInboundTaUnits != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::INBOUND_WORK);
    }
    if (transport.bufferedInboundPackets != 0)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::BUFFERED_INBOUND_PACKET);
    }
    if (transport.hasUnacknowledgedPsn)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::UNACKNOWLEDGED_PSN);
    }
    if (transport.retransmissionTimerRunning)
    {
        assessment.AddBlocker(UbTpQuiescenceBlocker::RETRANSMISSION_TIMER);
    }
    if (intent == UbTeardownIntent::BACKING_RECLAIM)
    {
        if (transport.outstandingRetransmissionState)
        {
            assessment.AddBlocker(UbTpQuiescenceBlocker::OUTSTANDING_RETRANSMISSION);
        }
        if (!transport.congestionControlPresent)
        {
            assessment.AddBlocker(UbTpQuiescenceBlocker::CONGESTION_CONTROL_UNAVAILABLE);
        }
        else if (transport.congestionControlDormancyEligibility !=
                 UbCongestionControlDormancyEligibility::ELIGIBLE)
        {
            assessment.AddBlocker(UbTpQuiescenceBlocker::CONGESTION_CONTROL_INELIGIBLE);
        }
    }
    return assessment;
}

bool
UbTransportChannelManager::ReclaimBackingStateLocked(uint32_t tpn)
{
    const auto it = m_endpoints.find(tpn);
    if (it == m_endpoints.end())
    {
        return false;
    }
    EndpointRecord& record = it->second;
    const UbTpQuiescenceAssessment assessment =
        AssessTpQuiescenceLocked(tpn, record, UbTeardownIntent::BACKING_RECLAIM);
    if (!assessment.IsAllowed())
    {
        return false;
    }
    if (record.state == EndpointState::DORMANT)
    {
        return true;
    }

    Ptr<Node> node = m_controller->GetObject<Node>();
    Ptr<UbTransportChannel> tp = record.channel;
    Ptr<UbTransaction> transaction = m_controller->GetUbTransaction();
    Ptr<UbSwitch> sw = node->GetObject<UbSwitch>();

    std::unique_ptr<UbTransportDormantState> dormantState = tp->SnapshotDormantState();
    if (dormantState == nullptr)
    {
        return false;
    }
    Ptr<UbIngressQueue> placeholder = sw->ReplaceTpWithDormantPlaceholder(tp);
    if (placeholder == nullptr)
    {
        return false;
    }
    if (!transaction->ReplaceTpBacking(tpn, tp, nullptr))
    {
        const bool rolledBack = sw->ReplaceDormantTpBacking(placeholder, tp);
        NS_ABORT_MSG_IF(!rolledBack, "Failed to roll back TP allocator replacement");
        return false;
    }

    tp->ReleaseBackingForDormancy();
    record.channel = nullptr;
    record.dormantState = std::move(dormantState);
    record.allocatorPlaceholder = placeholder;
    record.state = EndpointState::DORMANT;
    utils::UbUtils::NotifyTpBackingReleased();
    return true;
}

Ptr<UbTransportChannel>
UbTransportChannelManager::ResolveLiveTp(uint32_t tpn,
                                         const UbLifecycleResourceIdentity& expectedIdentity)
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    return ResolveLiveTpLocked(tpn, expectedIdentity);
}

Ptr<UbTransportChannel>
UbTransportChannelManager::ResolveLiveTpLocked(uint32_t tpn,
                                               const UbLifecycleResourceIdentity& expectedIdentity)
{
    if (m_controller == nullptr || m_controller->IsDisposing())
    {
        return nullptr;
    }
    Ptr<Node> node = m_controller->GetObject<Node>();
    if (node == nullptr || expectedIdentity.ownerNodeId != node->GetId() ||
        !IsTpBackingExecutionOwner(node->GetId()))
    {
        return nullptr;
    }
    const auto it = m_endpoints.find(tpn);
    if (it == m_endpoints.end() || it->second.state == EndpointState::DRAINING)
    {
        return nullptr;
    }
    EndpointRecord& record = it->second;
    if (record.state == EndpointState::LIVE)
    {
        return record.channel != nullptr && record.channel->GetLifecycleOwner() == expectedIdentity
                   ? record.channel
                   : nullptr;
    }
    if (record.dormantState == nullptr || record.allocatorPlaceholder == nullptr ||
        record.dormantState->lifecycleIdentity != expectedIdentity)
    {
        return nullptr;
    }

    Ptr<UbLifecycleCoordinator> coordinator = UbLifecycleCoordinator::GetOrCreate(node);
    if (!coordinator->IsResourceActive(expectedIdentity))
    {
        return nullptr;
    }

    ObjectFactory factory;
    factory.SetTypeId(record.dormantState->congestionControlType);
    Ptr<UbCongestionControl> congestionCtrl = factory.Create<UbCongestionControl>();
    Ptr<UbTransportChannel> tp = CreateObject<UbTransportChannel>();
    if (!tp->RestoreDormantState(*record.dormantState, congestionCtrl))
    {
        return nullptr;
    }
    tp->SetLifecycleStateChangedCallback(
        MakeCallback(&UbTransportChannelManager::NotifyStateChanged, this));

    Ptr<UbSwitch> sw = node->GetObject<UbSwitch>();
    if (sw == nullptr || !sw->ReplaceDormantTpBacking(record.allocatorPlaceholder, tp))
    {
        return nullptr;
    }
    Ptr<UbTransaction> transaction = m_controller->GetUbTransaction();
    if (transaction == nullptr || !transaction->ReplaceTpBacking(tpn, nullptr, tp))
    {
        const bool rolledBack = sw->ReplaceDormantTpBacking(tp, record.allocatorPlaceholder);
        NS_ABORT_MSG_IF(!rolledBack, "Failed to roll back dormant TP allocator resolution");
        return nullptr;
    }

    record.channel = tp;
    record.dormantState.reset();
    record.allocatorPlaceholder = nullptr;
    record.state = EndpointState::LIVE;
    utils::UbUtils::Get()->SingleTpTraceConnect(tp->GetSrc(), tpn);
    ++m_backingRehydrations;
    utils::UbUtils::NotifyTpBackingMaterialized();
    return tp;
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
    if (record.state == EndpointState::DORMANT)
    {
        record.state = EndpointState::DRAINING;
        record.reason = reason;
        if (!callback.IsNull())
        {
            record.callbacks.push_back(callback);
        }
        FinalizeDestroy(tpn, false);
        return UbDestroyStatus::COMPLETED;
    }
    const bool beginDrain = record.state == EndpointState::LIVE;
    record.state = EndpointState::DRAINING;
    record.reason = reason;
    if (!callback.IsNull())
    {
        record.callbacks.push_back(callback);
    }
    if (beginDrain)
    {
        UbLifecycleCoordinator::GetOrCreate(m_controller->GetObject<Node>())
            ->SetResourceStateChangedCallback(
                record.channel->GetLifecycleOwner(),
                MakeCallback(&UbTransportChannelManager::NotifyLifecycleStateChanged, this));
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
    return record.channel != nullptr && AssessTpQuiescenceLocked(record.channel->GetTpn(),
                                                                 record,
                                                                 UbTeardownIntent::EXPLICIT_DESTROY)
                                            .IsAllowed();
}

void
UbTransportChannelManager::NotifyStateChanged(uint32_t tpn)
{
    const auto it = m_endpoints.find(tpn);
    if (it == m_endpoints.end())
    {
        return;
    }
    if (it->second.state == EndpointState::LIVE)
    {
        if (it->second.channel == nullptr || !it->second.channel->IsSafeForBackingReclaim())
        {
            return;
        }
        ReclaimBackingState(tpn);
    }
    else if (it->second.state == EndpointState::DRAINING && CanFinalize(it->second))
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
    if (it->second.channel == nullptr)
    {
        NS_ABORT_MSG_IF(it->second.dormantState == nullptr ||
                            it->second.allocatorPlaceholder == nullptr,
                        "Cannot finalize an invalid dormant TP endpoint");
        const UbLifecycleResourceIdentity identity = it->second.dormantState->lifecycleIdentity;
        std::vector<UbDestroyCallback> callbacks = std::move(it->second.callbacks);
        if (!force)
        {
            m_controller->GetObject<Node>()->GetObject<UbSwitch>()->RemoveTpFromAllocator(
                it->second.allocatorPlaceholder);
            m_controller->GetUbTransaction()->TpDeinit(tpn, identity);
            UbLifecycleCoordinator::GetOrCreate(m_controller->GetObject<Node>())
                ->RetirePacketOwner(identity);
        }
        m_endpoints.erase(it);
        if (m_transportsCount > 0)
        {
            --m_transportsCount;
        }
        for (const UbDestroyCallback& dormantCallback : callbacks)
        {
            dormantCallback(identity, UbDestroyStatus::COMPLETED);
        }
        return;
    }
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
    utils::UbUtils::NotifyTpBackingReleased();
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
        if (record.channel != nullptr)
        {
            record.channel->MarkOwnerControllerDisposing();
        }
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

UbTransportEndpointStorageSnapshot
UbTransportChannelManager::GetStorageSnapshot() const
{
    std::lock_guard<std::mutex> lock(TpConnectionManager::g_tpConnectionControlLock);
    UbTransportEndpointStorageSnapshot snapshot;
    for (const auto& [tpn, record] : m_endpoints)
    {
        (void)tpn;
        snapshot.liveBackingObjects += record.channel != nullptr;
        snapshot.dormantRecords += record.state == EndpointState::DORMANT;
    }
    snapshot.backingRehydrations = m_backingRehydrations;
    return snapshot;
}

} // namespace ns3
