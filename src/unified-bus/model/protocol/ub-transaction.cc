// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/log.h"
#include "ns3/simulator.h"

#include "ns3/ub-datatype.h"
#include "ns3/ub-controller.h"
#include "ub-ctp.h"
#include "ns3/ub-modulo-sequence.h"
#include "ub-transaction.h"
#include "ns3/ub-utils.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbTransaction");

NS_OBJECT_ENSURE_REGISTERED(UbTransaction);

namespace
{

using UbTaSsnSequence = UbModuloSequence<16, uint32_t>;

} // namespace

bool
UbCtpEntityKey::operator<(const UbCtpEntityKey& other) const
{
    return std::tie(srcEntityId, dstNodeId, dstEntityId, vl) <
           std::tie(other.srcEntityId, other.dstNodeId, other.dstEntityId, other.vl);
}

bool
UbCtpEntityKey::operator==(const UbCtpEntityKey& other) const
{
    return srcEntityId == other.srcEntityId && dstNodeId == other.dstNodeId &&
           dstEntityId == other.dstEntityId && vl == other.vl;
}

UbCtpTransactionContext::UbCtpTransactionContext(UbCtpEntityKey key,
                                                 uint32_t ackWindowCapacity,
                                                 uint32_t initialSequence)
    : m_key(key),
      m_sendNext(initialSequence),
      m_completeUna(initialSequence)
{
    NS_ABORT_MSG_IF(ackWindowCapacity == 0,
                    "CTP transaction context requires a non-zero ack window");
    m_ackWindow.Resize(ackWindowCapacity);
    m_ackWindow.Reset(initialSequence);
}

bool
UbCtpTransactionContext::CanAdmit(uint32_t sequence) const
{
    if (sequence != m_sendNext)
    {
        return false;
    }
    return m_sendNext - m_completeUna < m_ackWindow.GetWindowSize();
}

bool
UbCtpTransactionContext::TryAdmit(uint32_t sequence)
{
    if (!CanAdmit(sequence))
    {
        return false;
    }
    ++m_sendNext;
    return true;
}

void
UbCtpTransactionContext::MarkTaAck(uint32_t sequence)
{
    if (sequence < m_completeUna || sequence >= m_sendNext)
    {
        return;
    }
    const bool marked = m_ackWindow.Mark(sequence);
    NS_ASSERT_MSG(marked, "CTP TAACK sequence must fit in window");
    m_completeUna += m_ackWindow.AdvanceContiguous();
}

std::optional<uint32_t>
UbCtpTransactionContext::MarkTaAckWire(uint16_t wireSequence)
{
    auto logicalSequence =
        UbTaSsnSequence::UnwrapInWindow(wireSequence, m_completeUna, m_sendNext);
    if (!logicalSequence.has_value())
    {
        return std::nullopt;
    }

    MarkTaAck(*logicalSequence);
    return logicalSequence;
}

uint32_t
UbCtpTransactionContext::GetCompleteUna() const
{
    return m_completeUna;
}

uint32_t
UbCtpTransactionContext::GetSendNext() const
{
    return m_sendNext;
}

uint32_t
UbCtpTransactionContext::GetOutstandingCount() const
{
    return m_sendNext - m_completeUna;
}

void
UbCtpTransactionContext::SetWindowForTest(uint32_t completeUna, uint32_t sendNext)
{
    NS_ABORT_MSG_IF(sendNext < completeUna, "CTP test window sendNext must not precede completeUna");
    m_completeUna = completeUna;
    m_sendNext = sendNext;
    m_ackWindow.Reset(completeUna);
}

UbRtpTransportReference::UbRtpTransportReference(
    UbRtpTransportReferenceKind kind,
    const UbLifecycleResourceIdentity& identity,
    uint32_t directTpn)
    : m_kind(kind),
      m_identity(identity),
      m_directTpn(directTpn)
{
}

UbRtpTransportReference
UbRtpTransportReference::ForDirectTp(uint32_t tpn,
                                     const UbLifecycleResourceIdentity& identity)
{
    NS_ABORT_MSG_IF(identity.kind != UbLifecycleResourceKind::RTP_ENDPOINT ||
                        identity.localId != tpn || identity.generation == 0,
                    "Direct RTP reference requires the exact live TP generation");
    return {UbRtpTransportReferenceKind::DIRECT_TP, identity, tpn};
}

UbRtpTransportReference
UbRtpTransportReference::ForTpGroup(const UbLifecycleResourceIdentity& identity)
{
    NS_ABORT_MSG_IF(identity.kind != UbLifecycleResourceKind::RTP_TPG || identity.localId == 0 ||
                        identity.generation == 0,
                    "TPG reference requires an RTP_TPG generation");
    return {UbRtpTransportReferenceKind::TPG, identity, UINT32_MAX};
}

UbRtpTransportReferenceKind
UbRtpTransportReference::GetKind() const
{
    return m_kind;
}

const UbLifecycleResourceIdentity&
UbRtpTransportReference::GetIdentity() const
{
    return m_identity;
}

bool
UbRtpTransportReference::IsDirectTp() const
{
    return m_kind == UbRtpTransportReferenceKind::DIRECT_TP;
}

uint32_t
UbRtpTransportReference::GetDirectTpn() const
{
    NS_ABORT_MSG_IF(!IsDirectTp(), "TPG reference has no direct TPN");
    return m_directTpn;
}

TypeId UbTransaction::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbTransaction")
        .SetParent<Object>()
        .SetGroupName("UnifiedBus");
    return tid;
}

UbTransaction::UbTransaction()
{
    NS_LOG_DEBUG("UbTransaction created");
}

UbTransaction::~UbTransaction()
{
}

UbTransaction::UbTransaction(Ptr<Node> node)
{
    m_nodeId = node->GetId();
    m_coordinator = UbLifecycleCoordinator::GetOrCreate(node);
    m_random = CreateObject<UniformRandomVariable>();
    m_random->SetAttribute("Min", DoubleValue(0.0));
    m_random->SetAttribute("Max", DoubleValue(1.0));
}

void UbTransaction::TpInit(Ptr<UbTransportChannel> tp)
{
    uint32_t tpn = tp->GetTpn();
    m_transportByTpn[tpn] = tp;
    m_tpRRIndex[tpn] = 0;
}

void UbTransaction::TpDeinit(uint32_t tpn)
{
    const auto transport = m_transportByTpn.find(tpn);
    if (transport != m_transportByTpn.end() && transport->second != nullptr)
    {
        RetireRtpTxQueue(transport->second->GetLifecycleOwner());
    }
    m_transportByTpn.erase(tpn);
    m_jettyTpgMaterializations.clear();
    m_tpRRIndex.erase(tpn);
    m_remoteRtpWqeSegmentsByTpn.erase(tpn);
}

UbFunction* UbTransaction::GetFunction()
{
    NS_ABORT_MSG_IF(m_function == nullptr, "Transaction has no Function facade");
    return m_function;
}

void
UbTransaction::SetFunction(UbFunction* function)
{
    NS_ABORT_MSG_IF(function == nullptr, "Transaction requires a Function facade");
    m_function = function;
}

void
UbTransaction::SetOwnerController(UbController* controller)
{
    NS_ABORT_MSG_IF(controller == nullptr, "Transaction requires an owner Controller");
    m_ownerController = controller;
}

Ptr<UbJetty> UbTransaction::GetJetty(uint32_t jettyNum)
{
    return GetFunction()->GetJetty(jettyNum);

}

bool UbTransaction::JettyBindTp(uint32_t src, uint32_t dest, uint32_t jettyNum,
                                bool multiPath, std::vector<uint32_t> tpns)
{
    NS_LOG_DEBUG(this);
    Ptr<UbJetty> ubJetty = GetJetty(jettyNum);
    if (ubJetty == nullptr) {
        return false;
    }
    if (!GetFunction()->IsJettyAcceptingWqes(jettyNum)) {
        return false;
    }
    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbCtpTransportService> ctpService =
        controller == nullptr ? nullptr : controller->PeekCtpTransportService();
    if (ctpService != nullptr && ctpService->HasJettyPreparation(jettyNum)) {
        return false;
    }

    std::vector<Ptr<UbTransportChannel>> requestedTransports;
    requestedTransports.reserve(tpns.size());
    Ptr<UbTransportChannelManager> manager =
        controller == nullptr ? nullptr : controller->GetTransportChannelManager();
    for (uint32_t tpn : tpns) {
        UbLifecycleResourceIdentity identity;
        if (manager == nullptr || !manager->TryGetEndpointIdentity(tpn, identity))
        {
            return false;
        }
        Ptr<UbTransportChannel> transport = manager->ResolveLiveTp(tpn, identity);
        if (transport == nullptr)
        {
            return false;
        }
        requestedTransports.push_back(transport);
    }
    if (requestedTransports.empty()) {
        return false;
    }
    // 在事务层模式为ROL时只能开启单路径模式
    if (m_serviceMode[jettyNum] == TransactionServiceMode::ROL) {
        NS_LOG_WARN("ROL, set to single path forced.");
        multiPath = false;
    }
    std::vector<Ptr<UbTransportChannel>> boundTransports;
    if (multiPath && requestedTransports.size() > 1) {
        NS_LOG_DEBUG("Multiple tp");
        boundTransports = requestedTransports;
        if (controller == nullptr)
        {
            return false;
        }
        const uint32_t initialSelectionCursor =
            m_nextJettyTpStartIndex % boundTransports.size();
        Ptr<UbTransportGroup> group =
            controller->CreateRtpTpGroup(boundTransports, initialSelectionCursor);
        if (group == nullptr)
        {
            return false;
        }
        m_jettyRtpReferences.insert_or_assign(
            jettyNum,
            UbRtpTransportReference::ForTpGroup(group->GetLifecycleIdentity()));
    } else {
        NS_LOG_DEBUG("Single tp");
        // 根据随机结果选择TP
        const uint32_t pos = std::min(
            static_cast<uint32_t>(m_random->GetValue() * requestedTransports.size()),
            static_cast<uint32_t>(requestedTransports.size() - 1));
        Ptr<UbTransportChannel> transport = requestedTransports[pos];
        boundTransports.push_back(transport);
        const uint32_t tpn = transport->GetTpn();
        m_jettyRtpReferences.insert_or_assign(
            jettyNum,
            UbRtpTransportReference::ForDirectTp(tpn, transport->GetLifecycleOwner()));
    }

    if (!boundTransports.empty()) {
        m_jettyTpNextIndex[jettyNum] = m_nextJettyTpStartIndex % boundTransports.size();
        m_nextJettyTpStartIndex = (m_nextJettyTpStartIndex + 1) % boundTransports.size();
    } else {
        m_jettyTpNextIndex[jettyNum] = 0;
    }
    GetFunction()->RegisterJettyRtpBinding(
        jettyNum,
        m_jettyRtpReferences.at(jettyNum).GetIdentity());
    m_jettyTpgMaterializations.erase(jettyNum);
    return true;
}

bool
UbTransaction::HasJettyTpBinding(uint32_t jettyNum) const
{
    return m_jettyRtpReferences.find(jettyNum) != m_jettyRtpReferences.end();
}

void UbTransaction::DestroyJettyTpMap(uint32_t jettyNum)
{
    m_jettyRtpReferences.erase(jettyNum);
    m_jettyTpgMaterializations.erase(jettyNum);
    m_jettyTpNextIndex.erase(jettyNum);
}

void
UbTransaction::ReleaseCompletedJettyTransportBinding(uint32_t jettyNum, bool destroyTpGroup)
{
    const auto reference = m_jettyRtpReferences.find(jettyNum);
    if (reference == m_jettyRtpReferences.end())
    {
        return;
    }

    const UbRtpTransportReference completedReference = reference->second;
    DestroyJettyTpMap(jettyNum);
    if (!completedReference.IsDirectTp())
    {
        if (!destroyTpGroup)
        {
            return;
        }
        Ptr<UbController> controller = GetOwnerController();
        NS_ABORT_MSG_IF(controller == nullptr,
                        "Completed TPG binding requires its owner Controller");
        const UbDestroyStatus status = controller->DestroyRtpTpGroup(
            completedReference.GetIdentity(),
            UbDestroyReason::PROTOCOL_COMPLETE);
        NS_ABORT_MSG_IF(status == UbDestroyStatus::REJECTED,
                        "Completed TPG binding rejected explicit destroy");
        return;
    }

    const uint32_t tpn = completedReference.GetDirectTpn();
    Ptr<UbController> controller = GetOwnerController();
    if (controller != nullptr && controller->GetTransportChannelManager() != nullptr)
    {
        controller->GetTransportChannelManager()->NotifyStateChanged(tpn);
    }
}

void
UbTransaction::EraseRtpTpGroupBindings(
    const UbLifecycleResourceIdentity& groupIdentity)
{
    std::vector<uint32_t> jettyNums;
    for (const auto& [jettyNum, reference] : m_jettyRtpReferences)
    {
        if (!reference.IsDirectTp() && reference.GetIdentity() == groupIdentity)
        {
            jettyNums.push_back(jettyNum);
        }
    }
    for (uint32_t jettyNum : jettyNums)
    {
        DestroyJettyTpMap(jettyNum);
    }
}

