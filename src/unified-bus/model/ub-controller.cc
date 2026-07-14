// SPDX-License-Identifier: GPL-2.0-only
#include "ub-controller.h"
#include <ns3/log.h>
#include <algorithm>
#include <limits>
#include <set>
#include "protocol/ub-transport.h"
#include "protocol/ub-datalink.h"
#include "protocol/ub-transaction.h"
#include "ns3/ub-network-address.h"
#include "ns3/ub-utils.h"

using namespace utils;
namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbController");
NS_OBJECT_ENSURE_REGISTERED(UbController);

TypeId UbController::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UbController")
        .SetParent<Object>()
        .SetGroupName("UnifiedBus")
        .AddConstructor<UbController>();
    return tid;
}

UbController::UbController()
{
    NS_LOG_FUNCTION(this);
    m_tpnConn = CreateObject<TpConnectionManager>();
}

void UbController::CreateUbFunction()
{
    m_function = CreateObject<UbFunction>();
    m_function->Init(GetObject<Node>()->GetId());
}

void UbController::CreateUbTransaction()
{
    m_transaction = CreateObject<UbTransaction>(GetObject<Node>());
    NS_ABORT_MSG_IF(m_function == nullptr,
                    "UbFunction must be created before UbTransaction binding");
    m_transaction->SetFunction(PeekPointer(m_function));
    m_function->BindTransaction(m_transaction);
}

UbController::~UbController()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_rtpTpGroups.empty(),
                  "UbController must force-clean canonical TPGs in DoDispose");
    m_numToTp.clear();
    m_rtpTpGroups.clear();
    m_rtpTpGroupByTpn.clear();
    m_ports.clear();
    m_destinationToPortsMap.clear();
    m_portPairsToIter.clear();
    m_dstPriToTp.clear();
    m_dstPriToTpRrIndex.clear();
}

void
UbController::DoDispose()
{
    m_disposing = true;
    for (const auto& [tpn, tp] : m_numToTp)
    {
        (void)tpn;
        NS_ABORT_MSG_IF(tp == nullptr,
                        "Controller canonical TP collection contains a null owner");
        tp->MarkOwnerControllerDisposing();
    }
    std::vector<UbLifecycleResourceIdentity> groups;
    groups.reserve(m_rtpTpGroups.size());
    for (const auto& [localId, group] : m_rtpTpGroups)
    {
        (void)localId;
        NS_ABORT_MSG_IF(group == nullptr,
                        "Controller canonical TPG collection contains a null owner");
        groups.push_back(group->GetLifecycleIdentity());
    }
    for (const UbLifecycleResourceIdentity& identity : groups)
    {
        const UbDestroyStatus status =
            DestroyRtpTpGroup(identity, UbDestroyReason::SIMULATION_STOP);
        NS_ABORT_MSG_IF(status != UbDestroyStatus::COMPLETED,
                        "Controller dispose failed to force-clean canonical TPG");
    }
    NS_ABORT_MSG_IF(!m_rtpTpGroups.empty() || !m_rtpTpGroupByTpn.empty(),
                    "Controller dispose left canonical TPG ownership behind: groups="
                        << m_rtpTpGroups.size()
                        << ", member-index=" << m_rtpTpGroupByTpn.size());

    m_ctpTransportService = nullptr;
    if (m_function != nullptr)
    {
        m_function->Dispose();
    }
    if (m_transaction != nullptr)
    {
        m_transaction->Dispose();
    }
    m_transaction = nullptr;
    m_function = nullptr;
    m_numToTp.clear();
    m_ports.clear();
    m_datalink = nullptr;
    m_destinationToPortsMap.clear();
    m_destinationToPortPairMap.clear();
    m_portPairsToIter.clear();
    m_dstPriToTp.clear();
    m_dstPriToTpRrIndex.clear();
    m_tpnConn = nullptr;
    Object::DoDispose();
}

Ptr<UbTransportChannel> UbController::GetTpByTpn(uint32_t tpn)
{
    auto it = m_numToTp.find(tpn);
    if (it == m_numToTp.end())
    {
        return nullptr;
    }
    return it->second;
}

