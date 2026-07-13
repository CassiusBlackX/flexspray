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
    m_tpSchedulingStatus[tpn] = false;
}

void UbTransaction::TpDeinit(uint32_t tpn)
{
    // 删除与该tp绑定的jetty
    m_tpRelatedJetties.erase(tpn);
    for (auto it = m_jettyTpGroup.begin(); it != m_jettyTpGroup.end(); ++it) {
        // 删除所有jetty中该tp的绑定关系
        auto tpIt = std::find(it->second.begin(), it->second.end(), m_tpnMap[tpn]);
        if (tpIt != it->second.end()) {
            it->second.erase(tpIt);
        }
    }
    m_tpnMap.erase(tpn);
    m_tpRRIndex.erase(tpn);
    m_tpSchedulingStatus.erase(tpn);
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
        for (const Ptr<UbTransportChannel>& transport : boundTransports) {
            const uint32_t tpn = transport->GetTpn();
            if (std::find(m_tpRelatedJetties[tpn].begin(),
                          m_tpRelatedJetties[tpn].end(),
                          ubJetty) == m_tpRelatedJetties[tpn].end()) {
                m_tpRelatedJetties[tpn].push_back(ubJetty);
            }
        }
        Ptr<UbController> controller = GetOwnerController();
        if (controller == nullptr)
        {
            return false;
        }
        const uint32_t initialSelectionCursor =
            m_nextJettyTpStartIndex % boundTransports.size();
        Ptr<UbTransportGroup> group =
            controller->CreateRtpTpGroup(boundTransports, initialSelectionCursor);
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
        if (std::find(m_tpRelatedJetties[tpn].begin(),
                      m_tpRelatedJetties[tpn].end(),
                      ubJetty) == m_tpRelatedJetties[tpn].end()) {
            m_tpRelatedJetties[tpn].push_back(ubJetty);
        }
        m_jettyRtpReferences.insert_or_assign(
            jettyNum,
            UbRtpTransportReference::ForDirectTp(tpn, transport->GetLifecycleOwner()));
    }

    m_jettyTpGroup[jettyNum] = boundTransports;
    if (!boundTransports.empty()) {
        m_jettyTpNextIndex[jettyNum] = m_nextJettyTpStartIndex % boundTransports.size();
        m_nextJettyTpStartIndex = (m_nextJettyTpStartIndex + 1) % boundTransports.size();
    } else {
        m_jettyTpNextIndex[jettyNum] = 0;
    }
    GetFunction()->RegisterJettyRtpBinding(jettyNum, boundTransports);
    return true;
}

void UbTransaction::DestroyJettyTpMap(uint32_t jettyNum)
{
    auto itJettyTp = m_jettyTpGroup.find(jettyNum);
    if (itJettyTp != m_jettyTpGroup.end()) {
        // 解除关系
        m_jettyTpGroup.erase(itJettyTp);
        m_jettyRtpReferences.erase(jettyNum);
        m_jettyTpNextIndex.erase(jettyNum);
        NS_LOG_DEBUG("Destroyed jetty in m_jettyTpGroup");
    } else {
        NS_LOG_WARN("Jetty Tp map not found for destruction");
    }

    for (auto it = m_tpRelatedJetties.begin(); it != m_tpRelatedJetties.end(); it++) {
        for (auto jettyIt = it->second.begin(); jettyIt != it->second.end();) {
            if ((*jettyIt)->GetJettyNum() == jettyNum) {
                jettyIt = it->second.erase(jettyIt);
            } else {
                ++jettyIt;
            }
        }
    }
}

const std::vector<Ptr<UbTransportChannel>> UbTransaction::GetJettyRelatedTpVec(uint32_t jettyNum)
{
    NS_LOG_DEBUG(this);
    auto it = m_jettyTpGroup.find(jettyNum);
    if (it != m_jettyTpGroup.end()) {
        return it->second;
    }
    NS_LOG_DEBUG("UbTransportChannel vector not found");
    return {};
}