const std::vector<Ptr<UbTransportChannel>>
UbTransaction::GetJettyLiveTps(uint32_t jettyNum)
{
    NS_LOG_DEBUG(this);
    const auto reference = m_jettyRtpReferences.find(jettyNum);
    if (reference == m_jettyRtpReferences.end())
    {
        return {};
    }
    if (reference->second.IsDirectTp())
    {
        Ptr<UbController> controller = GetOwnerController();
        Ptr<UbTransportChannel> transport =
            controller == nullptr ? nullptr
                                  : controller->GetTransportChannelManager()->ResolveLiveTp(
                                        reference->second.GetDirectTpn(),
                                        reference->second.GetIdentity());
        if (transport == nullptr)
        {
            return {};
        }
        return {transport};
    }

    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbTransportGroup> group =
        controller == nullptr ? nullptr
                              : controller->ResolveRtpTpGroup(reference->second.GetIdentity());
    if (group == nullptr)
    {
        return {};
    }
    return MaterializeRtpTpGroup(jettyNum, reference->second, group).liveTransports;
}

const UbTransaction::RtpTpgMaterialization&
UbTransaction::MaterializeRtpTpGroup(uint32_t jettyNum,
                                     const UbRtpTransportReference& reference,
                                     Ptr<UbTransportGroup> group)
{
    NS_ABORT_MSG_IF(group == nullptr || reference.IsDirectTp() ||
                        group->GetLifecycleIdentity() != reference.GetIdentity(),
                    "TPG materialization requires the exact resolved group generation");
    const uint64_t membershipVersion = group->GetMembershipVersion();
    const auto existing = m_jettyTpgMaterializations.find(jettyNum);
    if (existing != m_jettyTpgMaterializations.end() &&
        existing->second.groupIdentity == reference.GetIdentity() &&
        existing->second.membershipVersion == membershipVersion)
    {
        Ptr<UbController> controller = GetOwnerController();
        Ptr<UbTransportChannelManager> manager =
            controller == nullptr ? nullptr : controller->GetTransportChannelManager();
        existing->second.liveTransports.clear();
        for (uint32_t index = 0; index < group->GetMemberCount(); ++index)
        {
            const UbTransportGroupMember& member = group->GetMember(index);
            Ptr<UbTransportChannel> transport =
                manager == nullptr ? nullptr
                                   : manager->ResolveLiveTp(member.tpn, member.tpIdentity);
            existing->second.indexedTransports[index] = transport;
            if (transport != nullptr)
            {
                existing->second.liveTransports.push_back(transport);
            }
        }
        return existing->second;
    }

    RtpTpgMaterialization materialization;
    materialization.groupIdentity = reference.GetIdentity();
    materialization.membershipVersion = membershipVersion;
    materialization.indexedTransports.resize(group->GetMemberCount());
    materialization.liveTransports.reserve(group->GetMemberCount());
    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbTransportChannelManager> manager =
        controller == nullptr ? nullptr : controller->GetTransportChannelManager();
    for (uint32_t index = 0; index < group->GetMemberCount(); ++index)
    {
        const UbTransportGroupMember& member = group->GetMember(index);
        Ptr<UbTransportChannel> transport =
            manager == nullptr ? nullptr : manager->ResolveLiveTp(member.tpn, member.tpIdentity);
        if (transport == nullptr)
        {
            continue;
        }
        materialization.indexedTransports[index] = transport;
        materialization.liveTransports.push_back(transport);
    }
    if (m_collectRtpResolutionCountersForTest)
    {
        ++m_rtpTpgMaterializationCount;
    }
    return m_jettyTpgMaterializations.insert_or_assign(jettyNum,
                                                       std::move(materialization))
        .first->second;
}

std::vector<Ptr<UbJetty>>
UbTransaction::GetJettiesUsingTpn(uint32_t tpn)
{
    NS_LOG_DEBUG(this);
    std::vector<Ptr<UbJetty>> jetties;
    for (const auto& [jettyNum, reference] : m_jettyRtpReferences)
    {
        (void)reference;
        const std::vector<Ptr<UbTransportChannel>> transports = GetJettyLiveTps(jettyNum);
        const bool jettyUsesTpn =
            std::any_of(transports.begin(),
                        transports.end(),
                        [tpn](const Ptr<UbTransportChannel>& transport) {
                            return transport != nullptr && transport->GetTpn() == tpn;
                        });
        Ptr<UbJetty> jetty = jettyUsesTpn ? GetJetty(jettyNum) : nullptr;
        if (jetty != nullptr)
        {
            jetties.push_back(jetty);
        }
    }
    return jetties;
}

void
UbTransaction::TriggerScheduleWqeSegment(uint32_t jettyNum,
                                         Ptr<UbWqe> originatingWqe,
                                         bool lifecycleOperation)
{
    NS_ABORT_MSG_IF(!lifecycleOperation,
                    "Non-lifecycle local RTP scheduling has no live caller");
    EnqueueRtpWqe(jettyNum, originatingWqe, false);
}

void
UbTransaction::TryContinueJettyTransmission(uint32_t jettyNum)
{
    if (!m_tpgsBlockingJettyRetry.empty())
    {
        const UbRtpTransportReference* reference = FindRtpTransportReference(jettyNum);
        if (reference != nullptr && !reference->IsDirectTp() &&
            std::find(m_tpgsBlockingJettyRetry.begin(),
                      m_tpgsBlockingJettyRetry.end(),
                      reference->GetIdentity()) != m_tpgsBlockingJettyRetry.end())
        {
            return;
        }
    }
    Ptr<UbJetty> jetty = GetJetty(jettyNum);
    if (jetty == nullptr || jetty->IsLimited())
    {
        return;
    }

    std::vector<Ptr<UbWqe>> eligibleWqes;
    for (auto operation = m_operations.lower_bound({jettyNum, 0});
         operation != m_operations.end() && operation->first.first == jettyNum;
         ++operation)
    {
        OperationRecord& record = operation->second;
        Ptr<UbWqe> wqe = record.wqe;
        if (record.ctp || record.awaitingTpSegmentCleanup || wqe == nullptr ||
            wqe->GetBytesLeft() == 0 || wqe->IsSentCompleted() ||
            (!wqe->CanSend() && !IsOrderedByInitiator(jettyNum, wqe)))
        {
            continue;
        }
        eligibleWqes.push_back(wqe);
    }

    for (const Ptr<UbWqe>& wqe : eligibleWqes)
    {
        EnqueueRtpWqe(jettyNum, wqe, false);
    }
}

void
UbTransaction::TryContinueJettyTransmission(Ptr<UbTransportChannel> tp)
{
    if (tp == nullptr || tp->IsWqeSegmentLimited() || !tp->CanScheduleAnotherSegment())
    {
        return;
    }

    TryContinueRtpTxQueue({UbRtpTransportReferenceKind::DIRECT_TP, tp->GetLifecycleOwner()}, true);

    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbTransportGroup> tpGroup =
        controller == nullptr ? nullptr
                              : controller->ResolveRtpTpGroupForMember(tp->GetLifecycleOwner());
    if (tpGroup != nullptr)
    {
        TryContinueRtpTxQueue({UbRtpTransportReferenceKind::TPG, tpGroup->GetLifecycleIdentity()},
                              true);
    }

    for (const Ptr<UbJetty>& jetty : GetJettiesUsingTpn(tp->GetTpn()))
    {
        if (jetty != nullptr)
        {
            TryContinueJettyTransmission(jetty->GetJettyNum());
        }
    }
}

void
UbTransaction::TryContinueJettyTransmission(Ptr<UbTransportGroup> tpGroup)
{
    if (tpGroup == nullptr)
    {
        return;
    }

    const UbLifecycleResourceIdentity groupIdentity = tpGroup->GetLifecycleIdentity();
    TryContinueRtpTxQueue({UbRtpTransportReferenceKind::TPG, groupIdentity}, false);

    for (const auto& [jettyNum, reference] : m_jettyRtpReferences)
    {
        if (!reference.IsDirectTp() && reference.GetIdentity() == groupIdentity)
        {
            TryContinueJettyTransmission(jettyNum);
        }
    }
}

void
UbTransaction::RunTrackedPrepareRtpWqeSegment(uint32_t jettyNum,
                                              Ptr<UbWqe> originatingWqe,
                                              RtpTxQueueKey queueKey,
                                              uint64_t eventToken)
{
    if (!GetFunction()->BeginJettyEvent(jettyNum, eventToken))
    {
        return;
    }
    if (IsCurrentWqeOperation(jettyNum, originatingWqe))
    {
        Ptr<UbJetty> jetty = GetJetty(jettyNum);
        NS_ABORT_MSG_IF(jetty == nullptr, "RTP operation references a missing Jetty");
        Ptr<UbWqeSegment> segment = jetty->PrepareNextWqeSegment(originatingWqe);
        if (segment != nullptr)
        {
            ValidateUrmaServiceModeOrDie(jettyNum, segment);
            const uint64_t finishToken = GetFunction()->TrackJettyEvent(jettyNum);
            const EventId finishEvent = Simulator::ScheduleNow(
                &UbTransaction::RunTrackedAdmitRtpWqeSegment,
                this,
                segment,
                originatingWqe,
                queueKey,
                finishToken);
            GetFunction()->AttachJettyEvent(
                jettyNum,
                finishToken,
                finishEvent,
                MakeCallback(&UbTransaction::CancelRtpWqeSegmentTx, this)
                    .Bind(queueKey, jettyNum, originatingWqe));
        }
        else
        {
            ClearRtpWqeSegmentTxState(jettyNum, originatingWqe);
            FinishActiveRtpTxEntry(jettyNum, originatingWqe);
            const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
            if (operation != m_operations.end() && operation->second.wqe == originatingWqe)
            {
                ScheduleRtpTx(
                    MakeRtpTxQueueKey(operation->second.transportReference));
            }
        }
    }
    else
    {
        CancelRtpWqeSegmentTx(queueKey, jettyNum, originatingWqe);
    }
    GetFunction()->ResolveJettyEvent(jettyNum, eventToken);
}

void
UbTransaction::RunTrackedAdmitRtpWqeSegment(Ptr<UbWqeSegment> segment,
                                            Ptr<UbWqe> originatingWqe,
                                            RtpTxQueueKey queueKey,
                                            uint64_t eventToken)
{
    const uint32_t jettyNum = segment->GetJettyNum();
    if (!GetFunction()->BeginJettyEvent(jettyNum, eventToken))
    {
        return;
    }
    if (IsCurrentWqeOperation(jettyNum, originatingWqe))
    {
        Ptr<UbTransportChannel> admittedTp;
        const RtpSegmentAdmissionResult result =
            TryAdmitRtpSegment(jettyNum, originatingWqe, segment, admittedTp);
        ClearRtpWqeSegmentTxState(jettyNum, originatingWqe);
        FinishActiveRtpTxEntry(jettyNum, originatingWqe);
        if (result == RtpSegmentAdmissionResult::ADMITTED)
        {
            NS_LOG_INFO("WQE Segment Sends, taskId:" << segment->GetTaskId()
                                                     << "TASSN: " << segment->GetTaSsn());
            admittedTp->WqeSegmentTriggerPortTransmit(segment);
            if (originatingWqe->GetBytesLeft() != 0)
            {
                TriggerScheduleWqeSegment(jettyNum, originatingWqe, true);
            }
            else
            {
                const auto operation =
                    m_operations.find({jettyNum, originatingWqe->GetWqeId()});
                if (operation != m_operations.end())
                {
                    ScheduleRtpTx(
                        MakeRtpTxQueueKey(operation->second.transportReference));
                }
            }
        }
        else if (result == RtpSegmentAdmissionResult::STALE_PREPARED_CANDIDATE)
        {
            TriggerScheduleWqeSegment(jettyNum, originatingWqe, true);
        }
        else if (result == RtpSegmentAdmissionResult::DEFERRED)
        {
            EnqueueRtpWqeAfterBackpressure(jettyNum, originatingWqe);
        }
        else
        {
            const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
            if (operation != m_operations.end())
            {
                ScheduleRtpTx(
                    MakeRtpTxQueueKey(operation->second.transportReference));
            }
        }
    }
    else
    {
        CancelRtpWqeSegmentTx(queueKey, jettyNum, originatingWqe);
    }
    GetFunction()->ResolveJettyEvent(jettyNum, eventToken);
}

Ptr<UbWqeSegment>
UbTransaction::PrepareNextCtpWqeSegment(Ptr<UbJetty> jetty, uint32_t maxSegmentBytes)
{
    NS_ABORT_MSG_IF(jetty == nullptr, "CTP segmentation requires a Jetty");
    return jetty->PrepareNextWqeSegment(nullptr, maxSegmentBytes);
}

