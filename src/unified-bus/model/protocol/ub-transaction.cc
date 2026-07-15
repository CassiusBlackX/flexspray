// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/log.h"
#include "ns3/simulator.h"

#include "ns3/ub-datatype.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-transaction.h"
#include "ns3/ub-utils.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbTransaction");

NS_OBJECT_ENSURE_REGISTERED(UbTransaction);

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
    m_tpnMap[tpn] = tp;
    m_tpRRIndex[tpn] = 0;
}

void UbTransaction::TpDeinit(uint32_t tpn)
{
    const auto transport = m_tpnMap.find(tpn);
    if (transport != m_tpnMap.end() && transport->second != nullptr)
    {
        RetireRtpAdmissionDomain(transport->second->GetLifecycleOwner());
    }
    m_tpnMap.erase(tpn);
    m_tpRRIndex.erase(tpn);
    m_tpRelatedRemoteRequests.erase(tpn);
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

    std::vector<Ptr<UbTransportChannel>> requestedTransports;
    requestedTransports.reserve(tpns.size());
    for (uint32_t tpn : tpns) {
        const auto transport = m_tpnMap.find(tpn);
        if (transport == m_tpnMap.end() || transport->second == nullptr) {
            return false;
        }
        requestedTransports.push_back(transport->second);
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
        Ptr<UbController> controller = GetOwnerController();
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
    return true;
}

void UbTransaction::DestroyJettyTpMap(uint32_t jettyNum)
{
    m_jettyRtpReferences.erase(jettyNum);
    m_jettyTpNextIndex.erase(jettyNum);
}

void
UbTransaction::ReleaseRtpTpGroupBindingLeases(
    const UbLifecycleResourceIdentity& groupIdentity)
{
    GetFunction()->ReleaseJettyRtpBindings(groupIdentity);
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

const std::vector<Ptr<UbTransportChannel>> UbTransaction::GetJettyRelatedTpVec(uint32_t jettyNum)
{
    NS_LOG_DEBUG(this);
    const auto reference = m_jettyRtpReferences.find(jettyNum);
    if (reference == m_jettyRtpReferences.end())
    {
        return {};
    }
    if (reference->second.IsDirectTp())
    {
        const auto transport = m_tpnMap.find(reference->second.GetDirectTpn());
        if (transport == m_tpnMap.end() || transport->second == nullptr ||
            transport->second->GetLifecycleOwner() != reference->second.GetIdentity())
        {
            return {};
        }
        return {transport->second};
    }

    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbTransportGroup> group =
        controller == nullptr ? nullptr
                              : controller->ResolveRtpTpGroup(reference->second.GetIdentity());
    if (group == nullptr)
    {
        return {};
    }
    std::vector<Ptr<UbTransportChannel>> transports;
    transports.reserve(group->GetMemberCount());
    for (uint32_t index = 0; index < group->GetMemberCount(); ++index)
    {
        const UbTransportGroupMember& member = group->GetMember(index);
        const auto transport = m_tpnMap.find(member.tpn);
        if (transport != m_tpnMap.end() && transport->second != nullptr &&
            transport->second->GetLifecycleOwner() == member.tpIdentity)
        {
            transports.push_back(transport->second);
        }
    }
    return transports;
}

std::vector<Ptr<UbJetty>> UbTransaction::GetTpRelatedJettyVec(uint32_t tpn)
{
    NS_LOG_DEBUG(this);
    std::vector<Ptr<UbJetty>> jetties;
    for (const auto& [jettyNum, reference] : m_jettyRtpReferences)
    {
        (void)reference;
        const std::vector<Ptr<UbTransportChannel>> transports =
            GetJettyRelatedTpVec(jettyNum);
        const bool related = std::any_of(
            transports.begin(),
            transports.end(),
            [tpn](const Ptr<UbTransportChannel>& transport) {
                return transport != nullptr && transport->GetTpn() == tpn;
            });
        Ptr<UbJetty> jetty = related ? GetJetty(jettyNum) : nullptr;
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
    EnqueueRtpSource(jettyNum, originatingWqe, false);
}

void
UbTransaction::RunTrackedPrepareRtpWqeSegment(uint32_t jettyNum,
                                              Ptr<UbWqe> originatingWqe,
                                              AdmissionDomainKey domainKey,
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
                domainKey,
                finishToken);
            GetFunction()->AttachJettyEvent(
                jettyNum,
                finishToken,
                finishEvent,
                MakeCallback(&UbTransaction::CancelRtpScheduleChain, this)
                    .Bind(domainKey, jettyNum, originatingWqe));
        }
        else
        {
            ClearRtpScheduleChain(jettyNum, originatingWqe);
            CompleteAdmissionDomainAttempt(jettyNum, originatingWqe);
            const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
            if (operation != m_operations.end() && operation->second.wqe == originatingWqe)
            {
                ScheduleAdmissionDomain(
                    MakeAdmissionDomainKey(operation->second.transportReference));
            }
        }
    }
    else
    {
        CancelRtpScheduleChain(domainKey, jettyNum, originatingWqe);
    }
    GetFunction()->ResolveJettyEvent(jettyNum, eventToken);
}

void
UbTransaction::RunTrackedAdmitRtpWqeSegment(Ptr<UbWqeSegment> segment,
                                            Ptr<UbWqe> originatingWqe,
                                            AdmissionDomainKey domainKey,
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
        ClearRtpScheduleChain(jettyNum, originatingWqe);
        CompleteAdmissionDomainAttempt(jettyNum, originatingWqe);
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
                    ScheduleAdmissionDomain(
                        MakeAdmissionDomainKey(operation->second.transportReference));
                }
            }
        }
        else if (result == RtpSegmentAdmissionResult::STALE_PREPARED_CANDIDATE)
        {
            TriggerScheduleWqeSegment(jettyNum, originatingWqe, true);
        }
        else if (result == RtpSegmentAdmissionResult::DEFERRED)
        {
            DeferRtpSource(jettyNum, originatingWqe);
        }
        else
        {
            const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
            if (operation != m_operations.end())
            {
                ScheduleAdmissionDomain(
                    MakeAdmissionDomainKey(operation->second.transportReference));
            }
        }
    }
    else
    {
        CancelRtpScheduleChain(domainKey, jettyNum, originatingWqe);
    }
    GetFunction()->ResolveJettyEvent(jettyNum, eventToken);
}