std::vector<Ptr<UbJetty>> UbTransaction::GetTpRelatedJettyVec(uint32_t tpn)
{
    NS_LOG_DEBUG(this);
    auto it = m_tpRelatedJetties.find(tpn);
    if (it != m_tpRelatedJetties.end()) {
        return it->second;
    }
    NS_LOG_DEBUG("UbJetty vector not found");
    return {};
}

void
UbTransaction::TriggerScheduleWqeSegment(uint32_t jettyNum,
                                         Ptr<UbWqe> originatingWqe,
                                         bool lifecycleOperation)
{
    if (lifecycleOperation)
    {
        const uint64_t eventToken = GetFunction()->TrackJettyEvent(jettyNum);
        const EventId event = Simulator::ScheduleNow(
            &UbTransaction::RunTrackedPrepareRtpWqeSegment,
            this,
            jettyNum,
            originatingWqe,
            eventToken);
        GetFunction()->AttachJettyEvent(jettyNum, eventToken, event);
        return;
    }

    // 遍历与该jetty绑定的tp，按轮转起点调度，避免短 WQE 总落到第一个 TP。
    auto tpVec = GetJettyRelatedTpVec(jettyNum);
    if (!tpVec.empty()) {
        uint32_t& nextIndex = m_jettyTpNextIndex[jettyNum];
        if (nextIndex >= tpVec.size()) {
            nextIndex = 0;
        }
        for (uint32_t i = 0; i < tpVec.size(); i++) {
            uint32_t idx = (nextIndex + i) % tpVec.size();
            const uint64_t eventToken = GetFunction()->TrackJettyEvent(jettyNum);
            const EventId event = Simulator::ScheduleNow(
                &UbTransaction::RunTrackedScheduleWqeSegment,
                this,
                jettyNum,
                originatingWqe,
                lifecycleOperation,
                tpVec[idx],
                eventToken);
            GetFunction()->AttachJettyEvent(jettyNum, eventToken, event);
        }
        nextIndex = (nextIndex + 1) % tpVec.size();
    }
}

void
UbTransaction::RunTrackedPrepareRtpWqeSegment(uint32_t jettyNum,
                                              Ptr<UbWqe> originatingWqe,
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
                finishToken);
            GetFunction()->AttachJettyEvent(jettyNum, finishToken, finishEvent);
        }
    }
    GetFunction()->ResolveJettyEvent(jettyNum, eventToken);
}

void
UbTransaction::RunTrackedAdmitRtpWqeSegment(Ptr<UbWqeSegment> segment,
                                            Ptr<UbWqe> originatingWqe,
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
        if (result == RtpSegmentAdmissionResult::ADMITTED)
        {
            NS_LOG_INFO("WQE Segment Sends, taskId:" << segment->GetTaskId()
                                                     << "TASSN: " << segment->GetTaSsn());
            admittedTp->WqeSegmentTriggerPortTransmit(segment);
            // T05 owns capacity wakeups. T04 only continues after a successful admission so the
            // next segment observes the newly committed TPG cursor.
            TriggerScheduleWqeSegment(jettyNum, originatingWqe, true);
        }
        else if (result == RtpSegmentAdmissionResult::STALE_PREPARED_CANDIDATE)
        {
            TriggerScheduleWqeSegment(jettyNum, originatingWqe, true);
        }
    }
    GetFunction()->ResolveJettyEvent(jettyNum, eventToken);
}

void
UbTransaction::RunTrackedScheduleWqeSegment(uint32_t jettyNum,
                                            Ptr<UbWqe> originatingWqe,
                                            bool lifecycleOperation,
                                            Ptr<UbTransportChannel> tp,
                                            uint64_t eventToken)
{
    if (!GetFunction()->BeginJettyEvent(jettyNum, eventToken))
    {
        return;
    }
    if (!lifecycleOperation || IsCurrentWqeOperation(jettyNum, originatingWqe))
    {
        ScheduleWqeSegment(tp);
    }
    GetFunction()->ResolveJettyEvent(jettyNum, eventToken);
}