void
UbTransaction::CommitCtpWqeSegment(Ptr<UbWqeSegment> segment)
{
    NS_ABORT_MSG_IF(segment == nullptr, "Cannot commit a null CTP Transaction segment");
    Ptr<UbJetty> jetty = GetJetty(segment->GetJettyNum());
    NS_ABORT_MSG_IF(jetty == nullptr, "CTP Transaction segment lost its Jetty binding");
    auto operation = m_operations.find({segment->GetJettyNum(), segment->GetTaskId()});
    if (operation != m_operations.end())
    {
        NS_ABORT_MSG_IF(!operation->second.ctp,
                        "CTP segment references an RTP Transaction operation");
        NS_ABORT_MSG_IF(operation->second.segments.find(segment->GetTaSsn()) !=
                            operation->second.segments.end(),
                        "Duplicate CTP Transaction segment admission");
        const uint64_t localId = (static_cast<uint64_t>(segment->GetJettyNum()) << 32) |
                                 segment->GetTaSsn();
        OperationRecord::SegmentRecord record;
        record.segment = segment;
        record.identity = m_coordinator->OpenResource(
            UbLifecycleResourceKind::TRANSACTION_SEGMENT,
            localId);
        record.childLease = m_coordinator->AcquireLease(operation->second.wqeIdentity,
                                                        UbLifecycleLeaseKind::CHILD,
                                                        record.identity);
        segment->SetTransactionCorrelation(operation->second.operationIdentity, record.identity);
        operation->second.segments.emplace(segment->GetTaSsn(), record);
    }
    jetty->CommitWqeSegmentAdmission(segment);
}

UbCtpTransactionState&
UbTransaction::GetOrCreateCtpTransactionState(const UbCtpEntityKey& key)
{
    return m_ctpTransactionStates.try_emplace(key).first->second;
}

Ptr<UbCtpTransactionContext>
UbTransaction::GetOrCreateCtpTransactionContext(const UbCtpEntityKey& key)
{
    UbCtpTransactionState& state = GetOrCreateCtpTransactionState(key);
    if (state.context == nullptr)
    {
        const auto cursor = m_nextCtpTaSsnByEntity.find(key);
        const uint32_t initialSequence =
            cursor == m_nextCtpTaSsnByEntity.end() ? 0 : cursor->second;
        state.context = Create<UbCtpTransactionContext>(
            key,
            UB_JETTY_TASSN_OOO_THRESHOLD,
            initialSequence);
    }
    return state.context;
}

const UbCtpTransactionState*
UbTransaction::FindCtpTransactionState(const UbCtpEntityKey& key) const
{
    const auto it = m_ctpTransactionStates.find(key);
    return it == m_ctpTransactionStates.end() ? nullptr : &it->second;
}

UbCtpTransactionState*
UbTransaction::FindCtpTransactionState(const UbCtpEntityKey& key)
{
    const auto it = m_ctpTransactionStates.find(key);
    return it == m_ctpTransactionStates.end() ? nullptr : &it->second;
}

void
UbTransaction::RetainCtpCompletionEvent(const UbCtpEntityKey& key)
{
    ++GetOrCreateCtpTransactionState(key).pendingCompletionEvents;
}

void
UbTransaction::ReleaseCtpCompletionEvent(const UbCtpEntityKey& key)
{
    UbCtpTransactionState* state = FindCtpTransactionState(key);
    NS_ABORT_MSG_IF(state == nullptr || state->pendingCompletionEvents == 0,
                    "CTP completion event release would underflow");
    --state->pendingCompletionEvents;
}

bool
UbTransaction::TryRetireCtpTransactionState(const UbCtpEntityKey& key)
{
    auto state = m_ctpTransactionStates.find(key);
    if (state == m_ctpTransactionStates.end())
    {
        return true;
    }
    const bool activeOperation = std::any_of(
        m_operations.begin(),
        m_operations.end(),
        [&key](const auto& entry) { return entry.second.ctp && entry.second.ctpKey == key; });
    const UbCtpTransactionState& transactionState = state->second;
    if (activeOperation || !transactionState.pendingSegments.empty() ||
        !transactionState.outstandingSegments.empty() ||
        !transactionState.segmentOutstandingPackets.empty() ||
        transactionState.pendingCompletionEvents != 0 ||
        (transactionState.context != nullptr &&
         transactionState.context->GetOutstandingCount() != 0))
    {
        return false;
    }
    if (transactionState.context != nullptr)
    {
        m_nextCtpTaSsnByEntity[key] = transactionState.context->GetSendNext();
    }
    m_ctpTransactionStates.erase(state);
    return true;
}

bool UbTransaction::IsUrmaReadWriteRequest(const Ptr<UbWqeSegment>& segment)
{
    if (segment == nullptr) {
        return false;
    }
    if (segment->GetSegmentKind() != UbTransactionSegmentKind::REQUEST) {
        return false;
    }
    return segment->GetType() == TaOpcode::TA_OPCODE_WRITE ||
           segment->GetType() == TaOpcode::TA_OPCODE_READ;
}

void UbTransaction::ValidateUrmaServiceModeOrDie(uint32_t jettyNum, const Ptr<UbWqeSegment>& segment)
{
    if (!IsUrmaReadWriteRequest(segment)) {
        return;
    }
    TransactionServiceMode mode = GetTransactionServiceMode(jettyNum);
    NS_ABORT_MSG_IF(mode != TransactionServiceMode::ROI,
                    "URMA read/write currently only supports ROI service mode.");
}

void
UbTransaction::ClearRtpWqeSegmentTxState(uint32_t jettyNum, Ptr<UbWqe> originatingWqe)
{
    if (originatingWqe == nullptr)
    {
        return;
    }
    const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation != m_operations.end() && operation->second.wqe == originatingWqe)
    {
        operation->second.rtpWqeSegmentTxActive = false;
    }
}

bool
UbTransaction::IsCurrentWqeOperation(uint32_t jettyNum, Ptr<UbWqe> wqe) const
{
    if (wqe == nullptr)
    {
        return false;
    }
    const auto operation = m_operations.find({jettyNum, wqe->GetWqeId()});
    return operation != m_operations.end() && operation->second.wqe == wqe &&
           !operation->second.awaitingTpSegmentCleanup;
}

bool
UbTransaction::HasLifecycleOperationForJetty(uint32_t jettyNum) const
{
    const auto first = m_operations.lower_bound({jettyNum, 0});
    return first != m_operations.end() && first->first.first == jettyNum;
}

bool
UbTransaction::HasRtpTpGroupWork(
    const UbLifecycleResourceIdentity& groupIdentity) const
{
    return std::any_of(
        m_operations.begin(),
        m_operations.end(),
        [&groupIdentity](const auto& entry) {
            const OperationRecord& operation = entry.second;
            return !operation.ctp && !operation.awaitingTpSegmentCleanup &&
                   !operation.transportReference.IsDirectTp() &&
                   operation.transportReference.GetIdentity() == groupIdentity;
        });
}

UbTransaction::RtpSegmentAdmissionResult
UbTransaction::TryAdmitRtpSegment(uint32_t jettyNum,
                                  Ptr<UbWqe> originatingWqe,
                                  Ptr<UbWqeSegment> segment,
                                  Ptr<UbTransportChannel>& admittedTp)
{
    if (originatingWqe == nullptr || segment == nullptr)
    {
        return RtpSegmentAdmissionResult::REJECTED;
    }

    const std::pair<uint32_t, uint32_t> key{jettyNum, originatingWqe->GetWqeId()};
    auto operation = m_operations.find(key);
    if (operation == m_operations.end() || operation->second.wqe != originatingWqe)
    {
        return RtpSegmentAdmissionResult::REJECTED;
    }
    NS_ABORT_MSG_IF(operation->second.segments.find(segment->GetTaSsn()) !=
                        operation->second.segments.end(),
                    "Duplicate Transaction segment admission");

    Ptr<UbJetty> jetty = GetJetty(jettyNum);
    if (jetty == nullptr || !jetty->CanCommitWqeSegmentAdmission(segment, originatingWqe))
    {
        return jetty != nullptr && segment->GetTaskId() == originatingWqe->GetWqeId()
                   ? RtpSegmentAdmissionResult::STALE_PREPARED_CANDIDATE
                   : RtpSegmentAdmissionResult::REJECTED;
    }

    Ptr<UbTransportGroup> selectedGroup;
    uint32_t selectedGroupIndex = UINT32_MAX;
    const UbRtpTransportReference& reference = operation->second.transportReference;
    RtpTransportResolution resolution;
    if (!ResolveRtpTransportReference(reference, resolution) && reference.IsDirectTp())
    {
        Ptr<UbController> controller = GetOwnerController();
        resolution.kind = reference.GetKind();
        resolution.identity = reference.GetIdentity();
        resolution.directTpn = reference.GetDirectTpn();
        resolution.directTp =
            controller == nullptr
                ? nullptr
                : controller->GetTransportChannelManager()->ResolveTpForAcceptedWork(
                      reference.GetDirectTpn(),
                      reference.GetIdentity());
    }
    if (resolution.directTp == nullptr && resolution.group == nullptr)
    {
        return RtpSegmentAdmissionResult::REJECTED;
    }
    if (reference.IsDirectTp())
    {
        if (!resolution.directTp->CanContinueAcceptedWqeSegment(segment))
        {
            return RtpSegmentAdmissionResult::DEFERRED;
        }
        admittedTp = resolution.directTp;
    }
    else
    {
        selectedGroup = resolution.group;
        const RtpTpgMaterialization& materialization =
            MaterializeRtpTpGroup(jettyNum, reference, selectedGroup);
        bool hasLiveMember = false;
        for (uint32_t offset = 0; offset < selectedGroup->GetMemberCount(); ++offset)
        {
            const uint32_t candidateIndex = selectedGroup->GetMemberIndexAtOffset(offset);
            if (!selectedGroup->IsMemberSelectable(candidateIndex))
            {
                continue;
            }
            const UbTransportGroupMember& member = selectedGroup->GetMember(candidateIndex);
            Ptr<UbTransportChannel> transport =
                materialization.indexedTransports[candidateIndex];
            if (transport == nullptr || transport->GetLifecycleOwner() != member.tpIdentity ||
                !m_coordinator->IsResourceActive(member.tpIdentity))
            {
                continue;
            }
            hasLiveMember = true;
            if (transport->CanAdmitWqeSegment(segment))
            {
                selectedGroupIndex = candidateIndex;
                admittedTp = transport;
                break;
            }
        }
        if (admittedTp == nullptr)
        {
            return hasLiveMember ? RtpSegmentAdmissionResult::DEFERRED
                                 : RtpSegmentAdmissionResult::REJECTED;
        }
    }

    // Every expected rejection is complete above. From here to cursor commit, owner-local
    // execution has no callback or event interleave.
    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    const uint64_t localId = (static_cast<uint64_t>(segment->GetJettyNum()) << 32) |
                             segment->GetTaSsn();
    OperationRecord::SegmentRecord record;
    record.segment = segment;
    record.identity = coordinator->OpenResource(UbLifecycleResourceKind::TRANSACTION_SEGMENT,
                                                localId);
    record.childLease = coordinator->AcquireLease(operation->second.wqeIdentity,
                                                  UbLifecycleLeaseKind::CHILD,
                                                  record.identity);
    record.resolvedTpIdentity = admittedTp->GetLifecycleOwner();
    record.transportWorkLease = coordinator->AcquireLease(
        record.resolvedTpIdentity,
        UbLifecycleLeaseKind::TRANSPORT_WORK,
        record.identity);
    segment->SetTransactionCorrelation(operation->second.operationIdentity, record.identity);
    const bool inserted =
        operation->second.segments.emplace(segment->GetTaSsn(), record).second;
    NS_ABORT_MSG_IF(!inserted, "Duplicate Transaction segment admission commit");

    const bool admitted = reference.IsDirectTp()
                              ? admittedTp->TryContinueAcceptedWqeSegment(segment)
                              : admittedTp->TryAdmitWqeSegment(segment);
    NS_ABORT_MSG_IF(!admitted,
                    "RTP TP admission changed after owner-local preflight");
    NS_ABORT_MSG_IF(segment->GetResolvedTpIdentity() != record.resolvedTpIdentity,
                    "Admitted segment did not pin the resolved TP generation");
    jetty->CommitWqeSegmentAdmission(segment);
    if (selectedGroup != nullptr)
    {
        selectedGroup->CommitSelection(selectedGroupIndex);
    }
    return RtpSegmentAdmissionResult::ADMITTED;
}

UbTransaction::RtpTxQueueKey
UbTransaction::MakeRtpTxQueueKey(const UbRtpTransportReference& reference)
{
    return {reference.GetKind(), reference.GetIdentity()};
}

void
UbTransaction::StartRtpWqeSegmentTx(uint32_t jettyNum, Ptr<UbWqe> originatingWqe)
{
    const auto operation = originatingWqe == nullptr
                               ? m_operations.end()
                               : m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation == m_operations.end() || operation->second.wqe != originatingWqe ||
        operation->second.awaitingTpSegmentCleanup || operation->second.rtpWqeSegmentTxActive)
    {
        return;
    }
    operation->second.rtpWqeSegmentTxActive = true;
    const RtpTxQueueKey queueKey =
        MakeRtpTxQueueKey(operation->second.transportReference);
    const uint64_t eventToken = GetFunction()->TrackJettyEvent(jettyNum);
    const EventId event = Simulator::ScheduleNow(
        &UbTransaction::RunTrackedPrepareRtpWqeSegment,
        this,
        jettyNum,
        originatingWqe,
        queueKey,
        eventToken);
    GetFunction()->AttachJettyEvent(
        jettyNum,
        eventToken,
        event,
        MakeCallback(&UbTransaction::CancelRtpWqeSegmentTx, this)
            .Bind(queueKey, jettyNum, originatingWqe));
}