void UbTransaction::ApplyScheduleWqeSegment(Ptr<UbTransportChannel> tp)
{
    WakeDeferredRtpSources(tp);
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
    auto [it, inserted] = m_ctpTransactionStates.try_emplace(key);
    if (inserted)
    {
        const auto cursor = m_ctpNextTaSsn.find(key);
        const uint32_t initialSequence =
            cursor == m_ctpNextTaSsn.end() ? 0 : cursor->second;
        it->second.context = Create<UbCtpTransactionContext>(
            key,
            UB_JETTY_TASSN_OOO_THRESHOLD,
            initialSequence);
    }
    return it->second;
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
        !transactionState.inboundTaUnits.empty() || transactionState.pendingCompletionEvents != 0 ||
        transactionState.context == nullptr ||
        transactionState.context->GetOutstandingCount() != 0)
    {
        return false;
    }
    m_ctpNextTaSsn[key] = transactionState.context->GetSendNext();
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
UbTransaction::ClearRtpScheduleChain(uint32_t jettyNum, Ptr<UbWqe> originatingWqe)
{
    if (originatingWqe == nullptr)
    {
        return;
    }
    const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation != m_operations.end() && operation->second.wqe == originatingWqe)
    {
        operation->second.rtpScheduleChainActive = false;
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
           !operation->second.terminalPending;
}

bool
UbTransaction::HasLifecycleOperationForJetty(uint32_t jettyNum) const
{
    const auto first = m_operations.lower_bound({jettyNum, 0});
    return first != m_operations.end() && first->first.first == jettyNum;
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
    if (!ResolveRtpTransportReference(reference, resolution))
    {
        return RtpSegmentAdmissionResult::REJECTED;
    }
    if (reference.IsDirectTp())
    {
        if (!resolution.directTp->CanAdmitWqeSegment(segment))
        {
            return RtpSegmentAdmissionResult::DEFERRED;
        }
        admittedTp = resolution.directTp;
    }
    else
    {
        selectedGroup = resolution.group;
        bool hasLiveMember = false;
        for (uint32_t offset = 0; offset < selectedGroup->GetMemberCount(); ++offset)
        {
            const uint32_t candidateIndex = selectedGroup->GetMemberIndexAtOffset(offset);
            if (!selectedGroup->IsMemberSelectable(candidateIndex))
            {
                continue;
            }
            const UbTransportGroupMember& member = selectedGroup->GetMember(candidateIndex);
            const auto transport = m_tpnMap.find(member.tpn);
            if (transport == m_tpnMap.end() || transport->second == nullptr ||
                transport->second->GetLifecycleOwner() != member.tpIdentity ||
                !m_coordinator->IsResourceActive(member.tpIdentity))
            {
                continue;
            }
            hasLiveMember = true;
            if (transport->second->CanAdmitWqeSegment(segment))
            {
                selectedGroupIndex = candidateIndex;
                admittedTp = transport->second;
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

    NS_ABORT_MSG_IF(!admittedTp->TryAdmitWqeSegment(segment),
                    "RTP TP admission changed after owner-local preflight");
    NS_ABORT_MSG_IF(segment->GetResolvedTpIdentity() != record.resolvedTpIdentity,
                    "Admitted segment did not pin the resolved TP generation");
    jetty->CommitWqeSegmentAdmission(segment);
    if (selectedGroup != nullptr)
    {
        selectedGroup->CommitSelection(selectedGroupIndex);
        if (originatingWqe->GetBytesLeft() == 0)
        {
            ReleaseTransportUseLease(operation->second);
        }
    }
    return RtpSegmentAdmissionResult::ADMITTED;
}

UbTransaction::AdmissionDomainKey
UbTransaction::MakeAdmissionDomainKey(const UbRtpTransportReference& reference)
{
    return {reference.GetKind(), reference.GetIdentity()};
}

void
UbTransaction::StartRtpScheduleChain(uint32_t jettyNum, Ptr<UbWqe> originatingWqe)
{
    const auto operation = originatingWqe == nullptr
                               ? m_operations.end()
                               : m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation == m_operations.end() || operation->second.wqe != originatingWqe ||
        operation->second.terminalPending || operation->second.rtpScheduleChainActive)
    {
        return;
    }
    operation->second.rtpScheduleChainActive = true;
    const AdmissionDomainKey domainKey =
        MakeAdmissionDomainKey(operation->second.transportReference);
    const uint64_t eventToken = GetFunction()->TrackJettyEvent(jettyNum);
    const EventId event = Simulator::ScheduleNow(
        &UbTransaction::RunTrackedPrepareRtpWqeSegment,
        this,
        jettyNum,
        originatingWqe,
        domainKey,
        eventToken);
    GetFunction()->AttachJettyEvent(
        jettyNum,
        eventToken,
        event,
        MakeCallback(&UbTransaction::CancelRtpScheduleChain, this)
            .Bind(domainKey, jettyNum, originatingWqe));
}

void
UbTransaction::ScheduleAdmissionDomain(const AdmissionDomainKey& domainKey)
{
    const auto domain = m_admissionDomains.find(domainKey);
    if (domain == m_admissionDomains.end() || domain->second.blocked ||
        domain->second.readySources.empty() || domain->second.eventPending ||
        domain->second.sourceActive)
    {
        return;
    }
    domain->second.eventPending = true;
    domain->second.pendingEvent =
        Simulator::ScheduleNow(&UbTransaction::RunAdmissionDomainWake, this, domainKey);
}

void
UbTransaction::EnqueueRtpSource(uint32_t jettyNum,
                                Ptr<UbWqe> originatingWqe,
                                bool restoreBlockedFront)
{
    if (originatingWqe == nullptr)
    {
        return;
    }
    const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation == m_operations.end() || operation->second.wqe != originatingWqe ||
        operation->second.terminalPending || operation->second.rtpScheduleChainActive)
    {
        return;
    }

    const AdmissionDomainKey key = MakeAdmissionDomainKey(operation->second.transportReference);
    AdmissionDomainState& domain =
        m_admissionDomains[key];
    if (!operation->second.readyQueued)
    {
        const ReadySourceKey source{
            ReadySourceKey::Kind::LOCAL_OPERATION,
            jettyNum,
            originatingWqe->GetWqeId(),
            operation->second.operationIdentity};
        if (restoreBlockedFront)
        {
            domain.readySources.push_front(source);
        }
        else
        {
            domain.readySources.push_back(source);
        }
        operation->second.readyQueued = true;
    }
    if (!restoreBlockedFront && !domain.sourceActive && !domain.eventPending && !domain.blocked)
    {
        RunAdmissionDomainWake(key);
    }
}

void
UbTransaction::EnqueueRemoteRtpSource(uint32_t tpn, Ptr<UbWqeSegment> segment)
{
    const auto transport = m_tpnMap.find(tpn);
    if (segment == nullptr || transport == m_tpnMap.end() || transport->second == nullptr)
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
        operationIdentity = {
            segment->GetSrc(),
            UbLifecycleResourceKind::TRANSACTION_OPERATION,
            (static_cast<uint64_t>(segment->GetOriginJettyNum()) << 32) |
                segment->GetTaskId(),
            0};
    }
    const RemoteSourceKey remoteKey{
        segment->GetOriginJettyNum(), segment->GetTaskId(), operationIdentity};
    RemoteSourceState& remote = m_tpRelatedRemoteRequests[tpn][remoteKey];
    remote.segments.push_back(segment);

    const AdmissionDomainKey domainKey{
        UbRtpTransportReferenceKind::DIRECT_TP,
        transport->second->GetLifecycleOwner()};
    AdmissionDomainState& domain = m_admissionDomains[domainKey];
    if (!remote.readyQueued)
    {
        domain.readySources.push_back({ReadySourceKey::Kind::REMOTE_ORIGIN,
                                       remoteKey.originJettyNum,
                                       remoteKey.taskId,
                                       remoteKey.operationIdentity});
        remote.readyQueued = true;
    }
    if (!domain.sourceActive && !domain.eventPending && !domain.blocked)
    {
        RunAdmissionDomainWake(domainKey);
    }
}