// Transport channel management
bool UbController::CreateTp(uint32_t src, uint32_t dest, uint8_t sport,
                            uint8_t dport, UbPriority priority, uint32_t srcTpn,
                            uint32_t dstTpn, Ptr<UbCongestionControl> congestionCtrl)
{
    NS_LOG_FUNCTION(this << src << dest << sport << dport  << priority << srcTpn);
    // 检查是否已存在
    if (m_numToTp.find(srcTpn) != m_numToTp.end()) {
        NS_LOG_DEBUG("Transport channel already exists");
        return false;
    }

    // 创建新的Transport Channel
    // 需要先获取当前节点
    Ptr<Node> currentNode = GetObject<Node>();

    if (!currentNode) {
        // 如果没有关联节点，可能需要从其他地方获取或设置默认值
        NS_LOG_WARN("No associated node found for UbController");
        currentNode = CreateObject<Node> ();
    }

    Ipv4Address sip = NodeIdToIp(src, sport);
    Ipv4Address dip = NodeIdToIp(dest, dport);

    Ptr<UbTransportChannel> tp = CreateObject<UbTransportChannel>();
    tp->SetUbTransport(currentNode->GetId(), src, dest, srcTpn, dstTpn, 0, static_cast<uint8_t>(priority),
                       static_cast<uint16_t>(sport), static_cast<uint16_t>(dport), sip, dip, congestionCtrl);
    m_transaction->TpInit(tp);
    m_numToTp[srcTpn] = tp;
    m_transportsCount++;
    currentNode->GetObject<UbSwitch>()->RegisterTpWithAllocator(tp, sport, priority);  // register TP with allocator

    utils::UbUtils::Get()->SingleTpTraceConnect(src, srcTpn);
    NS_LOG_DEBUG("Created transport channel success");
    return true;
}

Ptr<UbTransportChannel>
UbController::CreateReservedTpEndpoint(uint32_t tpn)
{
    std::lock_guard<std::mutex> controlLock(TpConnectionManager::g_tpConnectionControlLock);
    return CreateReservedTpEndpointLocked(tpn);
}

Ptr<UbTransportChannel>
UbController::CreateReservedTpEndpointLocked(uint32_t tpn)
{
    Ptr<UbTransportChannel> existingTp = GetTpByTpn(tpn);
    if (existingTp != nullptr)
    {
        return existingTp;
    }

    Ptr<Node> currentNode = GetObject<Node>();
    NS_ASSERT_MSG(currentNode != nullptr, "Reserved TP endpoint requires an attached node");
    NS_ASSERT_MSG(m_transaction != nullptr, "Reserved TP endpoint requires UbTransaction");
    NS_ASSERT_MSG(m_tpnConn != nullptr, "Reserved TP endpoint requires TpConnectionManager");

    Connection conn;
    if (!m_tpnConn->TryGetLocalConnection(currentNode->GetId(), tpn, conn))
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
    return GetTpByTpn(tpn);
}