void
UbTransaction::ScheduleRtpTx(const RtpTxQueueKey& queueKey)
{
    const auto queueIt = m_rtpTxQueues.find(queueKey);
    if (queueIt == m_rtpTxQueues.end() || queueIt->second.waitingForTransportCapacity ||
        queueIt->second.pendingTxEntries.empty() || queueIt->second.txEventPending ||
        queueIt->second.txEntryActive)
    {
        return;
    }
    queueIt->second.txEventPending = true;
    queueIt->second.pendingEvent = Simulator::ScheduleNow(&UbTransaction::RunRtpTx, this, queueKey);
}

void
UbTransaction::EnqueueRtpWqe(uint32_t jettyNum, Ptr<UbWqe> originatingWqe, bool restoreWaitingFront)
{
    if (originatingWqe == nullptr)
    {
        return;
    }
    const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation == m_operations.end() || operation->second.wqe != originatingWqe ||
        operation->second.awaitingTpSegmentCleanup || operation->second.rtpWqeSegmentTxActive)
    {
        return;
    }

    const RtpTxQueueKey key = MakeRtpTxQueueKey(operation->second.transportReference);
    RtpTxQueue& queue = m_rtpTxQueues[key];
    if (!operation->second.txEntryQueued)
    {
        const RtpTxQueueEntry entry{RtpTxQueueEntry::Kind::LOCAL_WQE,
                                    jettyNum,
                                    originatingWqe->GetWqeId(),
                                    operation->second.operationIdentity};
        if (restoreWaitingFront)
        {
            queue.pendingTxEntries.push_front(entry);
        }
        else
        {
            queue.pendingTxEntries.push_back(entry);
        }
        operation->second.txEntryQueued = true;
    }
    if (!restoreWaitingFront && !queue.txEntryActive && !queue.txEventPending &&
        !queue.waitingForTransportCapacity)
    {
        RunRtpTx(key);
    }
}

void
UbTransaction::EnqueueRemoteRtpWqeSegment(uint32_t tpn, Ptr<UbWqeSegment> segment)
{
    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbTransportChannelManager> manager =
        controller == nullptr ? nullptr : controller->GetTransportChannelManager();
    if (segment == nullptr || manager == nullptr)
    {
        return;
    }
    UbLifecycleResourceIdentity tpIdentity;
    if (segment->HasResolvedTpIdentity())
    {
        tpIdentity = segment->GetResolvedTpIdentity();
    }
    else if (!manager->TryGetEndpointIdentity(tpn, tpIdentity))
    {
        return;
    }
    Ptr<UbTransportChannel> transport =
        segment->HasResolvedTpIdentity()
            ? manager->ResolveTpForAcceptedWork(tpn, tpIdentity)
            : manager->ResolveLiveTp(tpn, tpIdentity);
    if (transport == nullptr)
    {
        transport = segment->HasResolvedTpIdentity()
                        ? manager->ResolveLiveTp(tpn, tpIdentity)
                        : manager->ResolveTpForAcceptedWork(tpn, tpIdentity);
    }
    if (transport == nullptr)
    {
        return;
    }

    UbLifecycleResourceIdentity operationIdentity;
    if (segment->HasTransactionCorrelation())
    {
        operationIdentity = segment->GetTransactionOperationIdentity();
    }
    else
    {
        operationIdentity = {segment->GetSrc(),
                             UbLifecycleResourceKind::TRANSACTION_OPERATION,
                             (static_cast<uint64_t>(segment->GetOriginJettyNum()) << 32) |
                                 segment->GetTaskId(),
                             0};
    }
    const RemoteRtpWqeSegmentKey remoteKey{segment->GetOriginJettyNum(),
                                           segment->GetTaskId(),
                                           operationIdentity};
    RemoteRtpWqeSegmentState& remote = m_remoteRtpWqeSegmentsByTpn[tpn][remoteKey];
    remote.segments.push_back(segment);

    const RtpTxQueueKey queueKey{UbRtpTransportReferenceKind::DIRECT_TP,
                                 transport->GetLifecycleOwner()};
    RtpTxQueue& queue = m_rtpTxQueues[queueKey];
    if (!remote.txEntryQueued)
    {
        queue.pendingTxEntries.push_back({RtpTxQueueEntry::Kind::REMOTE_WQE_SEGMENT,
                                          remoteKey.originJettyNum,
                                          remoteKey.taskId,
                                          remoteKey.operationIdentity});
        remote.txEntryQueued = true;
    }
    if (!queue.txEntryActive && !queue.txEventPending && !queue.waitingForTransportCapacity)
    {
        RunRtpTx(queueKey);
    }
}

void
UbTransaction::RunRemoteRtpWqeSegmentTx(const RtpTxQueueKey& queueKey, const RtpTxQueueEntry& entry)
{
    const uint32_t tpn = static_cast<uint32_t>(queueKey.identity.localId);
    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbTransportChannelManager> manager =
        controller == nullptr ? nullptr : controller->GetTransportChannelManager();
    Ptr<UbTransportChannel> transport =
        manager == nullptr
            ? nullptr
            : manager->ResolveTpForAcceptedWork(tpn, queueKey.identity);
    if (transport == nullptr && manager != nullptr)
    {
        transport = manager->ResolveLiveTp(tpn, queueKey.identity);
    }
    auto remoteWqeSegmentsByTpn = m_remoteRtpWqeSegmentsByTpn.find(tpn);
    const RemoteRtpWqeSegmentKey remoteKey{entry.jettyNum,
                                    entry.wqeId,
                                    entry.operationIdentity};
    if (transport == nullptr || remoteWqeSegmentsByTpn == m_remoteRtpWqeSegmentsByTpn.end())
    {
        return;
    }
    auto remote = remoteWqeSegmentsByTpn->second.find(remoteKey);
    if (remote == remoteWqeSegmentsByTpn->second.end())
    {
        return;
    }
    remote->second.txEntryQueued = false;
    while (!remote->second.segments.empty() && remote->second.segments.front() == nullptr)
    {
        remote->second.segments.pop_front();
    }
    if (remote->second.segments.empty())
    {
        remoteWqeSegmentsByTpn->second.erase(remote);
        if (remoteWqeSegmentsByTpn->second.empty())
        {
            m_remoteRtpWqeSegmentsByTpn.erase(remoteWqeSegmentsByTpn);
        }
        return;
    }

    Ptr<UbWqeSegment> segment = remote->second.segments.front();
    if (!transport->TryContinueAcceptedWqeSegment(segment))
    {
        RtpTxQueue& queue = m_rtpTxQueues[queueKey];
        queue.pendingTxEntries.push_front(entry);
        remote->second.txEntryQueued = true;
        queue.waitingForTransportCapacity = true;
        return;
    }

    remote->second.segments.pop_front();
    if (!remote->second.segments.empty())
    {
        m_rtpTxQueues[queueKey].pendingTxEntries.push_back(entry);
        remote->second.txEntryQueued = true;
    }
    else
    {
        remoteWqeSegmentsByTpn->second.erase(remote);
        if (remoteWqeSegmentsByTpn->second.empty())
        {
            m_remoteRtpWqeSegmentsByTpn.erase(remoteWqeSegmentsByTpn);
        }
    }
    NS_LOG_INFO("Remote WQE Segment Sends, taskId:" << segment->GetTaskId()
                                                     << "TASSN: " << segment->GetTaSsn());
    transport->WqeSegmentTriggerPortTransmit(segment);
    ScheduleRtpTx(queueKey);
}

void
UbTransaction::EnqueueRtpWqeAfterBackpressure(uint32_t jettyNum, Ptr<UbWqe> originatingWqe)
{
    EnqueueRtpWqe(jettyNum, originatingWqe, true);
    if (originatingWqe == nullptr)
    {
        return;
    }
    const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation != m_operations.end())
    {
        m_rtpTxQueues[MakeRtpTxQueueKey(operation->second.transportReference)]
            .waitingForTransportCapacity = true;
    }
}

void
UbTransaction::TryContinueRtpTxQueue(const RtpTxQueueKey& key, bool capacityTransition)
{
    const auto queueIt = m_rtpTxQueues.find(key);
    if (queueIt == m_rtpTxQueues.end() || !queueIt->second.waitingForTransportCapacity ||
        queueIt->second.pendingTxEntries.empty() || queueIt->second.txEventPending)
    {
        return;
    }
    queueIt->second.waitingForTransportCapacity = false;
    ScheduleRtpTx(key);
    if (capacityTransition && queueIt->second.txEventPending)
    {
        ++queueIt->second.transportCapacityRecoveryEvents;
    }
}

void
UbTransaction::NotifyRtpTpGroupMemberAvailable(const UbLifecycleResourceIdentity& groupIdentity)
{
    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbTransportGroup> group =
        controller == nullptr ? nullptr : controller->ResolveRtpTpGroup(groupIdentity);
    if (group != nullptr)
    {
        TryContinueJettyTransmission(group);
    }
}

void
UbTransaction::TerminateRtpTpGroupOperations(const UbLifecycleResourceIdentity& groupIdentity,
                                             UbWorkOutcome outcome,
                                             bool discardRetainedSegments)
{
    NS_ABORT_MSG_IF(outcome == UbWorkOutcome::COMPLETED,
                    "TPG cleanup requires a terminal failure outcome");

    struct BlockTpgJettyRetryGuard
    {
        BlockTpgJettyRetryGuard(std::vector<UbLifecycleResourceIdentity>& blockedTpgIdentities,
                                const UbLifecycleResourceIdentity& tpgIdentity)
            : blockedTpgIdentities(blockedTpgIdentities),
              tpgIdentity(tpgIdentity)
        {
            blockedTpgIdentities.push_back(tpgIdentity);
        }

        ~BlockTpgJettyRetryGuard()
        {
            NS_ABORT_MSG_IF(blockedTpgIdentities.empty() ||
                                blockedTpgIdentities.back() != tpgIdentity,
                            "TPG cleanup Jetty retry block is unbalanced");
            blockedTpgIdentities.pop_back();
        }

        std::vector<UbLifecycleResourceIdentity>& blockedTpgIdentities;
        UbLifecycleResourceIdentity tpgIdentity;
    } blockJettyRetry(m_tpgsBlockingJettyRetry, groupIdentity);

    std::vector<std::pair<uint32_t, uint32_t>> tpgOperationKeys;
    for (const auto& [key, operation] : m_operations)
    {
        if (!operation.transportReference.IsDirectTp() &&
            operation.transportReference.GetIdentity() == groupIdentity &&
            (discardRetainedSegments || !operation.awaitingTpSegmentCleanup))
        {
            tpgOperationKeys.push_back(key);
        }
    }
    for (const auto& [jettyNum, wqeId] : tpgOperationKeys)
    {
        const auto operation = m_operations.find({jettyNum, wqeId});
        if (operation == m_operations.end() ||
            operation->second.transportReference.IsDirectTp() ||
            operation->second.transportReference.GetIdentity() != groupIdentity)
        {
            continue;
        }
        if (!operation->second.awaitingTpSegmentCleanup)
        {
            CompleteWqeOperation(jettyNum, wqeId, outcome);
        }
        if (discardRetainedSegments)
        {
            DiscardRetainedWqeSegmentsForSimulationStop(jettyNum, wqeId);
        }
    }
}

void
UbTransaction::RunRtpTx(RtpTxQueueKey queueKey)
{
    const auto queueIt = m_rtpTxQueues.find(queueKey);
    if (queueIt == m_rtpTxQueues.end())
    {
        return;
    }
    queueIt->second.txEventPending = false;
    queueIt->second.pendingEvent = EventId();
    if (queueIt->second.txEntryActive || queueIt->second.waitingForTransportCapacity)
    {
        return;
    }

    while (!queueIt->second.pendingTxEntries.empty())
    {
        const RtpTxQueueEntry entry = queueIt->second.pendingTxEntries.front();
        queueIt->second.pendingTxEntries.pop_front();
        if (entry.kind == RtpTxQueueEntry::Kind::REMOTE_WQE_SEGMENT)
        {
            RunRemoteRtpWqeSegmentTx(queueKey, entry);
            break;
        }
        const auto operation = m_operations.find({entry.jettyNum, entry.wqeId});
        if (operation == m_operations.end() ||
            operation->second.operationIdentity != entry.operationIdentity)
        {
            continue;
        }
        operation->second.txEntryQueued = false;
        if (!operation->second.awaitingTpSegmentCleanup)
        {
            queueIt->second.txEntryActive = true;
            queueIt->second.activeTxEntry = entry;
            StartRtpWqeSegmentTx(entry.jettyNum, operation->second.wqe);
        }
        break;
    }
}

void
UbTransaction::FinishActiveRtpTxEntry(uint32_t jettyNum, Ptr<UbWqe> originatingWqe)
{
    if (originatingWqe == nullptr)
    {
        return;
    }
    const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation == m_operations.end() || operation->second.wqe != originatingWqe)
    {
        return;
    }
    const auto queueIt =
        m_rtpTxQueues.find(MakeRtpTxQueueKey(operation->second.transportReference));
    if (queueIt != m_rtpTxQueues.end() && queueIt->second.activeTxEntry.has_value() &&
        queueIt->second.activeTxEntry->kind == RtpTxQueueEntry::Kind::LOCAL_WQE &&
        queueIt->second.activeTxEntry->jettyNum == jettyNum &&
        queueIt->second.activeTxEntry->wqeId == originatingWqe->GetWqeId() &&
        queueIt->second.activeTxEntry->operationIdentity == operation->second.operationIdentity)
    {
        queueIt->second.txEntryActive = false;
        queueIt->second.activeTxEntry.reset();
    }
}