void
UbTransaction::RunRemoteRtpSource(const AdmissionDomainKey& domainKey,
                                  const ReadySourceKey& source)
{
    const uint32_t tpn = static_cast<uint32_t>(domainKey.identity.localId);
    const auto transport = m_tpnMap.find(tpn);
    auto remoteByTp = m_tpRelatedRemoteRequests.find(tpn);
    const RemoteSourceKey remoteKey{source.jettyNum,
                                    source.wqeId,
                                    source.operationIdentity};
    if (transport == m_tpnMap.end() || transport->second == nullptr ||
        transport->second->GetLifecycleOwner() != domainKey.identity ||
        remoteByTp == m_tpRelatedRemoteRequests.end())
    {
        return;
    }
    auto remote = remoteByTp->second.find(remoteKey);
    if (remote == remoteByTp->second.end())
    {
        return;
    }
    remote->second.readyQueued = false;
    while (!remote->second.segments.empty() && remote->second.segments.front() == nullptr)
    {
        remote->second.segments.pop_front();
    }
    if (remote->second.segments.empty())
    {
        remoteByTp->second.erase(remote);
        if (remoteByTp->second.empty())
        {
            m_tpRelatedRemoteRequests.erase(remoteByTp);
        }
        return;
    }

    Ptr<UbWqeSegment> segment = remote->second.segments.front();
    if (!transport->second->TryAdmitWqeSegment(segment))
    {
        AdmissionDomainState& domain = m_admissionDomains[domainKey];
        domain.readySources.push_front(source);
        remote->second.readyQueued = true;
        domain.blocked = true;
        return;
    }

    remote->second.segments.pop_front();
    if (!remote->second.segments.empty())
    {
        m_admissionDomains[domainKey].readySources.push_back(source);
        remote->second.readyQueued = true;
    }
    else
    {
        remoteByTp->second.erase(remote);
        if (remoteByTp->second.empty())
        {
            m_tpRelatedRemoteRequests.erase(remoteByTp);
        }
    }
    NS_LOG_INFO("Remote WQE Segment Sends, taskId:" << segment->GetTaskId()
                                                     << "TASSN: " << segment->GetTaSsn());
    transport->second->WqeSegmentTriggerPortTransmit(segment);
    ScheduleAdmissionDomain(domainKey);
}

void
UbTransaction::DeferRtpSource(uint32_t jettyNum, Ptr<UbWqe> originatingWqe)
{
    EnqueueRtpSource(jettyNum, originatingWqe, true);
    if (originatingWqe == nullptr)
    {
        return;
    }
    const auto operation = m_operations.find({jettyNum, originatingWqe->GetWqeId()});
    if (operation != m_operations.end())
    {
        m_admissionDomains[MakeAdmissionDomainKey(operation->second.transportReference)].blocked =
            true;
    }
}