Ptr<UbTransportChannel>
UbController::ResolveInboundTp(uint32_t srcTpn,
                               uint32_t dstTpn,
                               Ipv4Address sourceIp,
                               Ipv4Address destinationIp,
                               UbPriority priority)
{
    std::lock_guard<std::mutex> controlLock(TpConnectionManager::g_tpConnectionControlLock);
    Ptr<UbTransportChannel> targetTp = GetTpByTpn(dstTpn);

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
        Ptr<Node> currentNode = GetObject<Node>();
        NS_ASSERT_MSG(currentNode != nullptr, "Inbound TP resolution requires an attached node");

        Connection conn;
        if (!m_tpnConn->TryGetLocalConnection(currentNode->GetId(), dstTpn, conn))
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

Ptr<UbTransportChannel> UbController::GetTp(uint32_t tpn)
{
    NS_LOG_FUNCTION(this);

    auto it = m_numToTp.find(tpn);
    if (it != m_numToTp.end()) {
        return it->second;
    }

    return nullptr;
}

void UbController::DestroyTp(uint32_t tpn)
{
    // 算法中删除tp记录
    GetObject<Node>()->GetObject<UbSwitch>()->RemoveTpFromAllocator(m_numToTp[tpn]);
    // 事务层删除tp记录
    m_transaction->TpDeinit(tpn);
    // 删除本地记录
    m_numToTp.erase(tpn);
    m_transportsCount--;
}

Ptr<UbTransportGroup>
UbController::CreateRtpTpGroup(const std::vector<Ptr<UbTransportChannel>>& members,
                               uint32_t initialSelectionCursor)
{
    NS_ABORT_MSG_IF(members.empty(), "Cannot create an empty RTP TPG");
    NS_ABORT_MSG_IF(m_nextRtpTpGroupId == std::numeric_limits<uint64_t>::max(),
                    "RTP TPG identity exhausted");

    std::vector<UbTransportGroupMember> records;
    records.reserve(members.size());
    std::set<uint32_t> seenTpns;
    uint64_t existingGroupId = 0;
    for (const Ptr<UbTransportChannel>& member : members)
    {
        NS_ABORT_MSG_IF(member == nullptr, "Cannot add a null TP to an RTP TPG");
        NS_ABORT_MSG_IF(!seenTpns.insert(member->GetTpn()).second,
                        "Cannot add the same TP to an RTP TPG twice");
        records.push_back({member->GetTpn(), member->GetLifecycleOwner()});
        const auto existing = m_rtpTpGroupByTpn.find(member->GetTpn());
        if (existing != m_rtpTpGroupByTpn.end())
        {
            NS_ABORT_MSG_IF(existingGroupId != 0 && existingGroupId != existing->second,
                            "RTP TPG members already belong to different groups");
            existingGroupId = existing->second;
        }
    }

    if (existingGroupId != 0)
    {
        const auto existing = m_rtpTpGroups.find(existingGroupId);
        NS_ABORT_MSG_IF(existing == m_rtpTpGroups.end() || existing->second == nullptr,
                        "RTP TPG member index references a missing group");
        Ptr<UbTransportGroup> group = existing->second;
        if (!group->IsAcceptingNewOperations())
        {
            return nullptr;
        }
        NS_ABORT_MSG_IF(group->GetMemberCount() != records.size(),
                        "A TP can belong to only one RTP TPG");
        for (uint32_t index = 0; index < records.size(); ++index)
        {
            NS_ABORT_MSG_IF(group->GetMember(index).tpn != records[index].tpn ||
                                group->GetMember(index).tpIdentity != records[index].tpIdentity,
                            "Existing RTP TPG membership does not match the requested group");
        }
        return group;
    }

    const uint64_t localId = ++m_nextRtpTpGroupId;
    Ptr<UbLifecycleCoordinator> coordinator =
        UbLifecycleCoordinator::GetOrCreate(GetObject<Node>());
    const UbLifecycleResourceIdentity identity =
        coordinator->OpenResource(UbLifecycleResourceKind::RTP_TPG, localId);
    for (UbTransportGroupMember& record : records)
    {
        record.membershipLease = coordinator->AcquireLease(record.tpIdentity,
                                                           UbLifecycleLeaseKind::BINDING,
                                                           identity);
    }
    Ptr<UbTransportGroup> group = CreateObject<UbTransportGroup>();
    group->Init(identity, records, initialSelectionCursor);
    const bool inserted = m_rtpTpGroups.emplace(localId, group).second;
    NS_ABORT_MSG_IF(!inserted, "Duplicate RTP TPG collection identity");
    for (const UbTransportGroupMember& member : records)
    {
        const bool indexed = m_rtpTpGroupByTpn.emplace(member.tpn, localId).second;
        NS_ABORT_MSG_IF(!indexed, "A TP can belong to only one RTP TPG");
    }
    return group;
}

Ptr<UbTransportGroup>
UbController::ResolveRtpTpGroup(const UbLifecycleResourceIdentity& identity) const
{
    Ptr<Node> node = GetObject<Node>();
    Ptr<UbLifecycleCoordinator> coordinator =
        node == nullptr ? nullptr : node->GetObject<UbLifecycleCoordinator>();
    if (identity.kind != UbLifecycleResourceKind::RTP_TPG || node == nullptr ||
        identity.ownerNodeId != node->GetId() || coordinator == nullptr ||
        !coordinator->IsResourceActive(identity))
    {
        return nullptr;
    }
    const auto group = m_rtpTpGroups.find(identity.localId);
    if (group == m_rtpTpGroups.end() || group->second == nullptr ||
        group->second->GetLifecycleIdentity() != identity)
    {
        return nullptr;
    }
    return group->second;
}

Ptr<UbTransportGroup>
UbController::ResolveRtpTpGroupForMember(
    const UbLifecycleResourceIdentity& tpIdentity) const
{
    if (tpIdentity.kind != UbLifecycleResourceKind::RTP_ENDPOINT)
    {
        return nullptr;
    }
    const auto membership = m_rtpTpGroupByTpn.find(static_cast<uint32_t>(tpIdentity.localId));
    if (membership == m_rtpTpGroupByTpn.end())
    {
        return nullptr;
    }
    const auto group = m_rtpTpGroups.find(membership->second);
    if (group == m_rtpTpGroups.end() || group->second == nullptr)
    {
        return nullptr;
    }
    for (uint32_t index = 0; index < group->second->GetMemberCount(); ++index)
    {
        if (group->second->GetMember(index).tpIdentity == tpIdentity)
        {
            return group->second;
        }
    }
    return nullptr;
}

void
UbController::FinalizeRtpTpGroupMemberRemoval(Ptr<UbTransportGroup> group, uint32_t index)
{
    NS_ABORT_MSG_IF(group == nullptr || index >= group->GetMemberCount(),
                    "TPG member finalize requires an exact member");
    group->MarkMemberRetiring(index);
    const UbTransportGroupMember removed = group->RemoveMember(index);
    Ptr<UbLifecycleCoordinator> coordinator =
        UbLifecycleCoordinator::GetOrCreate(GetObject<Node>());
    coordinator->ReleaseLease(removed.membershipLease);
    const auto membership = m_rtpTpGroupByTpn.find(removed.tpn);
    NS_ABORT_MSG_IF(membership == m_rtpTpGroupByTpn.end() ||
                        membership->second != group->GetLifecycleIdentity().localId,
                    "TPG member reverse index is stale");
    m_rtpTpGroupByTpn.erase(membership);
}

UbTpgMemberMutationStatus
UbController::AddRtpTpGroupMember(
    const UbLifecycleResourceIdentity& groupIdentity,
    const UbLifecycleResourceIdentity& tpIdentity)
{
    Ptr<UbTransportGroup> group = ResolveRtpTpGroup(groupIdentity);
    if (group == nullptr)
    {
        return UbTpgMemberMutationStatus::NOT_FOUND;
    }
    if (group->IsDestroyPending() ||
        (!group->IsAcceptingNewOperations() && !group->HasPendingLastMemberRemoval()))
    {
        return UbTpgMemberMutationStatus::REJECTED;
    }

    Ptr<Node> node = GetObject<Node>();
    Ptr<UbLifecycleCoordinator> coordinator =
        UbLifecycleCoordinator::GetOrCreate(node);
    if (tpIdentity.kind != UbLifecycleResourceKind::RTP_ENDPOINT ||
        tpIdentity.ownerNodeId != node->GetId() ||
        tpIdentity.localId > std::numeric_limits<uint32_t>::max() ||
        !coordinator->IsResourceActive(tpIdentity))
    {
        return UbTpgMemberMutationStatus::REJECTED;
    }
    const uint32_t tpn = static_cast<uint32_t>(tpIdentity.localId);
    const auto transport = m_numToTp.find(tpn);
    if (transport == m_numToTp.end() || transport->second == nullptr ||
        transport->second->GetLifecycleOwner() != tpIdentity ||
        group->FindMemberIndex(tpIdentity) >= 0 ||
        m_rtpTpGroupByTpn.find(tpn) != m_rtpTpGroupByTpn.end())
    {
        return UbTpgMemberMutationStatus::REJECTED;
    }

    UbTransportGroupMember member{tpn, tpIdentity};
    member.membershipLease = coordinator->AcquireLease(tpIdentity,
                                                       UbLifecycleLeaseKind::BINDING,
                                                       groupIdentity);
    bool memberInserted = false;
    try
    {
        group->AddMember(member);
        memberInserted = true;
        const bool indexed = m_rtpTpGroupByTpn.emplace(tpn, groupIdentity.localId).second;
        if (!indexed)
        {
            const int32_t index = group->FindMemberIndex(tpIdentity);
            NS_ABORT_MSG_IF(index < 0, "TPG AddMember rollback lost the inserted member");
            group->RemoveMember(static_cast<uint32_t>(index));
            coordinator->ReleaseLease(member.membershipLease);
            return UbTpgMemberMutationStatus::REJECTED;
        }
    }
    catch (...)
    {
        if (memberInserted)
        {
            const int32_t index = group->FindMemberIndex(tpIdentity);
            if (index >= 0)
            {
                group->RemoveMember(static_cast<uint32_t>(index));
            }
        }
        coordinator->ReleaseLease(member.membershipLease);
        throw;
    }

    if (group->HasPendingLastMemberRemoval())
    {
        const int32_t oldIndex =
            group->FindMemberIndex(group->GetPendingLastMemberRemoval());
        NS_ABORT_MSG_IF(oldIndex < 0,
                        "Pending last TPG member disappeared before replacement");
        FinalizeRtpTpGroupMemberRemoval(group, static_cast<uint32_t>(oldIndex));
        group->ClearPendingLastMemberRemoval();
        group->ResumeNewOperations();
    }
    if (m_transaction != nullptr)
    {
        m_transaction->NotifyRtpTpGroupMemberAvailable(groupIdentity);
    }
    return UbTpgMemberMutationStatus::ADDED;
}

UbTpgMemberMutationStatus
UbController::RemoveRtpTpGroupMember(
    const UbLifecycleResourceIdentity& groupIdentity,
    const UbLifecycleResourceIdentity& tpIdentity,
    UbDestroyReason reason)
{
    Ptr<UbTransportGroup> group = ResolveRtpTpGroup(groupIdentity);
    if (group == nullptr)
    {
        return UbTpgMemberMutationStatus::NOT_FOUND;
    }
    const int32_t memberIndex = group->FindMemberIndex(tpIdentity);
    if (memberIndex < 0)
    {
        return UbTpgMemberMutationStatus::NOT_FOUND;
    }
    if (group->HasPendingLastMemberRemoval())
    {
        return group->GetPendingLastMemberRemoval() == tpIdentity
                   ? UbTpgMemberMutationStatus::PENDING
                   : UbTpgMemberMutationStatus::REJECTED;
    }

    if (group->GetMemberCount() > 1)
    {
        FinalizeRtpTpGroupMemberRemoval(group, static_cast<uint32_t>(memberIndex));
        return UbTpgMemberMutationStatus::REMOVED;
    }

    Ptr<UbLifecycleCoordinator> coordinator =
        UbLifecycleCoordinator::GetOrCreate(GetObject<Node>());
    const uint64_t useLeases = coordinator->GetSnapshot(groupIdentity).activeTransportUseLeases;
    if (useLeases != 0)
    {
        if (reason == UbDestroyReason::ERROR ||
            reason == UbDestroyReason::SIMULATION_STOP)
        {
            group->FreezeNewOperations();
            if (m_transaction != nullptr)
            {
                const bool forceDrain = reason == UbDestroyReason::SIMULATION_STOP;
                m_transaction->TerminateRtpTpGroupOperations(
                    groupIdentity,
                    forceDrain ? UbWorkOutcome::FLUSHED : UbWorkOutcome::FAILED,
                    forceDrain);
            }
            const auto canonical = m_rtpTpGroups.find(groupIdentity.localId);
            if (canonical == m_rtpTpGroups.end() || canonical->second != group ||
                ResolveRtpTpGroup(groupIdentity) != group)
            {
                return UbTpgMemberMutationStatus::NOT_FOUND;
            }
            NS_ABORT_MSG_IF(
                coordinator->GetSnapshot(groupIdentity).activeTransportUseLeases != 0,
                "Typed last TPG member cleanup did not drain exact group use leases");
            const int32_t currentIndex = group->FindMemberIndex(tpIdentity);
            if (currentIndex < 0)
            {
                return UbTpgMemberMutationStatus::NOT_FOUND;
            }
            FinalizeRtpTpGroupMemberRemoval(group, static_cast<uint32_t>(currentIndex));
            group->ResumeNewOperations();
            return UbTpgMemberMutationStatus::REMOVED;
        }
        group->FreezeNewOperations();
        group->SetPendingLastMemberRemoval(tpIdentity);
        return UbTpgMemberMutationStatus::PENDING;
    }

    FinalizeRtpTpGroupMemberRemoval(group, static_cast<uint32_t>(memberIndex));
    return UbTpgMemberMutationStatus::REMOVED;
}

void
UbController::FinalizeRtpTpGroupDestroy(Ptr<UbTransportGroup> group)
{
    NS_ABORT_MSG_IF(group == nullptr, "TPG destroy finalize requires an exact group");
    const UbLifecycleResourceIdentity identity = group->GetLifecycleIdentity();
    Ptr<UbLifecycleCoordinator> coordinator =
        UbLifecycleCoordinator::GetOrCreate(GetObject<Node>());
    NS_ABORT_MSG_IF(coordinator->GetSnapshot(identity).activeTransportUseLeases != 0,
                    "TPG destroy finalize requires drained use leases");
    if (m_transaction != nullptr)
    {
        m_transaction->RetireRtpAdmissionDomain(identity);
        m_transaction->EraseRtpTpGroupBindings(identity);
    }
    while (group->GetMemberCount() != 0)
    {
        FinalizeRtpTpGroupMemberRemoval(group, group->GetMemberCount() - 1);
    }
    if (!coordinator->GetSnapshot(identity).retired)
    {
        coordinator->RecordSuccessfulTerminal(identity);
        coordinator->RetireResource(identity);
    }
    const auto record = m_rtpTpGroups.find(identity.localId);
    NS_ABORT_MSG_IF(record == m_rtpTpGroups.end() || record->second != group,
                    "TPG destroy finalize requires the canonical collection record");
    m_rtpTpGroups.erase(record);
}

UbDestroyStatus
UbController::DestroyRtpTpGroup(const UbLifecycleResourceIdentity& groupIdentity,
                                UbDestroyReason reason)
{
    Ptr<UbTransportGroup> group = ResolveRtpTpGroup(groupIdentity);
    if (group == nullptr && m_disposing && reason == UbDestroyReason::SIMULATION_STOP)
    {
        const auto canonical = m_rtpTpGroups.find(groupIdentity.localId);
        if (canonical != m_rtpTpGroups.end() && canonical->second != nullptr &&
            canonical->second->GetLifecycleIdentity() == groupIdentity)
        {
            group = canonical->second;
        }
    }
    if (group == nullptr)
    {
        return UbDestroyStatus::NOT_FOUND;
    }
    const UbTpgCleanupMode requestedMode =
        reason == UbDestroyReason::SIMULATION_STOP
            ? UbTpgCleanupMode::FORCE
            : (reason == UbDestroyReason::ERROR ? UbTpgCleanupMode::ABORT
                                                : UbTpgCleanupMode::GRACEFUL);
    group->RequestDestroy(requestedMode);
    group->FreezeNewOperations();
    if (!group->AreBindingLeasesReleased())
    {
        if (m_transaction != nullptr)
        {
            m_transaction->ReleaseRtpTpGroupBindingLeases(groupIdentity);
        }
        group->MarkBindingLeasesReleased();
    }
    if (group->GetCleanupMode() != UbTpgCleanupMode::GRACEFUL && m_transaction != nullptr)
    {
        const bool forceDrain = group->GetCleanupMode() == UbTpgCleanupMode::FORCE;
        m_transaction->TerminateRtpTpGroupOperations(groupIdentity,
                                                     forceDrain ? UbWorkOutcome::FLUSHED
                                                                : UbWorkOutcome::FAILED,
                                                     forceDrain);
        const auto canonical = m_rtpTpGroups.find(groupIdentity.localId);
        if (canonical == m_rtpTpGroups.end() || canonical->second != group)
        {
            return UbDestroyStatus::COMPLETED;
        }
    }
    Ptr<UbLifecycleCoordinator> coordinator =
        UbLifecycleCoordinator::GetOrCreate(GetObject<Node>());
    if (coordinator->GetSnapshot(groupIdentity).activeTransportUseLeases != 0)
    {
        return UbDestroyStatus::PENDING;
    }

    FinalizeRtpTpGroupDestroy(group);
    return UbDestroyStatus::COMPLETED;
}

void
UbController::NotifyRtpTpGroupUseLeaseReleased(
    const UbLifecycleResourceIdentity& groupIdentity)
{
    Ptr<UbTransportGroup> group = ResolveRtpTpGroup(groupIdentity);
    if (group == nullptr ||
        (!group->IsDestroyPending() && !group->HasPendingLastMemberRemoval()))
    {
        return;
    }
    Ptr<UbLifecycleCoordinator> coordinator =
        UbLifecycleCoordinator::GetOrCreate(GetObject<Node>());
    if (coordinator->GetSnapshot(groupIdentity).activeTransportUseLeases != 0)
    {
        return;
    }
    if (group->IsDestroyPending())
    {
        FinalizeRtpTpGroupDestroy(group);
        return;
    }
    const int32_t index = group->FindMemberIndex(group->GetPendingLastMemberRemoval());
    NS_ABORT_MSG_IF(index < 0, "Pending last TPG member disappeared before use-lease drain");
    FinalizeRtpTpGroupMemberRemoval(group, static_cast<uint32_t>(index));
    group->ClearPendingLastMemberRemoval();
    group->ResumeNewOperations();
}

// Port management
void UbController::AddPortDestination(Ptr<UbPort> port, uint32_t destination)
{
    NS_LOG_FUNCTION(this << port << destination);
    if (!port) {
        NS_LOG_WARN("Trying to add null port");
        return;
    }

    m_destinationToPortsMap[destination].push_back(port);
    NS_LOG_DEBUG("Added port to destination " << destination);
}

void UbController::RemovePortDestination(Ptr<UbPort> port, uint32_t destination)
{
    NS_LOG_FUNCTION(this << port << destination);
    if (!port) {
        NS_LOG_WARN("Trying to remove null port");
        return;
    }

    auto it = m_destinationToPortsMap.find(destination);
    if (it != m_destinationToPortsMap.end()) {
        std::vector<Ptr<UbPort>>& ports = it->second;
        ports.erase(std::remove(ports.begin(), ports.end(), port), ports.end());

        if (ports.empty()) {
            m_destinationToPortsMap.erase(it);
        }
        NS_LOG_DEBUG("Removed port from destination " << destination);
    }
}

std::vector<Ptr<UbPort>> UbController::GetAvailablePorts(uint32_t destination) const
{
    NS_LOG_FUNCTION(this << destination);

    auto it = m_destinationToPortsMap.find(destination);
    if (it != m_destinationToPortsMap.end()) {
        return it->second;
    }

    return {};
}

Ptr<UbFunction> UbController::GetUbFunction()
{
    return m_function;
}

Ptr<UbTransaction> UbController::GetUbTransaction()
{
    return m_transaction;
}

Ptr<UbCtpTransportService> UbController::GetCtpTransportService()
{
    if (m_ctpTransportService == nullptr)
    {
        m_ctpTransportService = CreateObject<UbCtpTransportService>();
        m_ctpTransportService->SetNode(GetObject<Node>());
        if (!m_ctpFirstPacketSendsCallback.IsNull())
        {
            m_ctpTransportService->TraceConnectWithoutContext("FirstPacketSendsNotify",
                                                              m_ctpFirstPacketSendsCallback);
        }
        if (!m_ctpLastPacketAcksCallback.IsNull())
        {
            m_ctpTransportService->TraceConnectWithoutContext("LastPacketACKsNotify",
                                                              m_ctpLastPacketAcksCallback);
        }
    }
    return m_ctpTransportService;
}

Ptr<UbCtpTransportService> UbController::PeekCtpTransportService() const
{
    return m_ctpTransportService;
}

void
UbController::SetCtpPacketTimingTraceCallbacks(CtpPacketTimingCallback firstPacketSendsCallback,
                                               CtpPacketTimingCallback lastPacketAcksCallback)
{
    m_ctpFirstPacketSendsCallback = firstPacketSendsCallback;
    m_ctpLastPacketAcksCallback = lastPacketAcksCallback;
    if (m_ctpTransportService != nullptr && !m_ctpFirstPacketSendsCallback.IsNull())
    {
        m_ctpTransportService->TraceConnectWithoutContext("FirstPacketSendsNotify",
                                                          m_ctpFirstPacketSendsCallback);
    }
    if (m_ctpTransportService != nullptr && !m_ctpLastPacketAcksCallback.IsNull())
    {
        m_ctpTransportService->TraceConnectWithoutContext("LastPacketACKsNotify",
                                                          m_ctpLastPacketAcksCallback);
    }
}

std::map<uint32_t, Ptr<UbTransportChannel>> UbController::GetTpnMap() const
{
    return m_numToTp;
}

bool UbController::AddTpMapping(uint32_t key, Ptr<UbTransportChannel> tp)
{
    bool ret = false;

    auto it = m_tpsMapInIngressSource.find(key);
    if (it != m_tpsMapInIngressSource.end()) { // 当前tp已经映射过
        ret = false;
    } else {
        m_tpsMapInIngressSource.insert(std::make_pair(key, tp));
        ret = true;
    }

    return ret;
}

Ptr<UbTransportChannel> UbController::GetTpByMap(uint32_t key)
{
    auto it = m_tpsMapInIngressSource.find(key);
    if (it != m_tpsMapInIngressSource.end()) {
        return it->second;
    } else {
        NS_LOG_WARN("Map error!");
    }

    return nullptr;
}

bool UbController::IsTPExists(uint32_t tpn)
{
    return (m_numToTp.find(tpn) != m_numToTp.end());
}
} // namespace ns3