void
UbTransaction::CancelRtpWqeSegmentTx(RtpTxQueueKey queueKey,
                                     uint32_t jettyNum,
                                     Ptr<UbWqe> originatingWqe)
{
    ClearRtpWqeSegmentTxState(jettyNum, originatingWqe);
    const auto queueIt = m_rtpTxQueues.find(queueKey);
    if (queueIt != m_rtpTxQueues.end())
    {
        queueIt->second.txEntryActive = false;
        queueIt->second.activeTxEntry.reset();
        ScheduleRtpTx(queueKey);
    }
}

void
UbTransaction::DetachRtpTxEntry(OperationRecord& operation)
{
    const RtpTxQueueKey key = MakeRtpTxQueueKey(operation.transportReference);
    const auto queueIt = m_rtpTxQueues.find(key);
    operation.txEntryQueued = false;
    operation.rtpWqeSegmentTxActive = false;
    if (queueIt == m_rtpTxQueues.end())
    {
        return;
    }
    if (queueIt->second.activeTxEntry.has_value())
    {
        const RtpTxQueueEntry& active = *queueIt->second.activeTxEntry;
        if (active.kind == RtpTxQueueEntry::Kind::LOCAL_WQE &&
            active.operationIdentity == operation.operationIdentity)
        {
            queueIt->second.txEntryActive = false;
            queueIt->second.activeTxEntry.reset();
        }
    }
    queueIt->second.waitingForTransportCapacity = false;
    ScheduleRtpTx(key);
}

void
UbTransaction::RetireRtpTxQueue(const UbLifecycleResourceIdentity& identity)
{
    const UbRtpTransportReferenceKind kind = identity.kind == UbLifecycleResourceKind::RTP_TPG
                                                 ? UbRtpTransportReferenceKind::TPG
                                                 : UbRtpTransportReferenceKind::DIRECT_TP;
    const RtpTxQueueKey key{kind, identity};
    const auto queueIt = m_rtpTxQueues.find(key);
    if (queueIt == m_rtpTxQueues.end())
    {
        return;
    }
    if (queueIt->second.txEventPending)
    {
        Simulator::Cancel(queueIt->second.pendingEvent);
    }
    for (const RtpTxQueueEntry& entry : queueIt->second.pendingTxEntries)
    {
        if (entry.kind == RtpTxQueueEntry::Kind::LOCAL_WQE)
        {
            const auto operation = m_operations.find({entry.jettyNum, entry.wqeId});
            if (operation != m_operations.end() &&
                operation->second.operationIdentity == entry.operationIdentity)
            {
                operation->second.txEntryQueued = false;
            }
        }
        else if (kind == UbRtpTransportReferenceKind::DIRECT_TP)
        {
            const uint32_t tpn = static_cast<uint32_t>(identity.localId);
            const auto remoteWqeSegmentsByTpn = m_remoteRtpWqeSegmentsByTpn.find(tpn);
            if (remoteWqeSegmentsByTpn != m_remoteRtpWqeSegmentsByTpn.end())
            {
                const RemoteRtpWqeSegmentKey remoteKey{entry.jettyNum,
                                                       entry.wqeId,
                                                       entry.operationIdentity};
                const auto remote = remoteWqeSegmentsByTpn->second.find(remoteKey);
                if (remote != remoteWqeSegmentsByTpn->second.end())
                {
                    remote->second.txEntryQueued = false;
                }
            }
        }
    }
    if (queueIt->second.txEntryActive)
    {
        NS_ABORT_MSG_IF(!queueIt->second.activeTxEntry.has_value() ||
                            queueIt->second.activeTxEntry->kind != RtpTxQueueEntry::Kind::LOCAL_WQE,
                        "Cannot retire an RTP Tx queue with unknown active entry");
        const RtpTxQueueEntry& active = *queueIt->second.activeTxEntry;
        const auto operation = m_operations.find({active.jettyNum, active.wqeId});
        NS_ABORT_MSG_IF(operation != m_operations.end() &&
                            (operation->second.operationIdentity != active.operationIdentity ||
                             operation->second.wqe == nullptr ||
                             operation->second.wqe->GetBytesLeft() != 0),
                        "Cannot retire an RTP Tx queue before final assignment");
        if (operation != m_operations.end())
        {
            operation->second.txEntryQueued = false;
        }
        queueIt->second.txEntryActive = false;
        queueIt->second.activeTxEntry.reset();
    }
    m_rtpTxQueues.erase(queueIt);
}

bool
UbTransaction::RecordSegmentSuccess(uint32_t jettyNum,
                                    uint32_t wqeId,
                                    uint32_t taSsn,
                                    Ptr<UbWqeSegment> completion)
{
    auto operation = m_operations.find({jettyNum, wqeId});
    if (operation == m_operations.end())
    {
        return completion == nullptr || !completion->HasTransactionCorrelation();
    }
    auto segment = operation->second.segments.find(taSsn);
    if (segment == operation->second.segments.end())
    {
        return false;
    }
    if (operation->second.ctp)
    {
        if (completion == nullptr || !completion->HasTransactionCorrelation() ||
            completion->GetTransactionOperationIdentity() != operation->second.operationIdentity ||
            completion->GetTransactionSegmentIdentity() != segment->second.identity)
        {
            return false;
        }
        m_coordinator->ReleaseLease(segment->second.childLease);
        m_coordinator->RecordWorkTerminal(segment->second.identity, UbWorkOutcome::COMPLETED);
        m_coordinator->RetireResource(segment->second.identity);
        operation->second.segments.erase(segment);
        return true;
    }
    if (completion == nullptr || !completion->HasTransactionCorrelation() ||
        completion->GetTransactionOperationIdentity() != operation->second.operationIdentity ||
        completion->GetTransactionSegmentIdentity() != segment->second.identity ||
        !completion->HasResolvedTpIdentity() ||
        completion->GetResolvedTpIdentity() != segment->second.resolvedTpIdentity ||
        !m_coordinator->IsResourceActive(segment->second.resolvedTpIdentity))
    {
        return false;
    }

    const bool awaitingTpSegmentCleanup = operation->second.awaitingTpSegmentCleanup;
    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    coordinator->ReleaseLease(segment->second.childLease);
    coordinator->ReleaseLease(segment->second.transportWorkLease);
    if (!awaitingTpSegmentCleanup)
    {
        coordinator->RecordWorkTerminal(segment->second.identity, UbWorkOutcome::COMPLETED);
    }
    coordinator->RetireResource(segment->second.identity);
    operation->second.segments.erase(segment);
    if (awaitingTpSegmentCleanup && operation->second.segments.empty())
    {
        const UbRtpTransportReference reference = operation->second.transportReference;
        coordinator->ReleaseLease(operation->second.workLease);
        coordinator->RetireResource(operation->second.operationIdentity);
        coordinator->RetireResource(operation->second.wqeIdentity);
        m_operations.erase(operation);
        NotifyRtpTpGroupWorkChanged(reference);
        GetFunction()->NotifyJettyProgress(jettyNum);
    }
    return true;
}

bool UbTransaction::ProcessWqeSegmentComplete(Ptr<UbWqeSegment> wqeSegment)
{
    if (wqeSegment == nullptr) {
        return false;
    }

    uint32_t jettyNum = wqeSegment->GetJettyNum();
    uint32_t taSsn = wqeSegment->GetTaSsn();
    if (wqeSegment->GetSegmentKind() == UbTransactionSegmentKind::RESPONSE) {
        jettyNum = wqeSegment->GetOriginJettyNum();
        taSsn = wqeSegment->GetRequestTassn();
    }

    const auto operation = m_operations.find({jettyNum, wqeSegment->GetTaskId()});
    const bool awaitingTpSegmentCleanup =
        operation != m_operations.end() && operation->second.awaitingTpSegmentCleanup;
    if (!RecordSegmentSuccess(jettyNum, wqeSegment->GetTaskId(), taSsn, wqeSegment))
    {
        return false;
    }
    if (awaitingTpSegmentCleanup)
    {
        return true;
    }

    Ptr<UbJetty> jetty = GetJetty(jettyNum);
    if (jetty == nullptr) {
        return false;
    }
    return jetty->ProcessWqeSegmentComplete(taSsn);
}

void UbTransaction::HandleInboundTaUnit(uint32_t localTpn, Ptr<UbWqeSegment> segment)
{
    if (segment == nullptr) {
        return;
    }

    if (segment->GetSegmentKind() == UbTransactionSegmentKind::RESPONSE) {
        ProcessInboundTaResponse(segment);
        return;
    }

    if (IsUrmaReadWriteRequest(segment) && HasRemoteUrmaRequestProcessor())
    {
        Ptr<UbTransaction> self(this);
        UbRemoteUrmaResponseSender responseSender =
            MakeBoundCallback(&UbTransaction::SendBoundRemoteUrmaResponse,
                              self,
                              localTpn,
                              segment);
        DispatchRemoteUrmaRequest(segment, responseSender);
        return;
    }

    Ptr<UbWqeSegment> response = ProcessInboundTaRequest(segment);
    EnqueueInboundTaResponse(localTpn, response);
}

bool
UbTransaction::DispatchRemoteUrmaRequest(Ptr<UbWqeSegment> request,
                                         UbRemoteUrmaResponseSender responseSender)
{
    if (!IsUrmaReadWriteRequest(request) || responseSender.IsNull() ||
        m_remoteUrmaRequestProcessor.IsNull())
    {
        return false;
    }

    const UbRemoteUrmaRequestContext requestContext{
        .operation = request->GetType(),
        .bytes = request->GetSize(),
        .sourceNodeId = request->GetSrc(),
        .targetNodeId = request->GetDest(),
        .priority = request->GetPriority(),
        .address = request->GetRemoteAddress(),
        .tokenId = request->GetRemoteTokenId(),
        .payload = request->GetExplicitPayload()};
    return m_remoteUrmaRequestProcessor(requestContext, responseSender);
}

Ptr<UbWqeSegment>
UbTransaction::BuildRemoteUrmaResponse(Ptr<UbWqeSegment> request) const
{
    if (request == nullptr)
    {
        return nullptr;
    }
    if (request->GetType() == TaOpcode::TA_OPCODE_WRITE)
    {
        return BuildRemoteWriteAck(UINT32_MAX, request);
    }
    if (request->GetType() == TaOpcode::TA_OPCODE_READ)
    {
        return BuildRemoteReadResponse(UINT32_MAX, request);
    }
    return nullptr;
}

void UbTransaction::SetRemoteUrmaRequestProcessor(UbRemoteUrmaRequestProcessor processor)
{
    m_remoteUrmaRequestProcessor = processor;
}

void UbTransaction::ClearRemoteUrmaRequestProcessor()
{
    m_remoteUrmaRequestProcessor = UbRemoteUrmaRequestProcessor();
}

bool UbTransaction::HasRemoteUrmaRequestProcessor() const
{
    return !m_remoteUrmaRequestProcessor.IsNull();
}

void UbTransaction::EnqueueInboundTaResponse(uint32_t localTpn,
                                             Ptr<UbWqeSegment> response)
{
    if (response == nullptr) {
        return;
    }

    response->SetTpn(localTpn);
    EnqueueRemoteRtpWqeSegment(localTpn, response);
}

void UbTransaction::SendBoundRemoteUrmaResponse(Ptr<UbTransaction> transaction,
                                                uint32_t localTpn,
                                                Ptr<UbWqeSegment> request)
{
    if (transaction != nullptr) {
        transaction->SendRemoteUrmaResponse(localTpn, request);
    }
}

void UbTransaction::SendRemoteUrmaResponse(uint32_t localTpn,
                                           Ptr<UbWqeSegment> request)
{
    if (request == nullptr) {
        return;
    }
    if (request->GetType() == TaOpcode::TA_OPCODE_WRITE) {
        EnqueueInboundTaResponse(localTpn, BuildRemoteWriteAck(localTpn, request));
        return;
    }
    if (request->GetType() == TaOpcode::TA_OPCODE_READ) {
        EnqueueInboundTaResponse(localTpn, BuildRemoteReadResponse(localTpn, request));
    }
}

Ptr<UbWqeSegment> UbTransaction::ProcessInboundTaRequest(Ptr<UbWqeSegment> request)
{
    if (request == nullptr) {
        return nullptr;
    }
    if (request->GetType() == TaOpcode::TA_OPCODE_WRITE) {
        return ExecuteRemoteWriteAndBuildAck(UINT32_MAX, request);
    }
    if (request->GetType() == TaOpcode::TA_OPCODE_READ) {
        return ExecuteRemoteReadAndBuildResponse(UINT32_MAX, request);
    }
    return nullptr;
}