void
UbTransaction::WakeAdmissionDomain(const AdmissionDomainKey& key, bool capacityTransition)
{
    const auto domain = m_admissionDomains.find(key);
    if (domain == m_admissionDomains.end() || !domain->second.blocked ||
        domain->second.readySources.empty() || domain->second.eventPending)
    {
        return;
    }
    domain->second.blocked = false;
    ScheduleAdmissionDomain(key);
    if (capacityTransition && domain->second.eventPending)
    {
        ++domain->second.capacityWakeEvents;
    }
}

void
UbTransaction::WakeDeferredRtpSources(Ptr<UbTransportChannel> tp)
{
    if (tp == nullptr || tp->IsWqeSegmentLimited() || !tp->CanScheduleAnotherSegment())
    {
        return;
    }

    WakeAdmissionDomain({
        UbRtpTransportReferenceKind::DIRECT_TP,
        tp->GetLifecycleOwner()},
        true);

    Ptr<UbController> controller = GetOwnerController();
    Ptr<UbTransportGroup> group =
        controller == nullptr
            ? nullptr
            : controller->ResolveRtpTpGroupForMember(tp->GetLifecycleOwner());
    if (group != nullptr)
    {
        WakeAdmissionDomain({UbRtpTransportReferenceKind::TPG,
                             group->GetLifecycleIdentity()},
                            true);
    }
}

void
UbTransaction::NotifyRtpTpGroupMemberAvailable(
    const UbLifecycleResourceIdentity& groupIdentity)
{
    WakeAdmissionDomain({UbRtpTransportReferenceKind::TPG, groupIdentity}, false);
}

void
UbTransaction::TerminateRtpTpGroupOperations(
    const UbLifecycleResourceIdentity& groupIdentity,
    UbWorkOutcome outcome,
    bool forceDrain)
{
    NS_ABORT_MSG_IF(outcome == UbWorkOutcome::COMPLETED,
                    "TPG cleanup requires a terminal failure outcome");
    struct OwnerProgressSuppressionGuard
    {
        OwnerProgressSuppressionGuard(
            std::vector<UbLifecycleResourceIdentity>& suppressed,
            const UbLifecycleResourceIdentity& identity)
            : state(suppressed), identity(identity)
        {
            state.push_back(identity);
        }

        ~OwnerProgressSuppressionGuard()
        {
            NS_ABORT_MSG_IF(state.empty() || state.back() != identity,
                            "TPG cleanup owner progress suppression is unbalanced");
            state.pop_back();
        }

        std::vector<UbLifecycleResourceIdentity>& state;
        UbLifecycleResourceIdentity identity;
    } suppression(m_suppressedTpgOwnerProgress, groupIdentity);

    std::vector<std::pair<uint32_t, uint32_t>> targets;
    for (const auto& [key, operation] : m_operations)
    {
        if (!operation.transportReference.IsDirectTp() &&
            operation.transportReference.GetIdentity() == groupIdentity &&
            (forceDrain || !operation.terminalPending))
        {
            targets.push_back(key);
        }
    }
    for (const auto& [jettyNum, wqeId] : targets)
    {
        const auto operation = m_operations.find({jettyNum, wqeId});
        if (operation == m_operations.end() ||
            operation->second.transportReference.IsDirectTp() ||
            operation->second.transportReference.GetIdentity() != groupIdentity)
        {
            continue;
        }
        if (!operation->second.terminalPending)
        {
            CompleteWqeOperation(jettyNum, wqeId, outcome);
        }
        if (forceDrain)
        {
            ForceDrainTerminalPendingOperation(jettyNum, wqeId);
        }
    }
}

void
UbTransaction::RunAdmissionDomainWake(AdmissionDomainKey domainKey)
{
    const auto domain = m_admissionDomains.find(domainKey);
    if (domain == m_admissionDomains.end())
    {
        return;
    }
    domain->second.eventPending = false;
    domain->second.pendingEvent = EventId();
    if (domain->second.sourceActive || domain->second.blocked)
    {
        return;
    }

    while (!domain->second.readySources.empty())
    {
        const ReadySourceKey source = domain->second.readySources.front();
        domain->second.readySources.pop_front();
        if (source.kind == ReadySourceKey::Kind::REMOTE_ORIGIN)
        {
            RunRemoteRtpSource(domainKey, source);
            break;
        }
        const auto operation = m_operations.find({source.jettyNum, source.wqeId});
        if (operation == m_operations.end() ||
            operation->second.operationIdentity != source.operationIdentity)
        {
            continue;
        }
        operation->second.readyQueued = false;
        if (!operation->second.terminalPending)
        {
            domain->second.sourceActive = true;
            domain->second.activeSource = source;
            StartRtpScheduleChain(source.jettyNum, operation->second.wqe);
        }
        break;
    }
}

void
UbTransaction::CompleteAdmissionDomainAttempt(uint32_t jettyNum, Ptr<UbWqe> originatingWqe)
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
    const auto domain =
        m_admissionDomains.find(MakeAdmissionDomainKey(operation->second.transportReference));
    if (domain != m_admissionDomains.end() && domain->second.activeSource.has_value() &&
        domain->second.activeSource->kind == ReadySourceKey::Kind::LOCAL_OPERATION &&
        domain->second.activeSource->jettyNum == jettyNum &&
        domain->second.activeSource->wqeId == originatingWqe->GetWqeId() &&
        domain->second.activeSource->operationIdentity == operation->second.operationIdentity)
    {
        domain->second.sourceActive = false;
        domain->second.activeSource.reset();
    }
}

void
UbTransaction::CancelRtpScheduleChain(AdmissionDomainKey domainKey,
                                      uint32_t jettyNum,
                                      Ptr<UbWqe> originatingWqe)
{
    ClearRtpScheduleChain(jettyNum, originatingWqe);
    const auto domain = m_admissionDomains.find(domainKey);
    if (domain != m_admissionDomains.end())
    {
        domain->second.sourceActive = false;
        domain->second.activeSource.reset();
        ScheduleAdmissionDomain(domainKey);
    }
}