void UbTransaction::ApplyScheduleWqeSegment(Ptr<UbTransportChannel> tp)
{
    Simulator::ScheduleNow(&UbTransaction::ScheduleWqeSegment, this, tp);
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

void UbTransaction::ScheduleWqeSegment(Ptr<UbTransportChannel> tp)
{
    if (tp == nullptr) {
        NS_LOG_DEBUG("TP not exist, Stop schedule.");
        return;
    }
    uint32_t tpn = tp->GetTpn();
    // 若tpnmap中没有该tp记录，表示该tp对应的traffic已经完成，TP即将删除
    if (m_tpnMap.find(tpn) == m_tpnMap.end()) {
        NS_LOG_DEBUG("TP already delete, WQE finished. Stop schedule.");
        return;
    }
    // 若当前TP正处于调度状态，则结束，否则继续进行，并将状态设置为true
    if (m_tpSchedulingStatus[tpn]) {
        return;
    }
    m_tpSchedulingStatus[tpn] = true;
    // 找到tp相关的Jetty
    auto tpRelatedJetties = GetTpRelatedJettyVec(tpn);

    // 记录开始轮询的位置， 避免无限循环
    uint32_t jettyCount = tpRelatedJetties.size();
    uint32_t remoteRequestCount = 0;
    auto remoteRequestMapIt = m_tpRelatedRemoteRequests.find(tpn);
    if (remoteRequestMapIt != m_tpRelatedRemoteRequests.end()) {
        remoteRequestCount = remoteRequestMapIt->second.size();
    }
    uint32_t rrCount = jettyCount + remoteRequestCount;

    // 该TP无对应jetty，不进行调度，状态重置
    if (rrCount == 0) {
        m_tpSchedulingStatus[tpn] = false;
        return;
    }

    // 当前TP队列满，不进行调度，状态重置
    if (tp->IsWqeSegmentLimited() ) {
        tp->SetTpFullStatus(true);
        NS_LOG_DEBUG("Full TP");
        // 满队列或满segment
        m_tpSchedulingStatus[tpn] = false;
        return;
    }

    // 浅流水同时限制仍可发送的活跃 segment 和已发完未 ACK 的 segment。
    if (!tp->CanScheduleAnotherSegment()) {
        NS_LOG_DEBUG("tp shallow pipeline budget exhausted");
        m_tpSchedulingStatus[tpn] = false;
        return;
    }
    // m_tpRRIndex每次更新时都会进行取余操作，不会大于rrCount
    // 只有某个jetty完成后删除，导致rrCount变小时才会出现这种情况。此时重置轮询位置
    if (m_tpRRIndex[tpn] > rrCount) {
        m_tpRRIndex[tpn] = 0;
    }

    Ptr<UbWqeSegment> wqeSegment = nullptr;
    bool localSegment = false;
    bool lifecycleOperation = false;
    uint32_t nextRrIndex = m_tpRRIndex[tpn];
    // 从tpRRIndex开始轮询，找到第一个非空且可以拿到wqesegment的jetty，获取wqesegment
    for (uint32_t i = 0; i < rrCount; i++) {
        uint32_t rrIndex = (m_tpRRIndex[tpn] + i) % rrCount;
        if (rrIndex < jettyCount) { // 轮询本地jetty
            // 获取当前jetty
            Ptr<UbJetty> currentJetty = tpRelatedJetties[rrIndex];
            if (currentJetty == nullptr) {
                continue;
            }
            if (HasLifecycleOperationForJetty(currentJetty->GetJettyNum()))
            {
                continue;
            }
            wqeSegment = currentJetty->GetNextWqeSegment();
            if (wqeSegment == nullptr) {
                continue;
            }
            ValidateUrmaServiceModeOrDie(currentJetty->GetJettyNum(), wqeSegment);
            localSegment = true;
        } else { // 轮询remoteRequest
            uint32_t remoteIndex = rrIndex - jettyCount;
            auto remoteIt = m_tpRelatedRemoteRequests.find(tpn);
            if (remoteIt == m_tpRelatedRemoteRequests.end()) {
                continue;
            }
            auto it = remoteIt->second.begin();
            std::advance(it, remoteIndex);
            if (it == remoteIt->second.end()) {
                continue;
            }
            auto& remoteSegments = it->second;
            for (auto vecIt = remoteSegments.begin(); vecIt != remoteSegments.end();) {
                if (*vecIt == nullptr) {
                    vecIt = remoteSegments.erase(vecIt);
                    continue;
                }
                wqeSegment = *vecIt;
                remoteSegments.erase(vecIt);
                break;
            }
            if (wqeSegment == nullptr) {
                continue;
            }
            if (remoteSegments.empty()) {
                remoteIt->second.erase(it);
            }
        }
        if (wqeSegment != nullptr) {
            nextRrIndex = (rrIndex + 1) % rrCount;
            break;
        }
    }
    if (wqeSegment != nullptr) {
        wqeSegment->SetTpn(tpn);
        if (lifecycleOperation)
        {
            const auto operation = m_operations.find(
                {wqeSegment->GetJettyNum(), wqeSegment->GetTaskId()});
            NS_ABORT_MSG_IF(operation == m_operations.end(),
                            "Prepared WQE segment has no originating operation");
            Ptr<UbWqe> originatingWqe = operation->second.wqe;
            const uint64_t eventToken =
                GetFunction()->TrackJettyEvent(wqeSegment->GetJettyNum());
            const EventId event = Simulator::ScheduleNow(
                &UbTransaction::RunTrackedScheduleWqeSegmentFinish,
                this,
                wqeSegment,
                originatingWqe,
                nextRrIndex,
                localSegment,
                lifecycleOperation,
                eventToken);
            GetFunction()->AttachJettyEvent(
                wqeSegment->GetJettyNum(),
                eventToken,
                event,
                MakeCallback(&UbTransaction::CancelPendingScheduleWqeSegmentFinish, this)
                    .Bind(wqeSegment->GetTpn()));
        }
        else
        {
            Simulator::ScheduleNow(&UbTransaction::OnScheduleWqeSegmentFinish,
                                   this,
                                   wqeSegment,
                                   nextRrIndex,
                                   localSegment,
                                   lifecycleOperation);
        }
    } else {
        m_tpSchedulingStatus[tpn] = false;
    }

}

void
UbTransaction::RunTrackedScheduleWqeSegmentFinish(Ptr<UbWqeSegment> segment,
                                                  Ptr<UbWqe> originatingWqe,
                                                  uint32_t nextRrIndex,
                                                  bool localSegment,
                                                  bool lifecycleOperation,
                                                  uint64_t eventToken)
{
    const uint32_t jettyNum = segment->GetJettyNum();
    if (!GetFunction()->BeginJettyEvent(jettyNum, eventToken))
    {
        return;
    }
    if (IsCurrentWqeOperation(jettyNum, originatingWqe))
    {
        OnScheduleWqeSegmentFinish(segment, nextRrIndex, localSegment, lifecycleOperation);
    }
    else
    {
        const uint32_t tpn = segment->GetTpn();
        const auto tp = m_tpnMap.find(tpn);
        if (tp != m_tpnMap.end() && tp->second != nullptr)
        {
            // The prepared segment held this TP's scheduler busy flag. Hand it back before
            // resuming current-generation work without committing the stale segment.
            m_tpSchedulingStatus[tpn] = false;
            ScheduleWqeSegment(tp->second);
        }
        else
        {
            m_tpSchedulingStatus.erase(tpn);
        }
    }
    GetFunction()->ResolveJettyEvent(jettyNum, eventToken);
}

void
UbTransaction::CancelPendingScheduleWqeSegmentFinish(uint32_t tpn)
{
    const auto scheduling = m_tpSchedulingStatus.find(tpn);
    if (scheduling != m_tpSchedulingStatus.end())
    {
        scheduling->second = false;
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
    return operation != m_operations.end() && operation->second.wqe == wqe;
}

void UbTransaction::OnScheduleWqeSegmentFinish(Ptr<UbWqeSegment> segment,
                                               uint32_t nextRrIndex,
                                               bool localSegment,
                                               bool lifecycleOperation)
{
    const auto tpIt = m_tpnMap.find(segment->GetTpn());
    if (tpIt == m_tpnMap.end() || tpIt->second == nullptr)
    {
        m_tpSchedulingStatus.erase(segment->GetTpn());
        return;
    }
    Ptr<UbTransportChannel> tp = tpIt->second;
    if (localSegment && !lifecycleOperation)
    {
        // Legacy multi-TP scheduling eagerly commits the Jetty cursor during prepare. Preserve
        // its old finish semantics because a second capacity rejection cannot be rolled back.
        segment->SetTpMsn(tp->GetMsnCnt());
        segment->SetPsnStart(tp->GetPsnCnt());
        tp->UpdatePsnCnt(segment->GetPsnSize());
        tp->UpDateMsnCnt(1);
        tp->PushWqeSegment(segment);
    }
    else if (!tp->TryAdmitWqeSegment(segment))
    {
        if (!localSegment)
        {
            auto& remoteSegments =
                m_tpRelatedRemoteRequests[segment->GetTpn()][segment->GetOriginJettyNum()];
            remoteSegments.insert(remoteSegments.begin(), segment);
        }
        tp->SetTpFullStatus(tp->IsWqeSegmentLimited());
        m_tpSchedulingStatus[segment->GetTpn()] = false;
        return;
    }

    NS_ABORT_MSG_IF(lifecycleOperation,
                    "Lifecycle RTP work must use atomic resolved-segment admission");
    m_tpRRIndex[segment->GetTpn()] = nextRrIndex;
    NS_LOG_INFO("WQE Segment Sends, taskId:" << segment->GetTaskId()
        << "TASSN: "<< segment->GetTaSsn());
    tp->WqeSegmentTriggerPortTransmit(segment);
    // TP调度状态重置
    m_tpSchedulingStatus[segment->GetTpn()] = false;
    ScheduleWqeSegment(tp);
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
            return RtpSegmentAdmissionResult::REJECTED;
        }
        admittedTp = resolution.directTp;
    }
    else
    {
        selectedGroup = resolution.group;
        selectedGroupIndex = selectedGroup->GetMemberIndexAtOffset(0);
        const UbTransportGroupMember& member = selectedGroup->GetMember(selectedGroupIndex);
        const auto transport = m_tpnMap.find(member.tpn);
        if (transport == m_tpnMap.end() || transport->second == nullptr ||
            transport->second->GetLifecycleOwner() != member.tpIdentity ||
            !m_coordinator->IsResourceActive(member.tpIdentity) ||
            !transport->second->CanAdmitWqeSegment(segment))
        {
            return RtpSegmentAdmissionResult::REJECTED;
        }
        admittedTp = transport->second;
    }

    // Every expected rejection is complete above. From here to cursor commit, owner-local
    // execution has no callback or event interleave.
    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    const uint64_t localId = (static_cast<uint64_t>(segment->GetJettyNum()) << 32) |
                             segment->GetTaSsn();
    OperationRecord::SegmentRecord record;
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
    }
    return RtpSegmentAdmissionResult::ADMITTED;
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
    if (completion == nullptr || !completion->HasTransactionCorrelation() ||
        completion->GetTransactionOperationIdentity() != operation->second.operationIdentity ||
        completion->GetTransactionSegmentIdentity() != segment->second.identity ||
        !completion->HasResolvedTpIdentity() ||
        completion->GetResolvedTpIdentity() != segment->second.resolvedTpIdentity ||
        !m_coordinator->IsResourceActive(segment->second.resolvedTpIdentity))
    {
        return false;
    }

    Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
    coordinator->ReleaseLease(segment->second.childLease);
    coordinator->ReleaseLease(segment->second.transportWorkLease);
    coordinator->RecordWorkTerminal(segment->second.identity, UbWorkOutcome::COMPLETED);
    coordinator->RetireResource(segment->second.identity);
    operation->second.segments.erase(segment);
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

    if (!RecordSegmentSuccess(jettyNum, wqeSegment->GetTaskId(), taSsn, wqeSegment))
    {
        return false;
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
    m_tpRelatedRemoteRequests[localTpn][response->GetOriginJettyNum()].push_back(response);
    auto tpIt = m_tpnMap.find(localTpn);
    if (tpIt != m_tpnMap.end()) {
        ApplyScheduleWqeSegment(tpIt->second);
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

bool
UbTransaction::TryAdmitRtpWqe(uint32_t jettyNum,
                              Ptr<UbWqe> wqe,
                              const UbLifecycleResourceIdentity& wqeIdentity)
{
    const UbRtpTransportReference* reference = FindRtpTransportReference(jettyNum);
    RtpTransportResolution resolution;
    if (wqe == nullptr || reference == nullptr ||
        !ResolveRtpTransportReference(*reference, resolution))
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
    operation.transportUseLease = coordinator->AcquireLease(wqeIdentity,
                                                            UbLifecycleLeaseKind::TRANSPORT_USE,
                                                            reference->GetIdentity());
    m_operations.emplace(key, operation);
    wqe->MarkLifecycleManaged();
    return true;
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
    bool hadLifecycleOperation = false;
    UbLifecycleResourceIdentity wqeIdentity;
    if (operation != m_operations.end())
    {
        hadLifecycleOperation = true;
        wqeIdentity = operation->second.wqeIdentity;
        Ptr<UbLifecycleCoordinator> coordinator = m_coordinator;
        for (const auto& [taSsn, segment] : operation->second.segments)
        {
            (void)taSsn;
            coordinator->ReleaseLease(segment.childLease);
            coordinator->ReleaseLease(segment.transportWorkLease);
            coordinator->RecordWorkTerminal(segment.identity, outcome);
            coordinator->RetireResource(segment.identity);
        }
        operation->second.segments.clear();
        coordinator->ReleaseLease(operation->second.workLease);
        coordinator->ReleaseLease(operation->second.transportUseLease);
        coordinator->RecordWorkTerminal(operation->second.operationIdentity, outcome);
        coordinator->RetireResource(operation->second.operationIdentity);
        coordinator->RecordWorkTerminal(operation->second.wqeIdentity, outcome);
        coordinator->RetireResource(operation->second.wqeIdentity);
        m_operations.erase(operation);
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
    auto it = std::find(m_jettyOrderedWqe[jettyNum].begin(), m_jettyOrderedWqe[jettyNum].end(), wqe->GetWqeId());
    if (it != m_jettyOrderedWqe[jettyNum].end()) {
        m_jettyOrderedWqe[jettyNum].erase(it);
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

void UbTransaction::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_tpnMap.clear();
    m_jettyOrderedWqe.clear();
    for (auto &it : m_jettyTpGroup) {
        for (auto tp : it.second) {
            tp = nullptr;
        }
    }
    m_jettyTpGroup.clear();
    m_jettyRtpReferences.clear();
    for (auto &it : m_tpRelatedJetties) {
        for(auto jetty : it.second) {
            jetty = nullptr;
        }
    }
    m_tpRelatedJetties.clear();
    for (auto &it : m_tpRelatedRemoteRequests) {
        for (auto &remoteMap : it.second) {
            for (auto segment : remoteMap.second) {
                segment = nullptr;
            }
        }
    }
    m_tpRelatedRemoteRequests.clear();
    m_tpRRIndex.clear();
    m_tpSchedulingStatus.clear();
    m_random = nullptr;
    m_serviceMode.clear();
    m_memoryStore.clear();
    m_operations.clear();
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
    auto it = m_tpRelatedJetties.find(tpn);
    if (it == m_tpRelatedJetties.end()) {
        // 不存在记录表示无jetty使用
        return false;
    }
    // 若存在记录且对应jetty数目为0，表示使用完毕，非0，表示正在使用
    return (it->second.size() != 0);
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