bool UbTransaction::ProcessInboundTaResponse(Ptr<UbWqeSegment> response)
{
    if (response == nullptr) {
        return false;
    }

    const bool isWriteAck =
        response->GetType() == TaOpcode::TA_OPCODE_TRANSACTION_ACK &&
        response->GetRequestOpcode() == TaOpcode::TA_OPCODE_WRITE;
    const bool isReadResponse =
        response->GetType() == TaOpcode::TA_OPCODE_READ_RESPONSE &&
        response->GetRequestOpcode() == TaOpcode::TA_OPCODE_READ;
    if (!isWriteAck && !isReadResponse) {
        return false;
    }

    return ProcessWqeSegmentComplete(response);
}

Ptr<UbWqeSegment> UbTransaction::ExecuteRemoteWriteAndBuildAck(uint32_t localTpn,
                                                               Ptr<UbWqeSegment> request)
{
    if (request == nullptr) {
        return nullptr;
    }

    const uint64_t address = DeriveRemoteAddress(request);
    m_memoryStore[std::make_pair(m_nodeId, address)] = request->GetSize();

    return BuildRemoteWriteAck(localTpn, request);
}

Ptr<UbWqeSegment> UbTransaction::BuildRemoteWriteAck(uint32_t localTpn,
                                                     Ptr<UbWqeSegment> request) const
{
    if (request == nullptr) {
        return nullptr;
    }

    Ptr<UbWqeSegment> response = CreateObject<UbWqeSegment>();
    response->SetSrc(m_nodeId);
    response->SetDest(request->GetSrc());
    response->SetSport(request->GetDport());
    response->SetDport(request->GetSport());
    response->SetType(TaOpcode::TA_OPCODE_TRANSACTION_ACK);
    // Keep a non-zero carrier so the existing TP scheduler will serialize one response packet.
    response->SetSize(1);
    response->SetPriority(request->GetPriority());
    response->SetTaskId(request->GetTaskId());
    response->SetWqeSize(request->GetWqeSize());
    response->SetOrderType(request->GetOrderType());
    response->SetJettyNum(request->GetOriginJettyNum());
    response->SetTaMsn(0);
    response->SetTaSsn(request->GetRequestTassn());
    response->SetSegmentKind(UbTransactionSegmentKind::RESPONSE);
    response->SetOriginJettyNum(request->GetOriginJettyNum());
    response->SetRequestTassn(request->GetRequestTassn());
    response->SetRequestOpcode(TaOpcode::TA_OPCODE_WRITE);
    response->SetResponseBytes(0);
    response->SetRemoteAddress(request->GetRemoteAddress());
    response->SetRemoteTokenId(request->GetRemoteTokenId());
    response->SetNeedsTransactionResponse(false);
    response->SetTpn(localTpn);
    if (request->HasTransactionCorrelation())
    {
        response->SetTransactionCorrelation(request->GetTransactionCorrelation());
    }
    if (request->HasResolvedTpIdentity())
    {
        response->SetResolvedTpIdentity(request->GetResolvedTpIdentity());
    }
    return response;
}

Ptr<UbWqeSegment> UbTransaction::ExecuteRemoteReadAndBuildResponse(uint32_t localTpn,
                                                                   Ptr<UbWqeSegment> request)
{
    if (request == nullptr) {
        return nullptr;
    }

    return BuildRemoteReadResponse(localTpn, request);
}

Ptr<UbWqeSegment> UbTransaction::BuildRemoteReadResponse(uint32_t localTpn,
                                                          Ptr<UbWqeSegment> request) const
{
    if (request == nullptr) {
        return nullptr;
    }

    const uint32_t responseBytes = request->GetResponseBytes() == 0 ? request->GetSize()
                                                                     : request->GetResponseBytes();

    Ptr<UbWqeSegment> response = CreateObject<UbWqeSegment>();
    response->SetSrc(m_nodeId);
    response->SetDest(request->GetSrc());
    response->SetSport(request->GetDport());
    response->SetDport(request->GetSport());
    response->SetType(TaOpcode::TA_OPCODE_READ_RESPONSE);
    response->SetSize(responseBytes);
    response->SetPriority(request->GetPriority());
    response->SetTaskId(request->GetTaskId());
    response->SetWqeSize(request->GetWqeSize());
    response->SetOrderType(request->GetOrderType());
    response->SetJettyNum(request->GetOriginJettyNum());
    response->SetTaMsn(0);
    response->SetTaSsn(request->GetRequestTassn());
    response->SetSegmentKind(UbTransactionSegmentKind::RESPONSE);
    response->SetOriginJettyNum(request->GetOriginJettyNum());
    response->SetRequestTassn(request->GetRequestTassn());
    response->SetRequestOpcode(TaOpcode::TA_OPCODE_READ);
    response->SetResponseBytes(responseBytes);
    response->SetRemoteAddress(request->GetRemoteAddress());
    response->SetRemoteTokenId(request->GetRemoteTokenId());
    response->SetNeedsTransactionResponse(false);
    response->SetTpn(localTpn);
    if (request->HasTransactionCorrelation())
    {
        response->SetTransactionCorrelation(request->GetTransactionCorrelation());
    }
    if (request->HasResolvedTpIdentity())
    {
        response->SetResolvedTpIdentity(request->GetResolvedTpIdentity());
    }
    return response;
}

void UbTransaction::CompleteLocalRequestFromResponse(Ptr<UbWqeSegment> response)
{
    ProcessInboundTaResponse(response);
}

uint64_t UbTransaction::DeriveRemoteAddress(const Ptr<UbWqeSegment>& request) const
{
    NS_ASSERT_MSG(request != nullptr, "request must exist when deriving a remote address");
    return request->GetRemoteAddress() +
           static_cast<uint64_t>(request->GetTaSsn()) * UB_WQE_TA_SEGMENT_BYTE;
}

bool UbTransaction::IsOrderedByInitiator(uint32_t jettyNum, Ptr<UbWqe> wqe)
{
    if (m_serviceMode.find(jettyNum) == m_serviceMode.end()) {
        return false;
    }
    if (m_serviceMode[jettyNum] != TransactionServiceMode::ROI) { // 不是ROI，直接返回true
        return true;
    }
    bool res = false;
    bool orderedEmpty = m_orderedWqeIdsByJetty[jettyNum].empty();
    switch (wqe->GetOrderType()) {
        case OrderType::ORDER_NO:
        case OrderType::ORDER_RESERVED:
            res = true;
            break;
        case OrderType::ORDER_RELAX:
            NS_ASSERT_MSG(!orderedEmpty, "RO/SO Wqe should in Ordered vector!");
            res = true;
            break;
        case OrderType::ORDER_STRONG:
            NS_ASSERT_MSG(!orderedEmpty, "RO/SO Wqe should in Ordered vector!");
            res = (m_orderedWqeIdsByJetty[jettyNum].front() == wqe->GetWqeId());
            break;
        default:
            NS_ASSERT_MSG(0, "Invalid Transaction Order Type!");
    }
    return res;
}

void UbTransaction::SetTransactionServiceMode(uint32_t jettyNum, TransactionServiceMode mode)
{
    m_serviceMode[jettyNum] = mode;
    // ROI模式下且wqeVector中尚无该jetty的记录，则新建
    if (mode == TransactionServiceMode::ROI &&
        m_orderedWqeIdsByJetty.find(jettyNum) == m_orderedWqeIdsByJetty.end())
    {
        m_orderedWqeIdsByJetty[jettyNum] = std::vector<uint32_t>();
    }
}

TransactionServiceMode UbTransaction::GetTransactionServiceMode(uint32_t jettyNum)
{
    if (m_serviceMode.find(jettyNum) != m_serviceMode.end()) {
        return m_serviceMode[jettyNum];
    } else { //默认为ROI
        return TransactionServiceMode::ROI;
    }
}


bool UbTransaction::IsOrderedByTarget(Ptr<UbWqe> wqe)
{
    NS_LOG_DEBUG("IsOrderedByTarget");
    return true;
}

bool UbTransaction::IsReliable(Ptr<UbWqe> wqe)
{
    return true;
}

bool UbTransaction::IsUnreliable(Ptr<UbWqe> wqe)
{
    return false;
}

void UbTransaction::AddWqe(uint32_t jettyNum, Ptr<UbWqe> wqe)
{
    if (m_serviceMode.find(jettyNum) != m_serviceMode.end()) {
        // ROI模式且wqe是RO或SO
        if (m_serviceMode[jettyNum] == TransactionServiceMode::ROI
            && (wqe->GetOrderType() == OrderType::ORDER_RELAX || wqe->GetOrderType() == OrderType::ORDER_STRONG)) {
            m_orderedWqeIdsByJetty[jettyNum].push_back(wqe->GetWqeId());
        }
    } else {
        SetTransactionServiceMode(jettyNum, TransactionServiceMode::ROI);
        AddWqe(jettyNum, wqe);
    }
}

const UbRtpTransportReference*
UbTransaction::FindRtpTransportReference(uint32_t jettyNum) const
{
    const auto transportReference = m_jettyRtpReferences.find(jettyNum);
    if (transportReference == m_jettyRtpReferences.end())
    {
        return nullptr;
    }
    return &transportReference->second;
}

Ptr<UbController>
UbTransaction::GetOwnerController() const
{
    return m_ownerController == nullptr ? nullptr : Ptr<UbController>(m_ownerController);
}

bool
UbTransaction::ResolveRtpTransportReference(
    const UbRtpTransportReference& reference,
    RtpTransportResolution& resolution) const
{
    if (m_collectRtpResolutionCountersForTest)
    {
        ++m_rtpResolutionCount;
    }
    resolution = {};
    resolution.kind = reference.GetKind();
    resolution.identity = reference.GetIdentity();
    resolution.directTpn = reference.IsDirectTp() ? reference.GetDirectTpn() : UINT32_MAX;
    if (m_coordinator == nullptr ||
        !m_coordinator->IsResourceActive(reference.GetIdentity()))
    {
        return false;
    }
    if (reference.IsDirectTp())
    {
        Ptr<UbController> controller = GetOwnerController();
        Ptr<UbTransportChannel> transport =
            controller == nullptr
                ? nullptr
                : controller->GetTransportChannelManager()->ResolveLiveTp(reference.GetDirectTpn(),
                                                                          reference.GetIdentity());
        if (transport == nullptr)
        {
            return false;
        }
        resolution.directTp = transport;
        return true;
    }

    Ptr<UbController> controller = GetOwnerController();
    resolution.group =
        controller == nullptr ? nullptr : controller->ResolveRtpTpGroup(reference.GetIdentity());
    return resolution.group != nullptr;
}

void
UbTransaction::ResetRtpResolutionCountersForTest()
{
    m_collectRtpResolutionCountersForTest = true;
    m_rtpResolutionCount = 0;
    m_rtpTpgMaterializationCount = 0;
}

uint64_t
UbTransaction::GetRtpResolutionCountForTest() const
{
    return m_rtpResolutionCount;
}

uint64_t
UbTransaction::GetRtpTpgMaterializationCountForTest() const
{
    return m_rtpTpgMaterializationCount;
}

bool
UbTransaction::IsRtpTransportReferenceCurrent(
    const UbRtpTransportReference& reference) const
{
    RtpTransportResolution resolution;
    return ResolveRtpTransportReference(reference, resolution);
}

UbRtpOperationPreflightResult
UbTransaction::PreflightRtpOperation(const UbRtpTransportReference& reference) const
{
    RtpTransportResolution resolution;
    return PreflightRtpOperation(reference, resolution);
}

UbRtpOperationPreflightResult
UbTransaction::PreflightRtpOperation(const UbRtpTransportReference& reference,
                                     RtpTransportResolution& resolution) const
{
    if (!ResolveRtpTransportReference(reference, resolution))
    {
        return UbRtpOperationPreflightResult::STALE_REFERENCE;
    }
    if (reference.IsDirectTp())
    {
        return UbRtpOperationPreflightResult::ACCEPTING;
    }
    if (!resolution.group->IsAcceptingNewOperations())
    {
        return UbRtpOperationPreflightResult::NOT_ACCEPTING;
    }
    return resolution.group->GetMemberCount() == 0
               ? UbRtpOperationPreflightResult::NO_MEMBER
               : UbRtpOperationPreflightResult::ACCEPTING;
}

bool
UbTransaction::TryAdmitRtpWqe(uint32_t jettyNum,
                              Ptr<UbWqe> wqe,
                              const UbLifecycleResourceIdentity& wqeIdentity,
                              const RtpTransportResolution& resolution)
{
    const UbRtpTransportReference* reference = FindRtpTransportReference(jettyNum);
    const bool referenceMatches =
        reference != nullptr && reference->GetKind() == resolution.kind &&
        reference->GetIdentity() == resolution.identity &&
        (!reference->IsDirectTp() || reference->GetDirectTpn() == resolution.directTpn);
    const bool resolutionAccepting =
        referenceMatches &&
        (reference->IsDirectTp()
             ? resolution.directTp != nullptr &&
                   resolution.directTp->GetLifecycleOwner() == resolution.identity
             : resolution.group != nullptr && resolution.group->IsAcceptingNewOperations() &&
                   resolution.group->GetLifecycleIdentity() == resolution.identity &&
                   resolution.group->GetMemberCount() != 0);
    if (wqe == nullptr || !resolutionAccepting)
    {
        return false;
    }

    const std::pair<uint32_t, uint32_t> key{jettyNum, wqe->GetWqeId()};
    if (m_operations.find(key) != m_operations.end())
    {
        return false;
    }

    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    OperationRecord operation;
    operation.wqe = wqe;
    operation.wqeIdentity = wqeIdentity;
    operation.operationIdentity = coordinator->OpenResource(
        UbLifecycleResourceKind::TRANSACTION_OPERATION,
        wqeIdentity.localId);
    operation.transportReference = *reference;
    operation.directTpn = reference->IsDirectTp() ? reference->GetDirectTpn() : UINT32_MAX;
    operation.workLease = coordinator->AcquireLease(wqeIdentity,
                                                    UbLifecycleLeaseKind::WORK,
                                                    operation.operationIdentity);
    m_operations.emplace(key, operation);
    wqe->MarkLifecycleManaged();
    return true;
}