void
UbTransaction::DetachRtpSource(OperationRecord& operation)
{
    const AdmissionDomainKey key = MakeAdmissionDomainKey(operation.transportReference);
    const auto domain = m_admissionDomains.find(key);
    operation.readyQueued = false;
    operation.rtpScheduleChainActive = false;
    if (domain == m_admissionDomains.end())
    {
        return;
    }
    if (domain->second.activeSource.has_value())
    {
        const ReadySourceKey& active = *domain->second.activeSource;
        if (active.kind == ReadySourceKey::Kind::LOCAL_OPERATION &&
            active.operationIdentity == operation.operationIdentity)
        {
            domain->second.sourceActive = false;
            domain->second.activeSource.reset();
        }
    }
    domain->second.blocked = false;
    ScheduleAdmissionDomain(key);
}

void
UbTransaction::RetireRtpAdmissionDomain(
    const UbLifecycleResourceIdentity& identity)
{
    const UbRtpTransportReferenceKind kind =
        identity.kind == UbLifecycleResourceKind::RTP_TPG
            ? UbRtpTransportReferenceKind::TPG
            : UbRtpTransportReferenceKind::DIRECT_TP;
    const AdmissionDomainKey key{kind, identity};
    const auto domain = m_admissionDomains.find(key);
    if (domain == m_admissionDomains.end())
    {
        return;
    }
    if (domain->second.eventPending)
    {
        Simulator::Cancel(domain->second.pendingEvent);
    }
    for (const ReadySourceKey& source : domain->second.readySources)
    {
        if (source.kind == ReadySourceKey::Kind::LOCAL_OPERATION)
        {
            const auto operation = m_operations.find({source.jettyNum, source.wqeId});
            if (operation != m_operations.end() &&
                operation->second.operationIdentity == source.operationIdentity)
            {
                operation->second.readyQueued = false;
            }
        }
        else if (kind == UbRtpTransportReferenceKind::DIRECT_TP)
        {
            const uint32_t tpn = static_cast<uint32_t>(identity.localId);
            const auto remoteByTp = m_tpRelatedRemoteRequests.find(tpn);
            if (remoteByTp != m_tpRelatedRemoteRequests.end())
            {
                const RemoteSourceKey remoteKey{source.jettyNum,
                                                source.wqeId,
                                                source.operationIdentity};
                const auto remote = remoteByTp->second.find(remoteKey);
                if (remote != remoteByTp->second.end())
                {
                    remote->second.readyQueued = false;
                }
            }
        }
    }
    if (domain->second.sourceActive)
    {
        NS_ABORT_MSG_IF(!domain->second.activeSource.has_value() ||
                            domain->second.activeSource->kind !=
                                ReadySourceKey::Kind::LOCAL_OPERATION,
                        "Cannot retire an RTP admission domain with unknown active source");
        const ReadySourceKey& active = *domain->second.activeSource;
        const auto operation = m_operations.find({active.jettyNum, active.wqeId});
        NS_ABORT_MSG_IF(operation != m_operations.end() &&
                            (operation->second.operationIdentity != active.operationIdentity ||
                             operation->second.wqe == nullptr ||
                             operation->second.wqe->GetBytesLeft() != 0),
                        "Cannot retire an RTP admission domain before final assignment");
        if (operation != m_operations.end())
        {
            operation->second.readyQueued = false;
        }
        domain->second.sourceActive = false;
        domain->second.activeSource.reset();
    }
    m_admissionDomains.erase(domain);
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

    const bool terminalCleanup = operation->second.terminalPending;
    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    coordinator->ReleaseLease(segment->second.childLease);
    coordinator->ReleaseLease(segment->second.transportWorkLease);
    if (!terminalCleanup)
    {
        coordinator->RecordWorkTerminal(segment->second.identity, UbWorkOutcome::COMPLETED);
    }
    coordinator->RetireResource(segment->second.identity);
    operation->second.segments.erase(segment);
    if (terminalCleanup && operation->second.segments.empty())
    {
        coordinator->ReleaseLease(operation->second.workLease);
        ReleaseTransportUseLease(operation->second);
        coordinator->RetireResource(operation->second.operationIdentity);
        coordinator->RetireResource(operation->second.wqeIdentity);
        m_operations.erase(operation);
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
    const bool terminalCleanup = operation != m_operations.end() &&
                                 operation->second.terminalPending;
    if (!RecordSegmentSuccess(jettyNum, wqeSegment->GetTaskId(), taSsn, wqeSegment))
    {
        return false;
    }
    if (terminalCleanup)
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

    Ptr<UbWqeSegment> response = ProcessInboundTaRequest(segment);

    if (response == nullptr) {
        return;
    }

    response->SetTpn(localTpn);
    EnqueueRemoteRtpSource(localTpn, response);
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
    response->SetRemoteAddress(address);
    response->SetNeedsTransactionResponse(false);
    response->SetTpn(localTpn);
    if (request->HasTransactionCorrelation())
    {
        response->SetTransactionCorrelation(request->GetTransactionCorrelation());
    }
    return response;
}

Ptr<UbWqeSegment> UbTransaction::ExecuteRemoteReadAndBuildResponse(uint32_t localTpn,
                                                                   Ptr<UbWqeSegment> request)
{
    if (request == nullptr) {
        return nullptr;
    }

    const uint64_t address = DeriveRemoteAddress(request);
    const auto memoryIt = m_memoryStore.find(std::make_pair(m_nodeId, address));
    const uint32_t responseBytes = request->GetResponseBytes() == 0 ? request->GetSize()
                                                                     : request->GetResponseBytes();
    (void)memoryIt;

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
    response->SetRemoteAddress(address);
    response->SetNeedsTransactionResponse(false);
    response->SetTpn(localTpn);
    if (request->HasTransactionCorrelation())
    {
        response->SetTransactionCorrelation(request->GetTransactionCorrelation());
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

void UbTransaction::TriggerTpTransmit(uint32_t jettyNum)
{
    if (!m_suppressedTpgOwnerProgress.empty())
    {
        const UbRtpTransportReference* reference = FindRtpTransportReference(jettyNum);
        if (reference != nullptr && !reference->IsDirectTp() &&
            std::find(m_suppressedTpgOwnerProgress.begin(),
                      m_suppressedTpgOwnerProgress.end(),
                      reference->GetIdentity()) != m_suppressedTpgOwnerProgress.end())
        {
            return;
        }
    }
    const std::vector<Ptr<UbTransportChannel>> ubTransportGroupVec = GetJettyRelatedTpVec(jettyNum);
    for (uint32_t i = 0; i < ubTransportGroupVec.size(); i++) {
        uint32_t& nextIndex = m_jettyTpNextIndex[jettyNum];
        if (nextIndex >= ubTransportGroupVec.size()) {
            nextIndex = 0;
        }
        uint32_t idx = (nextIndex + i) % ubTransportGroupVec.size();
        ubTransportGroupVec[idx]->ApplyNextWqeSegment();
    }
    if (!ubTransportGroupVec.empty()) {
        m_jettyTpNextIndex[jettyNum] = (m_jettyTpNextIndex[jettyNum] + 1) %
                                      ubTransportGroupVec.size();
    }
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
    bool orderedEmpty = m_jettyOrderedWqe[jettyNum].empty();
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
            res = (m_jettyOrderedWqe[jettyNum].front() == wqe->GetWqeId());
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
    if (mode == TransactionServiceMode::ROI && m_jettyOrderedWqe.find(jettyNum) == m_jettyOrderedWqe.end()) {
        m_jettyOrderedWqe[jettyNum] = std::vector<uint32_t>();
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
            m_jettyOrderedWqe[jettyNum].push_back(wqe->GetWqeId());
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
    Ptr<Node> node = NodeList::GetNode(m_nodeId);
    return node == nullptr ? nullptr : node->GetObject<UbController>();
}

bool
UbTransaction::ResolveRtpTransportReference(
    const UbRtpTransportReference& reference,
    RtpTransportResolution& resolution) const
{
    resolution = {};
    if (m_coordinator == nullptr ||
        !m_coordinator->IsResourceActive(reference.GetIdentity()))
    {
        return false;
    }
    if (reference.IsDirectTp())
    {
        const auto transport = m_tpnMap.find(reference.GetDirectTpn());
        if (transport == m_tpnMap.end() || transport->second == nullptr ||
            transport->second->GetLifecycleOwner() != reference.GetIdentity())
        {
            return false;
        }
        resolution.directTp = transport->second;
        return true;
    }

    Ptr<UbController> controller = GetOwnerController();
    resolution.group =
        controller == nullptr ? nullptr : controller->ResolveRtpTpGroup(reference.GetIdentity());
    return resolution.group != nullptr;
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
                              const UbLifecycleResourceIdentity& wqeIdentity)
{
    const UbRtpTransportReference* reference = FindRtpTransportReference(jettyNum);
    RtpTransportResolution resolution;
    if (wqe == nullptr || reference == nullptr ||
        !ResolveRtpTransportReference(*reference, resolution) ||
        PreflightRtpOperation(*reference) != UbRtpOperationPreflightResult::ACCEPTING)
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
        wqe->GetWqeId());
    operation.transportReference = *reference;
    operation.directTpn = reference->IsDirectTp() ? reference->GetDirectTpn() : UINT32_MAX;
    operation.workLease = coordinator->AcquireLease(wqeIdentity,
                                                    UbLifecycleLeaseKind::WORK,
                                                    operation.operationIdentity);
    operation.transportUseLease = reference->IsDirectTp()
                                      ? coordinator->AcquireLease(
                                            wqeIdentity,
                                            UbLifecycleLeaseKind::TRANSPORT_USE,
                                            reference->GetIdentity())
                                      : coordinator->AcquireLease(
                                            reference->GetIdentity(),
                                            UbLifecycleLeaseKind::TRANSPORT_USE,
                                            operation.operationIdentity);
    operation.transportUseLeaseHeld = true;
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
        wqe->GetWqeId());
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
UbTransaction::ReleaseTransportUseLease(OperationRecord& operation)
{
    if (!operation.transportUseLeaseHeld)
    {
        return;
    }
    const bool tpgUse = !operation.transportReference.IsDirectTp();
    const UbLifecycleResourceIdentity groupIdentity =
        operation.transportReference.GetIdentity();
    m_coordinator->ReleaseLease(operation.transportUseLease);
    operation.transportUseLeaseHeld = false;
    if (tpgUse &&
        std::find(m_suppressedTpgOwnerProgress.begin(),
                  m_suppressedTpgOwnerProgress.end(),
                  groupIdentity) == m_suppressedTpgOwnerProgress.end())
    {
        Ptr<UbController> controller = GetOwnerController();
        if (controller != nullptr)
        {
            controller->NotifyRtpTpGroupUseLeaseReleased(groupIdentity);
        }
    }
}

bool
UbTransaction::HasWqeOperation(uint32_t jettyNum, uint32_t wqeId) const
{
    return m_operations.find({jettyNum, wqeId}) != m_operations.end();
}

uint32_t
UbTransaction::GetActiveOperationCount() const
{
    return static_cast<uint32_t>(m_operations.size());
}

uint32_t
UbTransaction::GetAdmissionDomainReadySourceCountForTest(
    const UbLifecycleResourceIdentity& identity) const
{
    const AdmissionDomainKey key{
        identity.kind == UbLifecycleResourceKind::RTP_TPG
            ? UbRtpTransportReferenceKind::TPG
            : UbRtpTransportReferenceKind::DIRECT_TP,
        identity};
    const auto domain = m_admissionDomains.find(key);
    return domain == m_admissionDomains.end()
               ? 0
               : static_cast<uint32_t>(domain->second.readySources.size());
}

bool
UbTransaction::IsAdmissionDomainBlockedForTest(
    const UbLifecycleResourceIdentity& identity) const
{
    const AdmissionDomainKey key{
        identity.kind == UbLifecycleResourceKind::RTP_TPG
            ? UbRtpTransportReferenceKind::TPG
            : UbRtpTransportReferenceKind::DIRECT_TP,
        identity};
    const auto domain = m_admissionDomains.find(key);
    return domain != m_admissionDomains.end() && domain->second.blocked;
}

uint64_t
UbTransaction::GetAdmissionDomainCapacityWakeCountForTest(
    const UbLifecycleResourceIdentity& identity) const
{
    const AdmissionDomainKey key{
        identity.kind == UbLifecycleResourceKind::RTP_TPG
            ? UbRtpTransportReferenceKind::TPG
            : UbRtpTransportReferenceKind::DIRECT_TP,
        identity};
    const auto domain = m_admissionDomains.find(key);
    return domain == m_admissionDomains.end() ? 0 : domain->second.capacityWakeEvents;
}

uint32_t
UbTransaction::GetAdmissionDomainCountForTest() const
{
    return static_cast<uint32_t>(m_admissionDomains.size());
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

void UbTransaction::WqeFinish(uint32_t jettyNum, Ptr<UbWqe> wqe, UbWorkOutcome outcome)
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
            jetty->ApplyWqeSegmentTerminalProgress(terminalTaSsns);
        }
        m_coordinator->ReleaseLease(operation->second.workLease);
        m_coordinator->RecordWorkTerminal(operation->second.operationIdentity, outcome);
        m_coordinator->RetireResource(operation->second.operationIdentity);
        m_coordinator->RecordWorkTerminal(operation->second.wqeIdentity, outcome);
        m_coordinator->RetireResource(operation->second.wqeIdentity);
        m_operations.erase(operation);

        auto ordered = m_jettyOrderedWqe.find(jettyNum);
        if (ordered != m_jettyOrderedWqe.end())
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
        return;
    }
    bool hadLifecycleOperation = false;
    UbLifecycleResourceIdentity wqeIdentity;
    if (operation != m_operations.end())
    {
        NS_ABORT_MSG_IF(operation->second.terminalPending,
                        "WqeFinish references an operation already awaiting transport cleanup");
        hadLifecycleOperation = true;
        DetachRtpSource(operation->second);
        wqeIdentity = operation->second.wqeIdentity;
        Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
        const bool forceDrain = GetFunction()->ShouldForceDrainAcceptedWqes(jettyNum);
        std::vector<uint32_t> terminalTaSsns;
        terminalTaSsns.reserve(operation->second.segments.size());
        for (const auto& [taSsn, segment] : operation->second.segments)
        {
            (void)segment;
            terminalTaSsns.push_back(taSsn);
        }
        if (!terminalTaSsns.empty())
        {
            Ptr<UbJetty> jetty = GetJetty(jettyNum);
            NS_ABORT_MSG_IF(jetty == nullptr,
                            "Admitted Transaction segments require their Jetty owner");
            jetty->ApplyWqeSegmentTerminalProgress(terminalTaSsns);
        }
        std::vector<uint32_t> canceledTaSsns;
        for (auto segment = operation->second.segments.rbegin();
             segment != operation->second.segments.rend();
             ++segment)
        {
            const OperationRecord::SegmentRecord& record = segment->second;
            NS_ABORT_MSG_IF(record.segment == nullptr,
                            "Admitted Transaction record lost its segment instance");
            const auto transport = m_tpnMap.find(record.segment->GetTpn());
            NS_ABORT_MSG_IF(transport == m_tpnMap.end() || transport->second == nullptr ||
                                transport->second->GetLifecycleOwner() !=
                                    record.resolvedTpIdentity,
                            "Admitted Transaction terminal lost its exact TP generation");
            const UbWqeSegmentTerminalDisposition disposition =
                transport->second->TryCancelUnsentWqeSegment(
                    record.segment,
                    {operation->second.operationIdentity, record.identity},
                    record.resolvedTpIdentity);
            if (disposition == UbWqeSegmentTerminalDisposition::RETAINED && forceDrain)
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
            if (disposition != UbWqeSegmentTerminalDisposition::RETAINED || forceDrain)
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
            operation->second.terminalPending = true;
            if (!operation->second.transportReference.IsDirectTp())
            {
                ReleaseTransportUseLease(operation->second);
            }
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
            coordinator->ReleaseLease(operation->second.workLease);
            ReleaseTransportUseLease(operation->second);
            coordinator->RecordWorkTerminal(operation->second.operationIdentity, outcome);
            coordinator->RetireResource(operation->second.operationIdentity);
            coordinator->RecordWorkTerminal(operation->second.wqeIdentity, outcome);
            coordinator->RetireResource(operation->second.wqeIdentity);
            m_operations.erase(operation);
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
    std::vector<uint32_t>& orderedWqes = m_jettyOrderedWqe[jettyNum];
    const bool terminalizedOrderedFront =
        hadLifecycleOperation && !orderedWqes.empty() &&
        orderedWqes.front() == wqe->GetWqeId();
    const bool wakeOrderedSuccessor =
        terminalizedOrderedFront && GetFunction()->ShouldContinueAcceptedWqes(jettyNum);
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
    NS_ABORT_MSG_IF(outcome == UbWorkOutcome::COMPLETED,
                    "Successful WQE completion must come from Transaction response processing");
    const auto operation = m_operations.find({jettyNum, wqeId});
    NS_ABORT_MSG_IF(operation == m_operations.end(),
                    "Transaction outcome references an unknown, duplicate, or stale operation");
    Ptr<UbWqe> wqe = operation->second.wqe;
    WqeFinish(jettyNum, wqe, outcome);
}

void
UbTransaction::ForceDrainTerminalPendingOperation(uint32_t jettyNum, uint32_t wqeId)
{
    const auto operation = m_operations.find({jettyNum, wqeId});
    if (operation == m_operations.end() || !operation->second.terminalPending)
    {
        return;
    }
    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    for (const auto& [taSsn, segment] : operation->second.segments)
    {
        (void)taSsn;
        NS_ABORT_MSG_IF(segment.segment == nullptr,
                        "Terminal-pending operation lost its exact segment instance");
        const auto transport = m_tpnMap.find(segment.segment->GetTpn());
        NS_ABORT_MSG_IF(transport == m_tpnMap.end() || transport->second == nullptr ||
                            transport->second->GetLifecycleOwner() !=
                                segment.resolvedTpIdentity,
                        "Forced terminal-pending drain lost its exact TP generation");
        const UbWqeSegmentTerminalDisposition forcedDisposition =
            transport->second->ForceRemoveRetainedWqeSegment(
                segment.segment,
                {operation->second.operationIdentity, segment.identity},
                segment.resolvedTpIdentity);
        NS_ABORT_MSG_IF(
            forcedDisposition != UbWqeSegmentTerminalDisposition::FORCE_REMOVED &&
                forcedDisposition != UbWqeSegmentTerminalDisposition::TRANSPORT_CLEANED,
            "Forced terminal-pending drain did not resolve exact TP retained state");
        coordinator->ReleaseLease(segment.childLease);
        coordinator->ReleaseLease(segment.transportWorkLease);
        coordinator->RetireResource(segment.identity);
    }
    operation->second.segments.clear();
    coordinator->ReleaseLease(operation->second.workLease);
    ReleaseTransportUseLease(operation->second);
    coordinator->RetireResource(operation->second.operationIdentity);
    coordinator->RetireResource(operation->second.wqeIdentity);
    m_operations.erase(operation);
}

void
UbTransaction::ForceDrainTerminalPendingOperations(uint32_t jettyNum)
{
    std::vector<uint32_t> pending;
    for (auto operation = m_operations.lower_bound({jettyNum, 0});
         operation != m_operations.end() && operation->first.first == jettyNum;
         ++operation)
    {
        if (operation->second.terminalPending)
        {
            pending.push_back(operation->first.second);
        }
    }
    for (uint32_t wqeId : pending)
    {
        ForceDrainTerminalPendingOperation(jettyNum, wqeId);
    }
}

void UbTransaction::DoDispose()
{
    NS_LOG_FUNCTION(this);
    for (auto& [key, domain] : m_admissionDomains)
    {
        (void)key;
        if (domain.eventPending)
        {
            Simulator::Cancel(domain.pendingEvent);
        }
    }
    m_admissionDomains.clear();
    m_tpnMap.clear();
    m_jettyOrderedWqe.clear();
    m_jettyRtpReferences.clear();
    for (auto &it : m_tpRelatedRemoteRequests) {
        for (auto &remoteMap : it.second) {
            for (auto segment : remoteMap.second.segments) {
                segment = nullptr;
            }
        }
    }
    m_tpRelatedRemoteRequests.clear();
    m_ctpTransactionStates.clear();
    m_ctpNextTaSsn.clear();
    m_tpRRIndex.clear();
    m_random = nullptr;
    m_serviceMode.clear();
    m_memoryStore.clear();
    m_operations.clear();
    m_suppressedTpgOwnerProgress.clear();
    m_coordinator = nullptr;
    m_function = nullptr;
    for (auto &it : m_jettyOrderedWqe) {
        it.second.clear();
    }
    m_jettyOrderedWqe.clear();
    Object::DoDispose();
}

std::vector<uint32_t> UbTransaction::GetUselessTpns()
{
    std::vector<uint32_t> tpns;
    // 遍历tp，找出所有本地和对端都不再使用的tp
    for (auto it = m_tpnMap.begin(); it != m_tpnMap.end(); it++) {
        uint32_t tpn = it->first;
        if (!IsTpInUse(tpn) && !IsPeerTpInUse(tpn)) {
            tpns.push_back(tpn);
        }
    }
    return tpns;
}

bool UbTransaction::IsTpInUse(uint32_t tpn)
{
    NS_ASSERT_MSG(m_tpnMap.find(tpn) != m_tpnMap.end(), "no such tp.");
    return !GetTpRelatedJettyVec(tpn).empty();
}

bool
UbTransaction::HasPendingTransportWork(uint32_t tpn) const
{
    const auto remoteRequests = m_tpRelatedRemoteRequests.find(tpn);
    return remoteRequests != m_tpRelatedRemoteRequests.end() && !remoteRequests->second.empty();
}

bool UbTransaction::IsPeerTpInUse(uint32_t tpn)
{
    auto localTpIt = m_tpnMap.find(tpn);
    NS_ASSERT_MSG(localTpIt != m_tpnMap.end(), "can't find local tpn");
    auto localTp = localTpIt->second;
    uint32_t dst = localTp->GetDest();
    uint32_t dstTpn = localTp->GetDstTpn();
    auto peerTa = NodeList::GetNode(dst)->GetObject<UbController>()->GetUbTransaction();
    return peerTa->IsTpInUse(dstTpn);
}

} // namespace ns3