bool
UbTransaction::TryAdmitCtpWqe(uint32_t jettyNum,
                              Ptr<UbWqe> wqe,
                              const UbLifecycleResourceIdentity& wqeIdentity,
                              const UbCtpEntityKey& key)
{
    if (wqe == nullptr || m_operations.find({jettyNum, wqe->GetWqeId()}) != m_operations.end())
    {
        return false;
    }

    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    OperationRecord operation;
    operation.wqe = wqe;
    operation.wqeIdentity = wqeIdentity;
    operation.operationIdentity = coordinator->OpenResource(
        UbLifecycleResourceKind::TRANSACTION_OPERATION,
        wqeIdentity.localId);
    operation.workLease = coordinator->AcquireLease(wqeIdentity,
                                                    UbLifecycleLeaseKind::WORK,
                                                    operation.operationIdentity);
    operation.ctp = true;
    operation.ctpKey = key;
    m_operations.emplace(std::make_pair(jettyNum, wqe->GetWqeId()), operation);
    GetOrCreateCtpTransactionState(key);
    wqe->MarkLifecycleManaged();
    return true;
}

void
UbTransaction::NotifyRtpTpGroupWorkChanged(
    const UbRtpTransportReference& reference)
{
    if (reference.IsDirectTp())
    {
        return;
    }
    const UbLifecycleResourceIdentity groupIdentity = reference.GetIdentity();
    if (std::find(m_tpgsBlockingJettyRetry.begin(),
                  m_tpgsBlockingJettyRetry.end(),
                  groupIdentity) == m_tpgsBlockingJettyRetry.end())
    {
        Ptr<UbController> controller = GetOwnerController();
        if (controller != nullptr)
        {
            controller->NotifyRtpTpGroupWorkChanged(groupIdentity);
        }
    }
}

bool
UbTransaction::HasWqeOperation(uint32_t jettyNum, uint32_t wqeId) const
{
    return m_operations.find({jettyNum, wqeId}) != m_operations.end();
}

uint32_t
UbTransaction::GetResidentOperationCount() const
{
    return static_cast<uint32_t>(m_operations.size());
}

uint64_t
UbTransaction::GetResidentSegmentCount() const
{
    uint64_t count = 0;
    for (const auto& [operationId, operation] : m_operations)
    {
        (void)operationId;
        count += operation.segments.size();
    }
    return count;
}

uint32_t
UbTransaction::GetActiveOperationCount() const
{
    return GetResidentOperationCount();
}

uint32_t
UbTransaction::GetRtpTxQueuePendingEntryCountForTest(
    const UbLifecycleResourceIdentity& identity) const
{
    const RtpTxQueueKey key{identity.kind == UbLifecycleResourceKind::RTP_TPG
                                ? UbRtpTransportReferenceKind::TPG
                                : UbRtpTransportReferenceKind::DIRECT_TP,
                            identity};
    const auto queueIt = m_rtpTxQueues.find(key);
    return queueIt == m_rtpTxQueues.end()
               ? 0
               : static_cast<uint32_t>(queueIt->second.pendingTxEntries.size());
}

bool
UbTransaction::IsRtpTxQueueWaitingForTransportForTest(
    const UbLifecycleResourceIdentity& identity) const
{
    const RtpTxQueueKey key{identity.kind == UbLifecycleResourceKind::RTP_TPG
                                ? UbRtpTransportReferenceKind::TPG
                                : UbRtpTransportReferenceKind::DIRECT_TP,
                            identity};
    const auto queueIt = m_rtpTxQueues.find(key);
    return queueIt != m_rtpTxQueues.end() && queueIt->second.waitingForTransportCapacity;
}

bool
UbTransaction::IsRtpTxQueueEntryActiveForTest(const UbLifecycleResourceIdentity& identity) const
{
    const RtpTxQueueKey key{identity.kind == UbLifecycleResourceKind::RTP_TPG
                                ? UbRtpTransportReferenceKind::TPG
                                : UbRtpTransportReferenceKind::DIRECT_TP,
                            identity};
    const auto queueIt = m_rtpTxQueues.find(key);
    return queueIt != m_rtpTxQueues.end() && queueIt->second.txEntryActive;
}

bool
UbTransaction::IsRtpTxQueueEventPendingForTest(const UbLifecycleResourceIdentity& identity) const
{
    const RtpTxQueueKey key{identity.kind == UbLifecycleResourceKind::RTP_TPG
                                ? UbRtpTransportReferenceKind::TPG
                                : UbRtpTransportReferenceKind::DIRECT_TP,
                            identity};
    const auto queueIt = m_rtpTxQueues.find(key);
    return queueIt != m_rtpTxQueues.end() && queueIt->second.txEventPending;
}

uint64_t
UbTransaction::GetRtpTxQueueCapacityRecoveryCountForTest(
    const UbLifecycleResourceIdentity& identity) const
{
    const RtpTxQueueKey key{identity.kind == UbLifecycleResourceKind::RTP_TPG
                                ? UbRtpTransportReferenceKind::TPG
                                : UbRtpTransportReferenceKind::DIRECT_TP,
                            identity};
    const auto queueIt = m_rtpTxQueues.find(key);
    return queueIt == m_rtpTxQueues.end() ? 0 : queueIt->second.transportCapacityRecoveryEvents;
}

uint32_t
UbTransaction::GetRtpTxQueueCountForTest() const
{
    return static_cast<uint32_t>(m_rtpTxQueues.size());
}

bool
UbTransaction::TryGetOperationSnapshot(uint32_t jettyNum,
                                       uint32_t wqeId,
                                       UbTransactionOperationSnapshot& snapshot) const
{
    const auto it = m_operations.find({jettyNum, wqeId});
    if (it == m_operations.end())
    {
        return false;
    }
    snapshot.wqeIdentity = it->second.wqeIdentity;
    snapshot.operationIdentity = it->second.operationIdentity;
    snapshot.transportReference = it->second.transportReference;
    snapshot.directTpn = it->second.directTpn;
    snapshot.activeSegments = static_cast<uint32_t>(it->second.segments.size());
    snapshot.activeTransportWorkLeases = snapshot.activeSegments;
    snapshot.segmentIdentities.clear();
    snapshot.segmentIdentities.reserve(it->second.segments.size());
    snapshot.resolvedTpIdentities.clear();
    snapshot.resolvedTpIdentities.reserve(it->second.segments.size());
    for (const auto& [taSsn, segment] : it->second.segments)
    {
        (void)taSsn;
        snapshot.segmentIdentities.push_back(segment.identity);
        snapshot.resolvedTpIdentities.push_back(segment.resolvedTpIdentity);
    }
    return true;
}

uint32_t
UbTransaction::GetTpRoundRobinIndexForTest(uint32_t tpn) const
{
    const auto it = m_tpRRIndex.find(tpn);
    NS_ABORT_MSG_IF(it == m_tpRRIndex.end(),
                    "Transaction RR query references an unknown TP");
    return it->second;
}

void
UbTransaction::SetTpRoundRobinIndexForTest(uint32_t tpn, uint32_t index)
{
    const auto it = m_tpRRIndex.find(tpn);
    NS_ABORT_MSG_IF(it == m_tpRRIndex.end(), "Transaction RR update references an unknown TP");
    it->second = index;
}

void
UbTransaction::WqeFinish(uint32_t jettyNum,
                         Ptr<UbWqe> wqe,
                         UbWorkOutcome outcome)
{
    FinishWqe(jettyNum, wqe, outcome, WqeTerminalMode::CONTINUE_ACCEPTED);
}

void
UbTransaction::FinishWqe(uint32_t jettyNum,
                         Ptr<UbWqe> wqe,
                         UbWorkOutcome outcome,
                         WqeTerminalMode terminalMode)
{
    NS_ABORT_MSG_IF(wqe == nullptr, "WqeFinish requires a WQE");
    const auto operation = m_operations.find({jettyNum, wqe->GetWqeId()});
    NS_ABORT_MSG_IF(operation != m_operations.end() && operation->second.wqe != wqe,
                    "WqeFinish references a stale WQE instance");
    NS_ABORT_MSG_IF(operation == m_operations.end() && wqe->IsLifecycleManaged(),
                    "WqeFinish references an unknown, duplicate, or stale direct WQE");
    if (operation != m_operations.end() && operation->second.ctp)
    {
        const UbCtpEntityKey ctpKey = operation->second.ctpKey;
        const UbLifecycleResourceIdentity wqeIdentity = operation->second.wqeIdentity;
        auto transactionState = m_ctpTransactionStates.find(operation->second.ctpKey);
        if (transactionState != m_ctpTransactionStates.end())
        {
            auto& pendingSegments = transactionState->second.pendingSegments;
            std::erase_if(pendingSegments,
                          [jettyNum, wqe](const Ptr<UbWqeSegment>& segment) {
                              return segment != nullptr && segment->GetJettyNum() == jettyNum &&
                                     segment->GetTaskId() == wqe->GetWqeId();
                          });
        }
        std::vector<uint32_t> terminalTaSsns;
        terminalTaSsns.reserve(operation->second.segments.size());
        for (const auto& [taSsn, record] : operation->second.segments)
        {
            terminalTaSsns.push_back(taSsn);
            if (transactionState != m_ctpTransactionStates.end())
            {
                transactionState->second.outstandingSegments.erase(taSsn);
                transactionState->second.segmentOutstandingPackets.erase(record.segment);
                transactionState->second.context->MarkTaAck(taSsn);
            }
            m_coordinator->ReleaseLease(record.childLease);
            m_coordinator->RecordWorkTerminal(record.identity, outcome);
            m_coordinator->RetireResource(record.identity);
        }
        if (!terminalTaSsns.empty())
        {
            Ptr<UbJetty> jetty = GetJetty(jettyNum);
            NS_ABORT_MSG_IF(jetty == nullptr, "CTP Transaction terminal lost its Jetty owner");
            jetty->AdvanceTaSsnForWqeCleanup(terminalTaSsns);
        }
        m_coordinator->ReleaseLease(operation->second.workLease);
        m_coordinator->RecordWorkTerminal(operation->second.operationIdentity, outcome);
        m_coordinator->RetireResource(operation->second.operationIdentity);
        m_coordinator->RecordWorkTerminal(operation->second.wqeIdentity, outcome);
        m_coordinator->RetireResource(operation->second.wqeIdentity);
        m_operations.erase(operation);

        auto ordered = m_orderedWqeIdsByJetty.find(jettyNum);
        if (ordered != m_orderedWqeIdsByJetty.end())
        {
            auto wqeId = std::find(ordered->second.begin(),
                                   ordered->second.end(),
                                   wqe->GetWqeId());
            if (wqeId != ordered->second.end())
            {
                ordered->second.erase(wqeId);
            }
        }
        GetFunction()->ApplyWqeOutcomeFromTransaction(jettyNum,
                                                      wqe->GetWqeId(),
                                                      wqeIdentity,
                                                      outcome);
        if (outcome != UbWorkOutcome::COMPLETED)
        {
            TryRetireCtpTransactionState(ctpKey);
        }
        Ptr<UbController> controller = GetOwnerController();
        Ptr<UbCtpTransportService> ctpService =
            controller == nullptr ? nullptr : controller->PeekCtpTransportService();
        if (ctpService != nullptr)
        {
            ctpService->TryContinueJettyTransmission(ctpKey);
        }
        return;
    }
    bool hadLifecycleOperation = false;
    UbLifecycleResourceIdentity wqeIdentity;
    if (operation != m_operations.end())
    {
        NS_ABORT_MSG_IF(operation->second.awaitingTpSegmentCleanup,
                        "WqeFinish references an operation already awaiting transport cleanup");
        hadLifecycleOperation = true;
        DetachRtpTxEntry(operation->second);
        wqeIdentity = operation->second.wqeIdentity;
        Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
        const bool discardRetainedSegments = terminalMode == WqeTerminalMode::SIMULATION_STOP;
        std::vector<uint32_t> terminalTaSsns;
        terminalTaSsns.reserve(operation->second.segments.size());
        for (const auto& [taSsn, segment] : operation->second.segments)
        {
            (void)segment;
            terminalTaSsns.push_back(taSsn);
        }
        if (!terminalTaSsns.empty())
        {
            operation->second.awaitingTpSegmentCleanup = true;
            Ptr<UbJetty> jetty = GetJetty(jettyNum);
            NS_ABORT_MSG_IF(jetty == nullptr,
                            "Admitted Transaction segments require their Jetty owner");
            jetty->AdvanceTaSsnForWqeCleanup(terminalTaSsns);
        }
        std::vector<uint32_t> canceledTaSsns;
        for (auto segment = operation->second.segments.rbegin();
             segment != operation->second.segments.rend();
             ++segment)
        {
            const OperationRecord::SegmentRecord& record = segment->second;
            NS_ABORT_MSG_IF(record.segment == nullptr,
                            "Admitted Transaction record lost its segment instance");
            const auto transport = m_transportByTpn.find(record.segment->GetTpn());
            NS_ABORT_MSG_IF(transport == m_transportByTpn.end() || transport->second == nullptr ||
                                transport->second->GetLifecycleOwner() != record.resolvedTpIdentity,
                            "Admitted Transaction terminal lost its exact TP generation");
            const UbWqeSegmentTerminalDisposition disposition =
                transport->second->TryCancelUnsentWqeSegment(
                    record.segment,
                    {operation->second.operationIdentity, record.identity},
                    record.resolvedTpIdentity);
            if (disposition == UbWqeSegmentTerminalDisposition::RETAINED && discardRetainedSegments)
            {
                const UbWqeSegmentTerminalDisposition forcedDisposition =
                    transport->second->ForceRemoveRetainedWqeSegment(
                        record.segment,
                        {operation->second.operationIdentity, record.identity},
                        record.resolvedTpIdentity);
                NS_ABORT_MSG_IF(
                    forcedDisposition != UbWqeSegmentTerminalDisposition::FORCE_REMOVED &&
                        forcedDisposition !=
                            UbWqeSegmentTerminalDisposition::TRANSPORT_CLEANED,
                    "Forced WQE terminal did not resolve exact TP retained state");
            }
            if (disposition != UbWqeSegmentTerminalDisposition::RETAINED || discardRetainedSegments)
            {
                canceledTaSsns.push_back(segment->first);
            }
        }
        for (uint32_t taSsn : canceledTaSsns)
        {
            const auto segment = operation->second.segments.find(taSsn);
            NS_ABORT_MSG_IF(segment == operation->second.segments.end(),
                            "Canceled Transaction segment disappeared before release");
            coordinator->ReleaseLease(segment->second.childLease);
            coordinator->ReleaseLease(segment->second.transportWorkLease);
            coordinator->RecordWorkTerminal(segment->second.identity, outcome);
            coordinator->RetireResource(segment->second.identity);
            operation->second.segments.erase(segment);
        }
        if (!operation->second.segments.empty())
        {
            operation->second.awaitingTpSegmentCleanup = true;
            NotifyRtpTpGroupWorkChanged(operation->second.transportReference);
            for (const auto& [taSsn, segment] : operation->second.segments)
            {
                (void)taSsn;
                coordinator->RecordWorkTerminal(segment.identity, outcome);
            }
            coordinator->RecordWorkTerminal(operation->second.operationIdentity, outcome);
            coordinator->RecordWorkTerminal(operation->second.wqeIdentity, outcome);
        }
        else
        {
            const UbRtpTransportReference reference = operation->second.transportReference;
            coordinator->ReleaseLease(operation->second.workLease);
            coordinator->RecordWorkTerminal(operation->second.operationIdentity, outcome);
            coordinator->RetireResource(operation->second.operationIdentity);
            coordinator->RecordWorkTerminal(operation->second.wqeIdentity, outcome);
            coordinator->RetireResource(operation->second.wqeIdentity);
            m_operations.erase(operation);
            NotifyRtpTpGroupWorkChanged(reference);
        }
    }

    if (m_serviceMode.find(jettyNum) == m_serviceMode.end() || m_serviceMode[jettyNum] != TransactionServiceMode::ROI) {
        if (hadLifecycleOperation)
        {
            GetFunction()->ApplyWqeOutcomeFromTransaction(jettyNum,
                                                          wqe->GetWqeId(),
                                                          wqeIdentity,
                                                          outcome);
        }
        return;
    }
    // 从vector中寻找该wqe并删除
    std::vector<uint32_t>& orderedWqes = m_orderedWqeIdsByJetty[jettyNum];
    const bool terminalizedOrderedFront =
        hadLifecycleOperation && !orderedWqes.empty() &&
        orderedWqes.front() == wqe->GetWqeId();
    const bool wakeOrderedSuccessor =
        terminalizedOrderedFront && terminalMode == WqeTerminalMode::CONTINUE_ACCEPTED;
    auto it = std::find(orderedWqes.begin(), orderedWqes.end(), wqe->GetWqeId());
    if (it != orderedWqes.end()) {
        orderedWqes.erase(it);
    }

    Ptr<UbWqe> newlyEligibleStrongWqe;
    if (wakeOrderedSuccessor && !orderedWqes.empty())
    {
        const auto nextOperation = m_operations.find({jettyNum, orderedWqes.front()});
        if (nextOperation != m_operations.end() && nextOperation->second.wqe != nullptr &&
            nextOperation->second.wqe->GetOrderType() == OrderType::ORDER_STRONG &&
            nextOperation->second.segments.empty() &&
            !nextOperation->second.wqe->IsSentCompleted())
        {
            newlyEligibleStrongWqe = nextOperation->second.wqe;
        }
    }
    if (newlyEligibleStrongWqe != nullptr)
    {
        // This is an ordering transition, not a TP-capacity retry. The tracked exact-WQE event
        // runs after the current terminal callback and remains cancelable by Jetty drain.
        TriggerScheduleWqeSegment(jettyNum, newlyEligibleStrongWqe, true);
    }
    if (hadLifecycleOperation)
    {
        GetFunction()->ApplyWqeOutcomeFromTransaction(jettyNum,
                                                      wqe->GetWqeId(),
                                                      wqeIdentity,
                                                      outcome);
    }
}

void
UbTransaction::CompleteWqeOperation(uint32_t jettyNum,
                                    uint32_t wqeId,
                                    UbWorkOutcome outcome)
{
    CompleteWqeOperationForDrain(
        jettyNum,
        wqeId,
        outcome,
        WqeTerminalMode::CONTINUE_ACCEPTED);
}

void
UbTransaction::CompleteWqeOperationForDrain(uint32_t jettyNum,
                                            uint32_t wqeId,
                                            UbWorkOutcome outcome,
                                            WqeTerminalMode terminalMode)
{
    NS_ABORT_MSG_IF(outcome == UbWorkOutcome::COMPLETED,
                    "Successful WQE completion must come from Transaction response processing");
    const auto operation = m_operations.find({jettyNum, wqeId});
    NS_ABORT_MSG_IF(operation == m_operations.end(),
                    "Transaction outcome references an unknown, duplicate, or stale operation");
    Ptr<UbWqe> wqe = operation->second.wqe;
    FinishWqe(jettyNum, wqe, outcome, terminalMode);
}

void
UbTransaction::DiscardRetainedWqeSegmentsForSimulationStop(uint32_t jettyNum, uint32_t wqeId)
{
    const auto operation = m_operations.find({jettyNum, wqeId});
    if (operation == m_operations.end() || !operation->second.awaitingTpSegmentCleanup)
    {
        return;
    }
    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    for (const auto& [taSsn, segment] : operation->second.segments)
    {
        (void)taSsn;
        NS_ABORT_MSG_IF(segment.segment == nullptr,
                        "Simulation-stop cleanup lost the exact retained WQE Segment");
        const auto transport = m_transportByTpn.find(segment.segment->GetTpn());
        NS_ABORT_MSG_IF(transport == m_transportByTpn.end() || transport->second == nullptr ||
                            transport->second->GetLifecycleOwner() != segment.resolvedTpIdentity,
                        "Simulation-stop cleanup lost the exact TP generation");
        const UbWqeSegmentTerminalDisposition forcedDisposition =
            transport->second->ForceRemoveRetainedWqeSegment(
                segment.segment,
                {operation->second.operationIdentity, segment.identity},
                segment.resolvedTpIdentity);
        NS_ABORT_MSG_IF(
            forcedDisposition != UbWqeSegmentTerminalDisposition::FORCE_REMOVED &&
                forcedDisposition != UbWqeSegmentTerminalDisposition::TRANSPORT_CLEANED,
            "Simulation-stop cleanup did not resolve the exact TP-retained WQE Segment");
        coordinator->ReleaseLease(segment.childLease);
        coordinator->ReleaseLease(segment.transportWorkLease);
        coordinator->RetireResource(segment.identity);
    }
    operation->second.segments.clear();
    const UbRtpTransportReference reference = operation->second.transportReference;
    coordinator->ReleaseLease(operation->second.workLease);
    coordinator->RetireResource(operation->second.operationIdentity);
    coordinator->RetireResource(operation->second.wqeIdentity);
    m_operations.erase(operation);
    NotifyRtpTpGroupWorkChanged(reference);
}

void
UbTransaction::DiscardJettyRetainedWqeSegmentsForSimulationStop(uint32_t jettyNum)
{
    std::vector<uint32_t> wqeIdsWithRemainingSegments;
    for (auto operation = m_operations.lower_bound({jettyNum, 0});
         operation != m_operations.end() && operation->first.first == jettyNum;
         ++operation)
    {
        if (operation->second.awaitingTpSegmentCleanup)
        {
            wqeIdsWithRemainingSegments.push_back(operation->first.second);
        }
    }
    for (uint32_t wqeId : wqeIdsWithRemainingSegments)
    {
        DiscardRetainedWqeSegmentsForSimulationStop(jettyNum, wqeId);
    }
}

void
UbTransaction::DoDispose()
{
    NS_LOG_FUNCTION(this);
    for (auto& [key, queue] : m_rtpTxQueues)
    {
        (void)key;
        if (queue.txEventPending)
        {
            Simulator::Cancel(queue.pendingEvent);
        }
    }
    m_rtpTxQueues.clear();
    m_transportByTpn.clear();
    m_orderedWqeIdsByJetty.clear();
    m_jettyRtpReferences.clear();
    m_jettyTpgMaterializations.clear();
    for (auto& it : m_remoteRtpWqeSegmentsByTpn)
    {
        for (auto &remoteMap : it.second) {
            for (auto segment : remoteMap.second.segments) {
                segment = nullptr;
            }
        }
    }
    m_remoteRtpWqeSegmentsByTpn.clear();
    m_ctpTransactionStates.clear();
    m_nextCtpTaSsnByEntity.clear();
    m_tpRRIndex.clear();
    m_random = nullptr;
    m_serviceMode.clear();
    m_remoteUrmaRequestProcessor = UbRemoteUrmaRequestProcessor();
    m_memoryStore.clear();
    m_operations.clear();
    m_tpgsBlockingJettyRetry.clear();
    m_coordinator = nullptr;
    m_ownerController = nullptr;
    m_function = nullptr;
    for (auto& it : m_orderedWqeIdsByJetty)
    {
        it.second.clear();
    }
    m_orderedWqeIdsByJetty.clear();
    Object::DoDispose();
}

std::vector<uint32_t> UbTransaction::GetUselessTpns()
{
    std::vector<uint32_t> tpns;
    // 遍历tp，找出所有本地和对端都不再使用的tp
    for (auto it = m_transportByTpn.begin(); it != m_transportByTpn.end(); it++)
    {
        uint32_t tpn = it->first;
        if (!IsTpInUse(tpn) && !IsPeerTpInUse(tpn)) {
            tpns.push_back(tpn);
        }
    }
    return tpns;
}

bool UbTransaction::IsTpInUse(uint32_t tpn)
{
    NS_ASSERT_MSG(m_transportByTpn.find(tpn) != m_transportByTpn.end(), "no such tp.");
    Ptr<UbController> controller = GetOwnerController();
    for (const auto& [jettyNum, reference] : m_jettyRtpReferences)
    {
        (void)jettyNum;
        if (reference.IsDirectTp())
        {
            if (reference.GetDirectTpn() == tpn)
            {
                return true;
            }
            continue;
        }
        Ptr<UbTransportGroup> group = controller == nullptr
                                          ? nullptr
                                          : controller->ResolveRtpTpGroup(reference.GetIdentity());
        if (group == nullptr)
        {
            continue;
        }
        for (uint32_t index = 0; index < group->GetMemberCount(); ++index)
        {
            if (group->GetMember(index).tpn == tpn)
            {
                return true;
            }
        }
    }
    return false;
}

bool
UbTransaction::HasPendingTransportWork(uint32_t tpn) const
{
    const auto remoteRequests = m_remoteRtpWqeSegmentsByTpn.find(tpn);
    return remoteRequests != m_remoteRtpWqeSegmentsByTpn.end() && !remoteRequests->second.empty();
}

bool UbTransaction::IsPeerTpInUse(uint32_t tpn)
{
    auto localTpIt = m_transportByTpn.find(tpn);
    NS_ASSERT_MSG(localTpIt != m_transportByTpn.end(), "can't find local tpn");
    auto localTp = localTpIt->second;
    uint32_t dst = localTp->GetDest();
    uint32_t dstTpn = localTp->GetDstTpn();
    auto peerTa = NodeList::GetNode(dst)->GetObject<UbController>()->GetUbTransaction();
    return peerTa->IsTpInUse(dstTpn);
}

} // namespace ns3
