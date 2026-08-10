// SPDX-License-Identifier: GPL-2.0-only
#include "ub-ctp.h"

#include "ns3/boolean.h"
#include "ns3/global-value.h"
#include "ns3/hash.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-datalink.h"
#include "ns3/ub-function.h"
#include "ns3/ub-header.h"
#include "ns3/ub-lifecycle.h"
#include "ns3/ub-modulo-sequence.h"
#include "ns3/ub-network-address.h"
#include "ns3/ub-port.h"
#include "ns3/ub-routing-process.h"
#include "ns3/ub-switch.h"
#include "ns3/ub-transaction.h"
#include "ns3/ub-utils.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("UbCtpTransportService");
NS_OBJECT_ENSURE_REGISTERED(UbCtpTxQueue);
NS_OBJECT_ENSURE_REGISTERED(UbCtpTransportService);

namespace
{

using UbTaSsnSequence = UbModuloSequence<16, uint32_t>;

uint32_t
CalcCompactLength(uint32_t bytes)
{
    if (bytes <= 64)
    {
        return 0;
    }

    bytes = (bytes - 1) / 64;
    uint32_t length = 0;
    while (bytes > 0)
    {
        bytes >>= 1;
        ++length;
    }
    return length;
}

uint16_t
EntityLoadBalanceSalt(uint32_t srcNodeId, const UbCtpEntityKey& key)
{
    uint8_t buf[17];
    buf[0] = static_cast<uint8_t>((srcNodeId >> 24) & 0xff);
    buf[1] = static_cast<uint8_t>((srcNodeId >> 16) & 0xff);
    buf[2] = static_cast<uint8_t>((srcNodeId >> 8) & 0xff);
    buf[3] = static_cast<uint8_t>(srcNodeId & 0xff);
    buf[4] = static_cast<uint8_t>((key.dstNodeId >> 24) & 0xff);
    buf[5] = static_cast<uint8_t>((key.dstNodeId >> 16) & 0xff);
    buf[6] = static_cast<uint8_t>((key.dstNodeId >> 8) & 0xff);
    buf[7] = static_cast<uint8_t>(key.dstNodeId & 0xff);
    buf[8] = static_cast<uint8_t>((key.srcEntityId >> 24) & 0xff);
    buf[9] = static_cast<uint8_t>((key.srcEntityId >> 16) & 0xff);
    buf[10] = static_cast<uint8_t>((key.srcEntityId >> 8) & 0xff);
    buf[11] = static_cast<uint8_t>(key.srcEntityId & 0xff);
    buf[12] = static_cast<uint8_t>((key.dstEntityId >> 24) & 0xff);
    buf[13] = static_cast<uint8_t>((key.dstEntityId >> 16) & 0xff);
    buf[14] = static_cast<uint8_t>((key.dstEntityId >> 8) & 0xff);
    buf[15] = static_cast<uint8_t>(key.dstEntityId & 0xff);
    buf[16] = key.vl;
    return static_cast<uint16_t>(Hash64(std::string(reinterpret_cast<const char*>(buf), sizeof(buf))) &
                                 0xffff);
}

void
ValidateCna16PortHint(uint32_t port, const char* label)
{
    NS_ABORT_MSG_IF(port >= 15,
                    "CTP " << label << " port hint cannot be encoded as CNA16");
}

void
PadTaAckPacketForExperiment(Ptr<Packet> packet, uint32_t experimentTaAckPacketBytes)
{
    if (experimentTaAckPacketBytes == 0 || packet == nullptr)
    {
        return;
    }

    const uint32_t currentBytes = packet->GetSize();
    NS_ABORT_MSG_IF(experimentTaAckPacketBytes < currentBytes,
                    "ExperimentTaAckPacketBytes is smaller than the encoded CTP TAACK header stack");
    if (experimentTaAckPacketBytes > currentBytes)
    {
        packet->AddPaddingAtEnd(experimentTaAckPacketBytes - currentBytes);
    }
}

Ptr<Packet>
BuildCtpPacket(Ptr<UbWqeSegment> segment,
               uint32_t srcNodeId,
               const UbCtpEntityKey& key,
               uint32_t srcPortHint,
               uint32_t dstPortHint,
               uint32_t taSsn,
               uint32_t progressBytes,
               const UbCtpRoutingPolicy& routing,
               uint8_t loadBalanceSalt)
{
    NS_ABORT_MSG_IF(segment == nullptr, "CTP packet requires WQE segment");

    const bool isAckResponse =
        segment->GetSegmentKind() == UbTransactionSegmentKind::RESPONSE &&
        segment->GetType() == TaOpcode::TA_OPCODE_TRANSACTION_ACK;
    const bool isReadRequest = segment->GetType() == TaOpcode::TA_OPCODE_READ &&
                               segment->GetSegmentKind() == UbTransactionSegmentKind::REQUEST;
    const uint32_t payloadBytes = isAckResponse || isReadRequest ? 0 : progressBytes;
    const uint32_t logicalBytes = isReadRequest ? segment->GetLogicalBytes() : payloadBytes;
    NS_ABORT_MSG_IF(payloadBytes > UB_MTU_BYTE,
                    "CTP packet payload must not exceed one MTU-sized transaction segment");
    NS_ABORT_MSG_IF(isReadRequest && logicalBytes > UB_MTU_BYTE,
                    "CTP read request segment must not describe more than one MTU response");

    Ptr<Packet> packet = Create<Packet>(payloadBytes);
    UbFlowTag flowTag(segment->GetTaskId(), segment->GetWqeSize());
    packet->AddPacketTag(flowTag);

    if (segment->GetSegmentKind() == UbTransactionSegmentKind::RESPONSE)
    {
        UbCompactAckTransactionHeader ackHeader;
        ackHeader.SetTaOpcode(segment->GetType());
        ackHeader.SetIniTaSsn(static_cast<uint16_t>(taSsn));
        packet->AddHeader(ackHeader);
    }
    else
    {
        UbCompactTransactionHeader taHeader;
        taHeader.SetTaOpcode(segment->GetType());
        taHeader.SetIniTaSsn(static_cast<uint16_t>(taSsn));
        UbCompactMAExtTah maHeader;
        maHeader.SetLength(static_cast<uint8_t>(CalcCompactLength(logicalBytes)));
        packet->AddHeader(maHeader);
        packet->AddHeader(taHeader);
    }

    UbCompactEidHeader eidHeader;
    eidHeader.SetSourceEid(key.srcEntityId);
    eidHeader.SetDestinationEid(key.dstEntityId);
    packet->AddHeader(eidHeader);

    UbCompactUpiHeader upiHeader;
    upiHeader.SetUpi(0);
    packet->AddHeader(upiHeader);

    UbCtpHeader ctpHeader;
    ctpHeader.SetTPOpcode(CtpOpcode::CTP_DATA);
    ctpHeader.SetPadding(0);
    ctpHeader.SetNlp(UB_CTPH_NLP_UPI16_EID40_TAH);
    packet->AddHeader(ctpHeader);

    UbCna16NetworkHeader cnaHeader;
    const uint32_t srcCna = srcPortHint == UINT32_MAX
                                ? utils::NodeIdToCna16(srcNodeId)
                                : utils::NodeIdToCna16(srcNodeId, srcPortHint);
    cnaHeader.SetScna(static_cast<uint16_t>(srcCna));
    const uint32_t dstCna =
        dstPortHint == UINT32_MAX ? utils::NodeIdToCna16(key.dstNodeId)
                                  : utils::NodeIdToCna16(key.dstNodeId, dstPortHint);
    cnaHeader.SetDcna(static_cast<uint16_t>(dstCna));
    cnaHeader.SetLb(loadBalanceSalt);
    cnaHeader.SetServiceLevel(key.vl);
    cnaHeader.SetNlp(UB_CNA_NLP_CTPH);
    packet->AddHeader(cnaHeader);

    UbDataLink::GenPacketHeader(packet,
                                false,
                                false,
                                key.vl,
                                key.vl,
                                routing.routingType,
                                UbDatalinkHeaderConfig::PACKET_CNA16);
    return packet;
}

} // namespace

bool
UbCtpCongestionKey::operator<(const UbCtpCongestionKey& other) const
{
    return std::tie(dstNodeId, dstEntityId, vl) <
           std::tie(other.dstNodeId, other.dstEntityId, other.vl);
}

UbCtpEntityState::UbCtpEntityState(UbCtpEntityKey key)
    : m_key(key)
{
}

UbCtpTxState&
UbCtpEntityState::Tx()
{
    return m_tx;
}

const UbCtpTxState&
UbCtpEntityState::Tx() const
{
    return m_tx;
}

UbCtpRxState&
UbCtpEntityState::Rx()
{
    return m_rx;
}

const UbCtpRxState&
UbCtpEntityState::Rx() const
{
    return m_rx;
}

const UbCtpEntityKey&
UbCtpEntityState::GetKey() const
{
    return m_key;
}

void
UbCtpEntityState::AddOutPort(uint32_t outPort)
{
    if (std::find(m_tx.outPorts.begin(), m_tx.outPorts.end(), outPort) == m_tx.outPorts.end())
    {
        m_tx.outPorts.push_back(outPort);
    }
}

const std::vector<uint32_t>&
UbCtpEntityState::GetOutPorts() const
{
    return m_tx.outPorts;
}

TypeId
UbCtpTxQueue::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbCtpTxQueue")
                            .SetParent<UbIngressQueue>()
                            .SetGroupName("UnifiedBus")
                            .AddConstructor<UbCtpTxQueue>();
    return tid;
}

UbCtpTxQueue::UbCtpTxQueue() = default;

UbCtpTxQueue::~UbCtpTxQueue() = default;

IngressQueueType
UbCtpTxQueue::GetIngressQueueType()
{
    return IngressQueueType::CTP;
}

bool
UbCtpTxQueue::IsEmpty()
{
    return m_cnpQueue.empty() && m_ackQueue.empty() && m_dataQueue.empty();
}

Ptr<Packet>
UbCtpTxQueue::GetNextPacket()
{
    std::queue<Ptr<Packet>>* queue = GetPriorityQueue();
    if (queue == nullptr)
    {
        return nullptr;
    }

    Ptr<Packet> packet = queue->front();
    queue->pop();
    if (!IsEmpty())
    {
        m_headArrivalTime = Simulator::Now();
    }
    RefreshAllocatorSummary();
    return packet;
}

uint32_t
UbCtpTxQueue::GetNextPacketSize()
{
    const std::queue<Ptr<Packet>>* queue = GetPriorityQueue();
    return queue == nullptr ? 0 : queue->front()->GetSize();
}

void
UbCtpTxQueue::EnqueueData(Ptr<Packet> packet)
{
    EnqueueToClass(UbCtpQueueClass::DATA, packet);
}

void
UbCtpTxQueue::EnqueueAck(Ptr<Packet> packet)
{
    EnqueueToClass(UbCtpQueueClass::ACK, packet);
}

void
UbCtpTxQueue::EnqueueCnp(Ptr<Packet> packet)
{
    EnqueueToClass(UbCtpQueueClass::CNP, packet);
}

void
UbCtpTxQueue::Enqueue(Ptr<Packet> packet)
{
    EnqueueData(packet);
}

Ptr<Packet>
UbCtpTxQueue::PeekNextPacket() const
{
    const std::queue<Ptr<Packet>>* queue = GetPriorityQueue();
    return queue == nullptr ? nullptr : queue->front();
}

uint32_t
UbCtpTxQueue::GetQueuedPacketCountForTest() const
{
    return static_cast<uint32_t>(m_dataQueue.size() + m_ackQueue.size() + m_cnpQueue.size());
}

std::queue<Ptr<Packet>>&
UbCtpTxQueue::QueueForClass(UbCtpQueueClass queueClass)
{
    switch (queueClass)
    {
    case UbCtpQueueClass::CNP:
        return m_cnpQueue;
    case UbCtpQueueClass::ACK:
        return m_ackQueue;
    case UbCtpQueueClass::DATA:
    default:
        return m_dataQueue;
    }
}

const std::queue<Ptr<Packet>>&
UbCtpTxQueue::QueueForClass(UbCtpQueueClass queueClass) const
{
    switch (queueClass)
    {
    case UbCtpQueueClass::CNP:
        return m_cnpQueue;
    case UbCtpQueueClass::ACK:
        return m_ackQueue;
    case UbCtpQueueClass::DATA:
    default:
        return m_dataQueue;
    }
}

std::queue<Ptr<Packet>>*
UbCtpTxQueue::GetPriorityQueue()
{
    return const_cast<std::queue<Ptr<Packet>>*>(
        static_cast<const UbCtpTxQueue*>(this)->GetPriorityQueue());
}

const std::queue<Ptr<Packet>>*
UbCtpTxQueue::GetPriorityQueue() const
{
    if (!m_cnpQueue.empty())
    {
        return &m_cnpQueue;
    }
    if (!m_ackQueue.empty())
    {
        return &m_ackQueue;
    }
    if (!m_dataQueue.empty())
    {
        return &m_dataQueue;
    }
    return nullptr;
}

void
UbCtpTxQueue::EnqueueToClass(UbCtpQueueClass queueClass, Ptr<Packet> packet)
{
    NS_ABORT_MSG_IF(packet == nullptr, "Cannot enqueue null CTP packet");
    const bool wasEmpty = IsEmpty();
    if (wasEmpty)
    {
        m_headArrivalTime = Simulator::Now();
    }
    QueueForClass(queueClass).push(packet);
    if (wasEmpty)
    {
        RefreshAllocatorSummary();
    }
}

TypeId
UbCtpTransportService::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbCtpTransportService")
                            .SetParent<Object>()
                            .SetGroupName("UnifiedBus")
                            .AddConstructor<UbCtpTransportService>()
                            .AddAttribute("BoundOutPortCount",
                                          "Maximum number of shortest-path outports that a CTP "
                                          "Entity/VL may bind. Zero means no explicit limit.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_boundOutPortCount),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("ExperimentTaAckPacketBytes",
                                          "Temporary experiment hook: pad CTP TAACK packets to this "
                                          "total wire packet size in bytes when non-zero.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_experimentTaAckPacketBytes),
                                          MakeUintegerChecker<uint32_t>(0, UB_MTU_BYTE))
                            .AddAttribute("WindowTraceEnabled",
                                          "Enable CTP transaction admission and TAACK window trace.",
                                          BooleanValue(false),
                                          MakeBooleanAccessor(
                                              &UbCtpTransportService::m_windowTraceEnabled),
                                          MakeBooleanChecker())
                            .AddAttribute("WindowTraceSrcNode",
                                          "Trace only this CTP source node. UINT32_MAX traces any source.",
                                          UintegerValue(std::numeric_limits<uint32_t>::max()),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_windowTraceSrcNode),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("WindowTraceDstNode",
                                          "Trace only this CTP destination node. UINT32_MAX traces any destination.",
                                          UintegerValue(std::numeric_limits<uint32_t>::max()),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_windowTraceDstNode),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("DelayTaAckSrcNode",
                                          "Experiment hook: delay TAACK completion only for this CTP source node.",
                                          UintegerValue(std::numeric_limits<uint32_t>::max()),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_delayTaAckSrcNode),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("DelayTaAckDstNode",
                                          "Experiment hook: delay TAACK completion only for this CTP destination node.",
                                          UintegerValue(std::numeric_limits<uint32_t>::max()),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_delayTaAckDstNode),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("DelayTaAckSequence",
                                          "Experiment hook: delay this TAACK sequence. UINT32_MAX disables it.",
                                          UintegerValue(std::numeric_limits<uint32_t>::max()),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_delayTaAckSequence),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("DelayTaAckModulo",
                                          "Experiment hook: delay TAACKs matching sequence modulo.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_delayTaAckModulo),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("DelayTaAckRemainder",
                                          "Experiment hook: modulo remainder for DelayTaAckModulo.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(
                                              &UbCtpTransportService::m_delayTaAckRemainder),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("DelayTaAckTime",
                                          "Experiment hook: delay duration for matching TAACK.",
                                          TimeValue(NanoSeconds(0)),
                                          MakeTimeAccessor(
                                              &UbCtpTransportService::m_delayTaAckTime),
                                          MakeTimeChecker())
                            .AddTraceSource(
                                "FirstPacketSendsNotify",
                                "Fires when the first packet of a CTP TA unit is enqueued.",
                                MakeTraceSourceAccessor(
                                    &UbCtpTransportService::m_traceFirstPacketSendsNotify),
                                "ns3::UbCtpTransportService::FirstPacketSendsNotify")
                            .AddTraceSource(
                                "LastPacketACKsNotify",
                                "Fires when the last TAACK for a CTP TA unit is processed.",
                                MakeTraceSourceAccessor(
                                    &UbCtpTransportService::m_traceLastPacketACKsNotify),
                                "ns3::UbCtpTransportService::LastPacketACKsNotify")
                            .AddTraceSource(
                                "CtpRecvNotify",
                                "Fires when a CTP data or ACK packet is received with path trace.",
                                MakeTraceSourceAccessor(&UbCtpTransportService::m_ctpRecvNotify),
                                "ns3::UbCtpTransportService::CtpRecvNotify");
    return tid;
}

UbCtpTransportService::UbCtpTransportService()
{
    BooleanValue traceEnable(false);
    BooleanValue packetTraceEnable(false);
    const bool traceValueExists =
        GlobalValue::GetValueByNameFailSafe("UB_TRACE_ENABLE", traceEnable);
    const bool packetTraceValueExists =
        GlobalValue::GetValueByNameFailSafe("UB_PACKET_TRACE_ENABLE", packetTraceEnable);
    m_pktTraceEnabled =
        traceValueExists && packetTraceValueExists && traceEnable.Get() && packetTraceEnable.Get();
}

UbCtpTransportService::~UbCtpTransportService() = default;

void
UbCtpTransportService::SetNode(Ptr<Node> node)
{
    NS_ABORT_MSG_IF(node == nullptr, "CTP service requires an owning node");
    NS_ABORT_MSG_IF(m_node != nullptr && m_node != node,
                    "CTP service cannot move between owning nodes");
    m_node = node;
    Ptr<UbLifecycleCoordinator> coordinator = UbLifecycleCoordinator::GetOrCreate(node);
    m_lifecycleOwner =
        coordinator->OpenResource(UbLifecycleResourceKind::CTP_SERVICE, 0);
    m_delayedTaAckEvents.Init(coordinator);
}

Ptr<Node>
UbCtpTransportService::GetNode() const
{
    return m_node;
}

void
UbCtpTransportService::SetRoutingType(RoutingType routingType)
{
    m_defaultRoutingPolicy.routingType = routingType;
}

void
UbCtpTransportService::ValidateRoutingType(RoutingType routingType, const std::string& owner) const
{
    if (m_node == nullptr)
    {
        return;
    }
    Ptr<UbSwitch> sw = m_node->GetObject<UbSwitch>();
    if (sw == nullptr || sw->GetRoutingProcess() == nullptr)
    {
        return;
    }
    sw->GetRoutingProcess()->ValidateRoutingType(routingType, owner);
}

void
UbCtpTransportService::SetBoundOutPortCountForTest(uint32_t boundOutPortCount)
{
    m_boundOutPortCount = boundOutPortCount;
}

void
UbCtpTransportService::SetExperimentTaAckPacketBytesForTest(uint32_t packetBytes)
{
    NS_ABORT_MSG_IF(packetBytes > UB_MTU_BYTE,
                    "ExperimentTaAckPacketBytes must not exceed UB_MTU_BYTE");
    m_experimentTaAckPacketBytes = packetBytes;
}

void
UbCtpTransportService::SetSourcePortHint(const UbCtpEntityKey& key, uint32_t port)
{
    ValidateCna16PortHint(port, "source");
    m_srcPortHints[key] = port;
}

void
UbCtpTransportService::ClearSourcePortHint(const UbCtpEntityKey& key)
{
    m_srcPortHints.erase(key);
}

void
UbCtpTransportService::SetDestinationPortHint(const UbCtpEntityKey& key, uint32_t port)
{
    ValidateCna16PortHint(port, "destination");
    m_dstPortHints[key] = port;
}

void
UbCtpTransportService::ClearDestinationPortHint(const UbCtpEntityKey& key)
{
    m_dstPortHints.erase(key);
}

UbCtpEntityState&
UbCtpTransportService::GetOrCreateEntityState(const UbCtpEntityKey& key)
{
    auto [it, inserted] = m_entityStates.try_emplace(key, key);
    if (inserted)
    {
        it->second.Tx().routing = m_defaultRoutingPolicy;
    }
    return it->second;
}

Ptr<UbTransaction>
UbCtpTransportService::GetTransactionOwner() const
{
    Ptr<UbController> controller = m_node == nullptr ? nullptr : m_node->GetObject<UbController>();
    Ptr<UbTransaction> transaction = controller == nullptr ? nullptr : controller->GetUbTransaction();
    NS_ABORT_MSG_IF(transaction == nullptr, "CTP transaction state requires the Transaction owner");
    return transaction;
}

UbCtpTransactionState&
UbCtpTransportService::GetTransactionState(const UbCtpEntityKey& key) const
{
    return GetTransactionOwner()->GetOrCreateCtpTransactionState(key);
}

void
UbCtpTransportService::ConfigureRoutingPolicy(const UbCtpEntityKey& key,
                                              RoutingType routingType)
{
    ValidateRoutingType(routingType, "CTP entity routing policy");
    UbCtpEntityState& state = GetOrCreateEntityState(key);
    state.Tx().routing.routingType = routingType;
}

uint32_t
UbCtpTransportService::GetEntityStateCount() const
{
    return static_cast<uint32_t>(m_entityStates.size());
}

Ptr<UbCtpTransactionContext>
UbCtpTransportService::GetOrCreateTransactionContext(const UbCtpEntityKey& key)
{
    GetOrCreateEntityState(key);
    return GetTransactionOwner()->GetOrCreateCtpTransactionContext(key);
}

bool
UbCtpTransportService::HasTransactionContextForTesting(const UbCtpEntityKey& key) const
{
    if (m_node == nullptr)
    {
        return false;
    }
    Ptr<UbController> controller = m_node->GetObject<UbController>();
    Ptr<UbTransaction> transaction = controller == nullptr ? nullptr : controller->GetUbTransaction();
    const UbCtpTransactionState* state =
        transaction == nullptr ? nullptr : transaction->FindCtpTransactionState(key);
    return state != nullptr && state->context != nullptr;
}

Ptr<UbCtpTxQueue>
UbCtpTransportService::GetOrCreateQueue(const UbCtpEntityKey& key,
                                        uint32_t outPort,
                                        uint32_t priority)
{
    QueueKey queueKey = std::make_tuple(key, outPort, priority);
    auto it = m_queues.find(queueKey);
    if (it != m_queues.end())
    {
        return it->second;
    }

    Ptr<UbCtpTxQueue> queue = CreateObject<UbCtpTxQueue>();
    UbCtpEntityState& state = GetOrCreateEntityState(key);
    queue->SetOutPortId(outPort);
    queue->SetInPortId(outPort);
    queue->SetIngressPriority(priority);
    m_queues.emplace(queueKey, queue);
    state.AddOutPort(outPort);
    return queue;
}

Ptr<Packet>
UbCtpTransportService::BuildDataPacket(Ptr<UbWqeSegment> segment, const UbCtpEntityKey& key)
{
    NS_ABORT_MSG_IF(segment == nullptr, "CTP packet requires WQE segment");

    const uint32_t taSsn = segment->GetSegmentKind() == UbTransactionSegmentKind::RESPONSE
                               ? segment->GetRequestTassn()
                               : segment->GetTaSsn();
    const uint32_t progressBytes =
        segment->GetSegmentKind() == UbTransactionSegmentKind::RESPONSE &&
                segment->GetType() == TaOpcode::TA_OPCODE_TRANSACTION_ACK
            ? 0
            : static_cast<uint32_t>(segment->GetBytesLeft());
    const UbCtpEntityKey packetKey = MakeTxKeyForSegment(key, segment);
    UbCtpEntityState& state = GetOrCreateEntityState(packetKey);
    const uint16_t lbSalt = NextLoadBalanceSalt(state, packetKey);
    return BuildDataPacketWithTaSsn(segment,
                                    state,
                                    packetKey,
                                    taSsn,
                                    progressBytes,
                                    static_cast<uint8_t>(lbSalt & 0xff));
}

Ptr<Packet>
UbCtpTransportService::BuildDataPacketWithTaSsn(Ptr<UbWqeSegment> segment,
                                                UbCtpEntityState& state,
                                                const UbCtpEntityKey& packetKey,
                                                uint32_t taSsn,
                                                uint32_t progressBytes,
                                                uint8_t loadBalanceSalt)
{
    Ptr<Packet> packet = BuildCtpPacket(segment,
                                        m_node->GetId(),
                                        packetKey,
                                        ResolveSourcePortHint(packetKey),
                                        ResolveDestinationPortHint(packetKey),
                                        taSsn,
                                        progressBytes,
                                        state.Tx().routing,
                                        loadBalanceSalt);
    PadTaAckPacketForExperiment(packet);
    return packet;
}

void
UbCtpTransportService::PadTaAckPacketForExperiment(Ptr<Packet> packet) const
{
    ns3::PadTaAckPacketForExperiment(packet, m_experimentTaAckPacketBytes);
}

Ptr<Packet>
UbCtpTransportService::BuildResponsePacketForTest(Ptr<UbWqeSegment> response,
                                                  const UbCtpEntityKey& key)
{
    return BuildDataPacket(response, key);
}

void
UbCtpTransportService::AddCtpNetworkHeaders(Ptr<Packet> packet,
                                            const UbCtpEntityKey& key,
                                            CtpOpcode opcode,
                                            uint8_t loadBalanceSalt)
{
    AddCtpNetworkHeaders(packet,
                         key,
                         opcode,
                         loadBalanceSalt,
                         m_defaultRoutingPolicy.routingType);
}

void
UbCtpTransportService::AddCtpNetworkHeaders(Ptr<Packet> packet,
                                            const UbCtpEntityKey& key,
                                            CtpOpcode opcode,
                                            uint8_t loadBalanceSalt,
                                            RoutingType routingType)
{
    UbCtpRoutingPolicy routing{.routingType = routingType,
                               .nextSprayIndex = 0};
    Ptr<UbWqeSegment> empty = CreateObject<UbWqeSegment>();
    empty->SetSegmentKind(UbTransactionSegmentKind::REQUEST);
    empty->SetType(TaOpcode::TA_OPCODE_WRITE);
    empty->SetPayloadBytes(packet->GetSize());
    empty->SetCarrierBytes(packet->GetSize());
    Ptr<Packet> headers = BuildCtpPacket(empty,
                                         m_node->GetId(),
                                         key,
                                         ResolveSourcePortHint(key),
                                         ResolveDestinationPortHint(key),
                                         0,
                                         packet->GetSize(),
                                         routing,
                                         loadBalanceSalt);
    (void)headers;
    NS_ABORT_MSG_IF(opcode != CtpOpcode::CTP_DATA,
                    "manual CTP header helper only supports CTP_DATA in the entity path");
}

bool
UbCtpTransportService::SendSegment(Ptr<UbWqeSegment> segment, const UbCtpEntityKey& key)
{
    NS_ABORT_MSG_IF(segment == nullptr, "CTP send requires WQE segment");
    const UbCtpTransportConstraints constraints = GetTransactionConstraints();
    if (segment->GetBytesLeft() > constraints.maxSegmentBytes)
    {
        return false;
    }

    UbCtpEntityState& state = GetOrCreateEntityState(key);
    Ptr<UbCtpTransactionContext> context = GetOrCreateTransactionContext(key);
    if (!segment->IsSentCompleted())
    {
        const uint32_t progressBytes = static_cast<uint32_t>(segment->GetBytesLeft());
        if (progressBytes == 0)
        {
            return true;
        }
        const uint32_t taSsn = segment->GetTaSsn();
        if (!context->TryAdmit(taSsn))
        {
            TraceWindowEvent(key, "admit-blocked", taSsn, *context);
            return false;
        }
        TraceWindowEvent(key, "admit", taSsn, *context);
        const UbCtpEntityKey packetKey = MakeTxKeyForSegment(key, segment);
        if (!SendAdmittedFragment(segment, state, packetKey, taSsn, progressBytes))
        {
            return false;
        }
    }
    return segment->IsSentCompleted();
}

void
UbCtpTransportService::PrepareJetty(Ptr<UbJetty> jetty)
{
    NS_ABORT_MSG_IF(jetty == nullptr, "CTP send requires jetty");
}

UbCtpTransportConstraints
UbCtpTransportService::GetTransactionConstraints() const
{
    return {};
}

void
UbCtpTransportService::StartJetty(Ptr<UbJetty> jetty, const UbCtpEntityKey& key)
{
    PrepareJetty(jetty);

    auto& jettys = m_keyJettys[key];
    if (std::find(jettys.begin(), jettys.end(), jetty) == jettys.end())
    {
        jettys.push_back(jetty);
    }
    m_node->GetObject<UbController>()->GetUbFunction()->RegisterJettyCtpBinding(
        jetty->GetJettyNum(),
        Ptr<UbCtpTransportService>(this));
    UbCtpEntityState& state = GetOrCreateEntityState(key);
    state.Tx().routing.routingType = m_defaultRoutingPolicy.routingType;
    DrainJettys(state);
}

void
UbCtpTransportService::ReleaseJetty(uint32_t jettyNum)
{
    for (auto it = m_keyJettys.begin(); it != m_keyJettys.end();)
    {
        auto& jettys = it->second;
        jettys.erase(std::remove_if(jettys.begin(),
                                    jettys.end(),
                                    [jettyNum](const Ptr<UbJetty>& jetty) {
                                        return jetty == nullptr ||
                                               jetty->GetJettyNum() == jettyNum;
                                    }),
                      jettys.end());
        if (jettys.empty())
        {
            m_keyJettyRrIndex.erase(it->first);
            it = m_keyJettys.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool
UbCtpTransportService::HasJettyBindingForTest(uint32_t jettyNum) const
{
    for (const auto& [key, jettys] : m_keyJettys)
    {
        (void)key;
        if (std::any_of(jettys.begin(),
                        jettys.end(),
                        [jettyNum](const Ptr<UbJetty>& jetty) {
                            return jetty != nullptr && jetty->GetJettyNum() == jettyNum;
                        }))
        {
            return true;
        }
    }
    return false;
}

UbCtpEntityKey
UbCtpTransportService::MakeSourceGroupKeyForRequest(const UbCtpEntityKey& key) const
{
    UbCtpEntityKey sourceKey = key;
    sourceKey.dstNodeId = 0;
    sourceKey.dstEntityId = 0;
    return sourceKey;
}

UbCtpEntityKey
UbCtpTransportService::MakeTxKeyForSegment(const UbCtpEntityKey& jettyKey,
                                           Ptr<UbWqeSegment> segment) const
{
    NS_ABORT_MSG_IF(segment == nullptr, "CTP segment key requires segment");
    UbCtpEntityKey txKey = jettyKey;
    txKey.dstNodeId = segment->GetDest();
    if (segment->HasSrcEntityId())
    {
        txKey.srcEntityId = segment->GetSrcEntityId();
    }
    if (segment->HasDstEntityId())
    {
        txKey.dstEntityId = segment->GetDstEntityId();
    }
    txKey.vl = static_cast<uint8_t>(segment->GetPriority());
    return txKey;
}

bool
UbCtpTransportService::SendAdmittedFragment(Ptr<UbWqeSegment> segment,
                                            UbCtpEntityState& state,
                                            const UbCtpEntityKey& packetKey,
                                            uint32_t taSsn,
                                            uint32_t progressBytes)
{
    UbCtpTransactionState& transactionState = GetTransactionState(state.GetKey());
    if (transactionState.outstandingSegments.find(taSsn) ==
        transactionState.outstandingSegments.end())
    {
        transactionState.outstandingSegments[taSsn] = segment;
        ++transactionState.segmentOutstandingPackets[segment];
    }

    const uint16_t lbSalt = NextLoadBalanceSalt(state, packetKey);
    const uint32_t outPort = SelectOutPort(state, packetKey, lbSalt);
    Ptr<Packet> packet = BuildDataPacketWithTaSsn(segment,
                                                  state,
                                                  packetKey,
                                                  taSsn,
                                                  progressBytes,
                                                  static_cast<uint8_t>(lbSalt & 0xff));
    Ptr<UbCtpTxQueue> queue = GetOrCreateQueue(packetKey, outPort, packetKey.vl);
    RegisterQueueIfNeeded(queue, outPort, packetKey.vl);
    queue->EnqueueData(packet);
    FirstPacketSendsNotify(segment->GetTaskId(),
                           packetKey,
                           taSsn,
                           outPort,
                           progressBytes,
                           segment->GetType());
    segment->UpdateSentBytes(progressBytes);
    TraceWindowEvent(packetKey, "data-enqueued", taSsn, *transactionState.context, outPort);

    Ptr<UbPort> port = DynamicCast<UbPort>(m_node->GetDevice(outPort));
    NS_ABORT_MSG_IF(port == nullptr, "CTP send selected a non-UB source port");
    port->TriggerTransmit();
    return true;
}

bool
UbCtpTransportService::AdmitSegmentForTest(Ptr<UbWqeSegment> segment,
                                           const UbCtpEntityKey& key,
                                           uint32_t taSsn)
{
    GetOrCreateEntityState(key);
    UbCtpTransactionState& transactionState = GetTransactionState(key);
    Ptr<UbCtpTransactionContext> context = GetOrCreateTransactionContext(key);
    if (!context->TryAdmit(taSsn))
    {
        return false;
    }
    transactionState.outstandingSegments[taSsn] = segment;
    ++transactionState.segmentOutstandingPackets[segment];
    return true;
}

bool
UbCtpTransportService::SendResponseSegmentForTest(Ptr<UbWqeSegment> response,
                                                  const UbCtpEntityKey& key)
{
    UbCtpEntityState& state = GetOrCreateEntityState(key);
    return SendResponseSegment(response, state);
}

void
UbCtpTransportService::DrainJettys(UbCtpEntityState& state)
{
    const UbCtpEntityKey& key = state.GetKey();
    auto jettysIt = m_keyJettys.find(key);
    if (jettysIt == m_keyJettys.end() || jettysIt->second.empty())
    {
        return;
    }

    auto& jettys = jettysIt->second;
    UbCtpTransactionState& transactionState = GetTransactionState(key);
    auto& pendingSegments = transactionState.pendingSegments;
    uint32_t& rrIndex = m_keyJettyRrIndex[key];
    while (!jettys.empty())
    {
        if (pendingSegments.empty())
        {
            Ptr<UbWqeSegment> segment = nullptr;
            const uint32_t count = static_cast<uint32_t>(jettys.size());
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t idx = (rrIndex + i) % count;
                if (jettys[idx] == nullptr)
                {
                    continue;
                }
                Ptr<UbController> controller = m_node->GetObject<UbController>();
                NS_ABORT_MSG_IF(controller == nullptr || controller->GetUbTransaction() == nullptr,
                                "CTP segmentation requires the Transaction owner");
                segment = controller->GetUbTransaction()->PrepareNextCtpWqeSegment(
                    jettys[idx],
                    GetTransactionConstraints().maxSegmentBytes);
                if (segment != nullptr)
                {
                    rrIndex = (idx + 1) % count;
                    break;
                }
            }

            if (segment == nullptr)
            {
                return;
            }
            pendingSegments.push_back(segment);
        }

        Ptr<UbWqeSegment> segment = pendingSegments.front();
        if (segment == nullptr || segment->IsSentCompleted())
        {
            pendingSegments.pop_front();
            continue;
        }

        const UbCtpEntityKey packetKey = MakeTxKeyForSegment(key, segment);
        Ptr<UbCtpTransactionContext> context = GetOrCreateTransactionContext(key);
        const uint32_t taSsn = segment->GetTaSsn();
        if (!context->TryAdmit(taSsn))
        {
            TraceWindowEvent(packetKey, "drain-blocked", taSsn, *context);
            return;
        }
        TraceWindowEvent(packetKey, "admit", taSsn, *context);
        m_node->GetObject<UbController>()->GetUbTransaction()->CommitCtpWqeSegment(segment);
        const uint32_t progressBytes = static_cast<uint32_t>(segment->GetBytesLeft());
        SendAdmittedFragment(segment, state, packetKey, taSsn, progressBytes);
        if (segment->IsSentCompleted())
        {
            pendingSegments.pop_front();
        }
    }
}

bool
UbCtpTransportService::HandleReceivedPacket(Ptr<Packet> packet)
{
    return HandleReceivedPacket(packet, std::numeric_limits<uint32_t>::max());
}

bool
UbCtpTransportService::HandleReceivedPacket(Ptr<Packet> packet, uint32_t receivePortHint)
{
    if (packet == nullptr)
    {
        return false;
    }

    Ptr<Packet> copy = CopyPacketView(packet);

    UbDatalinkPacketHeader linkHeader;
    if (copy->RemoveHeader(linkHeader) == 0 ||
        linkHeader.GetConfig() != static_cast<uint8_t>(UbDatalinkHeaderConfig::PACKET_CNA16))
    {
        return false;
    }

    UbCna16NetworkHeader cnaHeader;
    if (copy->RemoveHeader(cnaHeader) == 0 || cnaHeader.GetNlp() != UB_CNA_NLP_CTPH)
    {
        return false;
    }

    UbCtpHeader ctpHeader;
    if (copy->RemoveHeader(ctpHeader) == 0)
    {
        return false;
    }

    uint32_t srcEntityId = 0;
    uint32_t dstEntityId = 0;
    if (ctpHeader.GetNlp() == UB_CTPH_NLP_UPI16_EID40_TAH)
    {
        UbCompactUpiHeader upiHeader;
        UbCompactEidHeader eidHeader;
        if (copy->RemoveHeader(upiHeader) == 0 || copy->RemoveHeader(eidHeader) == 0)
        {
            return false;
        }
        srcEntityId = eidHeader.GetSourceEid();
        dstEntityId = eidHeader.GetDestinationEid();
    }
    else if (ctpHeader.GetNlp() != UB_CTPH_NLP_COMPACT_TAH)
    {
        return false;
    }

    const uint32_t wireSrcNodeId = utils::Cna16ToNodeId(cnaHeader.GetScna());
    const uint32_t wireDstNodeId = utils::Cna16ToNodeId(cnaHeader.GetDcna());
    UbCtpEntityKey localKey{.srcEntityId = dstEntityId,
                            .dstNodeId = wireSrcNodeId,
                            .dstEntityId = srcEntityId,
                            .vl = cnaHeader.GetServiceLevel()};
    UbCtpEntityState& localState = GetOrCreateEntityState(localKey);
    ++localState.Rx().receivedPackets;

    if (ctpHeader.GetTPOpcode() == static_cast<uint8_t>(CtpOpcode::CTP_CNP))
    {
        RecordCnpForTest(localKey);
        return true;
    }

    if (ctpHeader.GetTPOpcode() != static_cast<uint8_t>(CtpOpcode::CTP_DATA))
    {
        return false;
    }

    if (copy->GetSize() >= UbCompactAckTransactionHeader().GetSerializedSize())
    {
        Ptr<Packet> ackCandidate = CopyPacketView(copy);
        UbCompactAckTransactionHeader ackHeader;
        if (ackCandidate->RemoveHeader(ackHeader) != 0 &&
            (ackHeader.GetTaOpcode() ==
                 static_cast<uint8_t>(TaOpcode::TA_OPCODE_TRANSACTION_ACK) ||
             ackHeader.GetTaOpcode() == static_cast<uint8_t>(TaOpcode::TA_OPCODE_READ_RESPONSE) ||
             ackHeader.GetTaOpcode() == static_cast<uint8_t>(TaOpcode::TA_OPCODE_ATOMIC_RESPONSE)))
        {
            UbFlowTag flowTag;
            copy->PeekPacketTag(flowTag);
            UbPacketTraceTag traceTag;
            copy->PeekPacketTag(traceTag);
            CtpRecvNotify(packet->GetUid(),
                          localKey,
                          ackHeader.GetIniTaSsn(),
                          PacketType::ACK,
                          copy->GetSize(),
                          flowTag.GetFlowId(),
                          traceTag);

            Ptr<UbWqeSegment> response = CreateObject<UbWqeSegment>();
            response->SetSrc(wireSrcNodeId);
            response->SetDest(wireDstNodeId);
            response->SetPriority(localKey.vl);
            response->SetTaskId(flowTag.GetFlowId());
            response->SetWqeSize(flowTag.GetFlowSize());
            response->SetJettyNum(ackHeader.GetIniTaSsn());
            response->SetTaMsn(0);
            response->SetTaSsn(ackHeader.GetIniTaSsn());
            response->SetSegmentKind(UbTransactionSegmentKind::RESPONSE);
            response->SetOriginJettyNum(ackHeader.GetIniTaSsn());
            response->SetRequestTassn(ackHeader.GetIniTaSsn());
            response->SetType(static_cast<TaOpcode>(ackHeader.GetTaOpcode()));
            response->SetRequestOpcode(ackHeader.GetTaOpcode() ==
                                               static_cast<uint8_t>(TaOpcode::TA_OPCODE_TRANSACTION_ACK)
                                           ? TaOpcode::TA_OPCODE_WRITE
                                           : TaOpcode::TA_OPCODE_READ);
            response->SetResponseBytes(copy->GetSize());
            response->SetNeedsTransactionResponse(false);
            response->SetSize(ackHeader.GetTaOpcode() ==
                                      static_cast<uint8_t>(TaOpcode::TA_OPCODE_TRANSACTION_ACK)
                                  ? 1
                                  : copy->GetSize());
            response->SetLogicalBytes(copy->GetSize());
            response->SetPayloadBytes(copy->GetSize());
            response->SetCarrierBytes(ackHeader.GetTaOpcode() ==
                                              static_cast<uint8_t>(TaOpcode::TA_OPCODE_TRANSACTION_ACK)
                                          ? 1
                                          : copy->GetSize());
            CompleteFromTaAck(localKey, ackHeader.GetIniTaSsn(), response);
            return true;
        }
    }

    UbCompactTransactionHeader taHeader;
    if (copy->RemoveHeader(taHeader) == 0)
    {
        return false;
    }
    UbCompactMAExtTah maHeader;
    if (copy->RemoveHeader(maHeader) == 0)
    {
        return false;
    }

    const auto taOpcode = static_cast<TaOpcode>(taHeader.GetTaOpcode());
    if (taOpcode != TaOpcode::TA_OPCODE_WRITE && taOpcode != TaOpcode::TA_OPCODE_READ)
    {
        return false;
    }

    UbFlowTag flowTag;
    packet->PeekPacketTag(flowTag);
    UbPacketTraceTag traceTag;
    packet->PeekPacketTag(traceTag);
    CtpRecvNotify(packet->GetUid(),
                  localKey,
                  taHeader.GetIniTaSsn(),
                  PacketType::PACKET,
                  copy->GetSize(),
                  flowTag.GetFlowId(),
                  traceTag);

    uint32_t receivePort = receivePortHint;
    if (receivePort == std::numeric_limits<uint32_t>::max())
    {
        receivePort = utils::Cna16ToPortId(cnaHeader.GetDcna());
    }

    Ptr<UbWqeSegment> request = BuildInboundTaOperation(taHeader,
                                                        maHeader,
                                                        taOpcode,
                                                        wireSrcNodeId,
                                                        wireDstNodeId,
                                                        localKey.vl,
                                                        copy->GetSize(),
                                                        flowTag.GetFlowId(),
                                                        flowTag.GetFlowSize());

    Ptr<UbWqeSegment> response = ProcessInboundTaRequest(request);
    if (response == nullptr)
    {
        return true;
    }
    ++localState.Rx().generatedResponses;

    const bool requestDestinationPortExplicit = (cnaHeader.GetDcna() & 0xF) != 0;
    if (receivePort < 15 &&
        (!RoutingTypeIsPerPacket(linkHeader.GetRoutingType()) || requestDestinationPortExplicit))
    {
        SetSourcePortHint(localKey, receivePort);
    }
    const bool requestSourcePortExplicit = (cnaHeader.GetScna() & 0xF) != 0;
    if (requestSourcePortExplicit)
    {
        const uint32_t sourcePort = utils::Cna16ToPortId(cnaHeader.GetScna());
        SetDestinationPortHint(localKey, sourcePort);
    }
    localState.Tx().routing.routingType = linkHeader.GetRoutingType();
    const bool sent = SendResponseSegment(response, localState);
    TryRetireTransactionState(localKey);
    return sent;
}

Ptr<UbWqeSegment>
UbCtpTransportService::ProcessInboundTaRequest(Ptr<UbWqeSegment> request) const
{
    if (request == nullptr)
    {
        return nullptr;
    }

    Ptr<UbController> controller = m_node == nullptr ? nullptr : m_node->GetObject<UbController>();
    Ptr<UbTransaction> transaction = controller == nullptr ? nullptr : controller->GetUbTransaction();
    if (transaction == nullptr)
    {
        return nullptr;
    }

    return transaction->ProcessInboundTaRequest(request);
}

Ptr<UbWqeSegment>
UbCtpTransportService::BuildInboundTaOperation(const UbCompactTransactionHeader& taHeader,
                                               const UbCompactMAExtTah& maHeader,
                                               TaOpcode taOpcode,
                                               uint32_t srcNodeId,
                                               uint32_t dstNodeId,
                                               uint32_t priority,
                                               uint32_t payloadBytes,
                                               uint32_t flowId,
                                               uint32_t flowSize)
{
    // UB Base Specification 2.0, 6.7.2: one CTP TP Packet carries one complete TA operation.
    const bool isReadRequest = taOpcode == TaOpcode::TA_OPCODE_READ;
    const uint32_t logicalBytes =
        static_cast<uint32_t>(64u << static_cast<uint32_t>(maHeader.GetLength()));

    Ptr<UbWqeSegment> operation = CreateObject<UbWqeSegment>();
    operation->SetSrc(srcNodeId);
    operation->SetDest(dstNodeId);
    operation->SetPriority(priority);
    operation->SetTaskId(flowId);
    operation->SetWqeSize(flowSize);
    operation->SetJettyNum(taHeader.GetIniTaSsn());
    operation->SetTaMsn(0);
    operation->SetTaSsn(taHeader.GetIniTaSsn());
    operation->SetSegmentKind(UbTransactionSegmentKind::REQUEST);
    operation->SetOriginJettyNum(taHeader.GetIniTaSsn());
    operation->SetRequestTassn(taHeader.GetIniTaSsn());
    operation->SetType(taOpcode);
    operation->SetRequestOpcode(taOpcode);
    operation->SetResponseBytes(isReadRequest ? logicalBytes : 0);
    operation->SetNeedsTransactionResponse(taOpcode == TaOpcode::TA_OPCODE_WRITE || isReadRequest);
    operation->SetSize(isReadRequest ? logicalBytes : payloadBytes);
    operation->SetLogicalBytes(isReadRequest ? logicalBytes : payloadBytes);
    operation->SetPayloadBytes(0);
    operation->SetCarrierBytes(isReadRequest ? 1 : payloadBytes);
    operation->SetPayloadBytes(payloadBytes);
    return operation;
}

void
UbCtpTransportService::RegisterQueueForTest(Ptr<UbCtpTxQueue> queue,
                                            uint32_t outPort,
                                            uint32_t priority)
{
    RegisterQueueIfNeeded(queue, outPort, priority);
}

void
UbCtpTransportService::CompleteFromTaAckForTest(const UbCtpEntityKey& key, uint32_t taSsn)
{
    UbCtpTransactionState& state = GetTransactionState(key);
    const auto outstanding = state.outstandingSegments.find(taSsn);
    const bool read = outstanding != state.outstandingSegments.end() &&
                      outstanding->second != nullptr &&
                      outstanding->second->GetType() == TaOpcode::TA_OPCODE_READ;
    Ptr<UbWqeSegment> response = CreateObject<UbWqeSegment>();
    response->SetSegmentKind(UbTransactionSegmentKind::RESPONSE);
    response->SetType(read ? TaOpcode::TA_OPCODE_READ_RESPONSE
                           : TaOpcode::TA_OPCODE_TRANSACTION_ACK);
    response->SetRequestOpcode(read ? TaOpcode::TA_OPCODE_READ : TaOpcode::TA_OPCODE_WRITE);
    response->SetRequestTassn(taSsn);
    response->SetTaSsn(static_cast<uint16_t>(taSsn));
    CompleteFromTaAck(key, taSsn, response);
}

void
UbCtpTransportService::RecordCnpForTest(const UbCtpEntityKey& key)
{
    UbCtpCongestionKey congestionKey{.dstNodeId = key.dstNodeId,
                                     .dstEntityId = key.dstEntityId,
                                     .vl = key.vl};
    ++m_congestionSignals[congestionKey];
}

uint32_t
UbCtpTransportService::GetQueueCount() const
{
    return static_cast<uint32_t>(m_queues.size());
}

uint32_t
UbCtpTransportService::GetCongestionStateCountForTest() const
{
    return static_cast<uint32_t>(m_congestionSignals.size());
}

const UbLifecycleResourceIdentity&
UbCtpTransportService::GetLifecycleOwnerForTest() const
{
    NS_ABORT_MSG_IF(m_lifecycleOwner.generation == 0,
                    "CTP lifecycle owner is not initialized");
    return m_lifecycleOwner;
}

uint32_t
UbCtpTransportService::SelectOutPort(UbCtpEntityState& state,
                                     const UbCtpEntityKey& routeKey,
                                     uint16_t loadBalanceSalt)
{
    NS_ABORT_MSG_IF(m_node == nullptr, "CTP send requires a source node");

    const uint32_t sourcePortHint = ResolveSourcePortHint(routeKey);
    if (sourcePortHint != UINT32_MAX)
    {
        const uint32_t outPort = sourcePortHint;
        NS_ABORT_MSG_IF(outPort >= m_node->GetNDevices(),
                        "CTP source port hint is outside the node device range");
        NS_ABORT_MSG_IF(DynamicCast<UbPort>(m_node->GetDevice(outPort)) == nullptr,
                        "CTP source port hint must reference a UB port");
        return outPort;
    }

    Ptr<UbSwitch> sw = m_node->GetObject<UbSwitch>();
    NS_ABORT_MSG_IF(sw == nullptr,
                    "CTP send requires a source port hint or a route to destination node");
    Ptr<UbRoutingProcess> routing = sw->GetRoutingProcess();
    NS_ABORT_MSG_IF(routing == nullptr,
                    "CTP send requires a source port hint or a route to destination node");

    const uint32_t dstPort = ResolveDestinationPortHint(routeKey);
    RoutingKey rtKey;
    rtKey.sip = utils::NodeIdToIp(m_node->GetId()).Get();
    rtKey.dip = dstPort == UINT32_MAX ? utils::NodeIdToIp(routeKey.dstNodeId).Get()
                                      : utils::NodeIdToIp(routeKey.dstNodeId, dstPort).Get();
    rtKey.sport = loadBalanceSalt;
    rtKey.dport = dstPort == UINT32_MAX ? 0 : static_cast<uint16_t>(dstPort);
    rtKey.priority = routeKey.vl;
    rtKey.routingType = state.Tx().routing.routingType;

    bool selectedShortestPath = false;
    const int outPort = dstPort == UINT32_MAX
                            ? routing->GetOutPort(rtKey, selectedShortestPath)
                            : routing->GetOutPortForEndpoint(rtKey, selectedShortestPath);
    // TODO: Source routing currently ignores selectedShortestPath. Propagate a non-shortest
    // selection into the transmit routing policy before building the packet header so a
    // downstream switch cannot consume the UB one-detour allowance again.
    NS_ABORT_MSG_IF(outPort < 0,
                    "CTP send requires a source port hint or a route to destination node");
    NS_ABORT_MSG_IF(static_cast<uint32_t>(outPort) >= m_node->GetNDevices(),
                    "CTP route selected an outPort outside the node device range");
    NS_ABORT_MSG_IF(DynamicCast<UbPort>(m_node->GetDevice(static_cast<uint32_t>(outPort))) ==
                        nullptr,
                    "CTP route selected a non-UB source port");
    return static_cast<uint32_t>(outPort);
}

uint16_t
UbCtpTransportService::NextLoadBalanceSalt(UbCtpEntityState& state, const UbCtpEntityKey& routeKey)
{
    if (!RoutingTypeIsPerPacket(state.Tx().routing.routingType))
    {
        return EntityLoadBalanceSalt(m_node->GetId(), routeKey);
    }
    return static_cast<uint16_t>(state.Tx().routing.nextSprayIndex++);
}

uint32_t
UbCtpTransportService::ResolveDestinationPortHint(const UbCtpEntityKey& key) const
{
    auto it = m_dstPortHints.find(key);
    if (it != m_dstPortHints.end())
    {
        return it->second;
    }

    const UbCtpEntityKey sourceGroupKey = MakeSourceGroupKeyForRequest(key);
    it = m_dstPortHints.find(sourceGroupKey);
    if (it != m_dstPortHints.end())
    {
        return it->second;
    }
    return UINT32_MAX;
}

uint32_t
UbCtpTransportService::ResolveSourcePortHint(const UbCtpEntityKey& key) const
{
    auto it = m_srcPortHints.find(key);
    if (it != m_srcPortHints.end())
    {
        return it->second;
    }
    return UINT32_MAX;
}

void
UbCtpTransportService::RegisterQueueIfNeeded(Ptr<UbCtpTxQueue> queue,
                                             uint32_t outPort,
                                             uint32_t priority)
{
    NS_ABORT_MSG_IF(queue == nullptr, "CTP queue registration requires a queue");
    NS_ABORT_MSG_IF(m_node == nullptr, "CTP queue registration requires a source node");

    if (m_registeredQueues.find(queue) != m_registeredQueues.end())
    {
        return;
    }

    Ptr<UbSwitch> sw = m_node->GetObject<UbSwitch>();
    NS_ABORT_MSG_IF(sw == nullptr, "CTP queue registration requires UbSwitch");
    sw->RegisterTpWithAllocator(queue, outPort, priority);
    m_registeredQueues.insert(queue);
}

bool
UbCtpTransportService::SendResponseSegment(Ptr<UbWqeSegment> response, UbCtpEntityState& state)
{
    NS_ABORT_MSG_IF(response == nullptr, "CTP response send requires WQE segment");
    if (response->GetType() != TaOpcode::TA_OPCODE_TRANSACTION_ACK &&
        response->GetBytesLeft() > GetTransactionConstraints().maxSegmentBytes)
    {
        return false;
    }

    const UbCtpEntityKey& key = state.GetKey();
    uint32_t taSsn = response->GetRequestTassn();
    if (response->GetType() == TaOpcode::TA_OPCODE_TRANSACTION_ACK)
    {
        const uint16_t lbSalt = NextLoadBalanceSalt(state, key);
        const uint32_t outPort = SelectOutPort(state, key, lbSalt);
        Ptr<Packet> packet = BuildDataPacketWithTaSsn(response,
                                                      state,
                                                      key,
                                                      taSsn,
                                                      0,
                                                      static_cast<uint8_t>(lbSalt & 0xff));
        Ptr<UbCtpTxQueue> queue = GetOrCreateQueue(key, outPort, key.vl);
        RegisterQueueIfNeeded(queue, outPort, key.vl);
        queue->EnqueueAck(packet);

        Ptr<UbPort> port = DynamicCast<UbPort>(m_node->GetDevice(outPort));
        NS_ABORT_MSG_IF(port == nullptr, "CTP response send requires valid out port");
        port->TriggerTransmit();
        return true;
    }

    if (!response->IsSentCompleted())
    {
        const uint32_t progressBytes = static_cast<uint32_t>(response->GetBytesLeft());
        const uint16_t lbSalt = NextLoadBalanceSalt(state, key);
        const uint32_t outPort = SelectOutPort(state, key, lbSalt);
        Ptr<Packet> packet = BuildDataPacketWithTaSsn(response,
                                                      state,
                                                      key,
                                                      taSsn,
                                                      progressBytes,
                                                      static_cast<uint8_t>(lbSalt & 0xff));
        Ptr<UbCtpTxQueue> queue = GetOrCreateQueue(key, outPort, key.vl);
        RegisterQueueIfNeeded(queue, outPort, key.vl);
        queue->EnqueueData(packet);
        response->UpdateSentBytes(progressBytes);

        Ptr<UbPort> port = DynamicCast<UbPort>(m_node->GetDevice(outPort));
        NS_ABORT_MSG_IF(port == nullptr, "CTP response send requires valid out port");
        port->TriggerTransmit();
        ++taSsn;
    }
    return true;
}

void
UbCtpTransportService::CompleteFromTaAck(const UbCtpEntityKey& key,
                                         uint32_t taSsn,
                                         Ptr<UbWqeSegment> response)
{
    if (ShouldDelayTaAck(key, taSsn))
    {
        GetOrCreateEntityState(key);
        TraceWindowEvent(key, "taack-delay", taSsn, *GetOrCreateTransactionContext(key));
        const uint64_t token = m_delayedTaAckEvents.Track(m_lifecycleOwner);
        GetTransactionOwner()->RetainCtpCompletionEvent(key);
        EventId event = Simulator::Schedule(m_delayTaAckTime,
                                            &UbCtpTransportService::RunTrackedCompleteFromTaAck,
                                            this,
                                            key,
                                            taSsn,
                                            response,
                                            token);
        m_delayedTaAckEvents.Attach(
            m_lifecycleOwner,
            token,
            event,
            MakeCallback(&UbCtpTransportService::CancelTrackedCompleteFromTaAck, this).Bind(key));
        return;
    }
    CompleteFromTaAckNow(key, taSsn, response);
    TryRetireTransactionState(key);
    const UbCtpEntityKey sourceGroupKey = MakeSourceGroupKeyForRequest(key);
    if (!(sourceGroupKey == key))
    {
        TryRetireTransactionState(sourceGroupKey);
    }
}

void
UbCtpTransportService::CancelTrackedCompleteFromTaAck(UbCtpEntityKey key)
{
    GetTransactionOwner()->ReleaseCtpCompletionEvent(key);
    TryRetireTransactionState(key);
}

void
UbCtpTransportService::RunTrackedCompleteFromTaAck(UbCtpEntityKey key,
                                                   uint32_t taSsn,
                                                   Ptr<UbWqeSegment> response,
                                                   uint64_t token)
{
    if (!m_delayedTaAckEvents.Begin(m_lifecycleOwner, token))
    {
        return;
    }
    CompleteFromTaAckNow(key, taSsn, response);
    m_delayedTaAckEvents.Resolve(m_lifecycleOwner, token);
    GetTransactionOwner()->ReleaseCtpCompletionEvent(key);
    TryRetireTransactionState(key);
    const UbCtpEntityKey sourceGroupKey = MakeSourceGroupKeyForRequest(key);
    if (!(sourceGroupKey == key))
    {
        TryRetireTransactionState(sourceGroupKey);
    }
}

void
UbCtpTransportService::TryRetireTransactionState(const UbCtpEntityKey& key)
{
    GetTransactionOwner()->TryRetireCtpTransactionState(key);
}

void
UbCtpTransportService::CompleteFromTaAckNow(const UbCtpEntityKey& key,
                                            uint32_t taSsn,
                                            Ptr<UbWqeSegment> response)
{
    UbCtpEntityState* state = &GetOrCreateEntityState(key);
    UbCtpTransactionState* transactionState = &GetTransactionState(key);
    GetOrCreateTransactionContext(key);
    taSsn = ResolveWireTaSsn(*transactionState, static_cast<uint16_t>(taSsn));
    auto outstandingIt = FindOutstandingSegment(*transactionState, taSsn);
    const UbCtpEntityKey sourceGroupKey = MakeSourceGroupKeyForRequest(key);
    const bool keyIsSourceGroup = sourceGroupKey == key;
    if (outstandingIt == transactionState->outstandingSegments.end() && !keyIsSourceGroup)
    {
        auto sourceGroupIt = m_entityStates.find(sourceGroupKey);
        UbCtpTransactionState* sourceGroupState =
            GetTransactionOwner()->FindCtpTransactionState(sourceGroupKey);
        if (sourceGroupIt != m_entityStates.end() && sourceGroupState != nullptr)
        {
            GetOrCreateTransactionContext(sourceGroupKey);
            const uint32_t sourceGroupTaSsn =
                ResolveWireTaSsn(*sourceGroupState, static_cast<uint16_t>(taSsn));
            auto sourceGroupOutstandingIt =
                FindOutstandingSegment(*sourceGroupState, sourceGroupTaSsn);
            if (sourceGroupOutstandingIt != sourceGroupState->outstandingSegments.end())
            {
                state = &sourceGroupIt->second;
                transactionState = sourceGroupState;
                taSsn = sourceGroupTaSsn;
                outstandingIt = sourceGroupOutstandingIt;
            }
        }
    }

    Ptr<UbCtpTransactionContext> context = transactionState->context;
    const uint32_t oldCompleteUna = context->GetCompleteUna();
    if (outstandingIt == transactionState->outstandingSegments.end())
    {
        context->MarkTaAck(taSsn);
        return;
    }

    context->MarkTaAck(taSsn);
    Ptr<UbWqeSegment> completedSegment = outstandingIt->second;
    LastPacketACKsNotify(completedSegment->GetTaskId(),
                         state->GetKey(),
                         taSsn,
                         completedSegment->GetSize(),
                         completedSegment->GetType());
    transactionState->outstandingSegments.erase(outstandingIt);
    auto segmentCountIt = transactionState->segmentOutstandingPackets.find(completedSegment);
    if (segmentCountIt != transactionState->segmentOutstandingPackets.end())
    {
        if (segmentCountIt->second > 1)
        {
            --segmentCountIt->second;
        }
        else
        {
            transactionState->segmentOutstandingPackets.erase(segmentCountIt);
        }
    }
    Ptr<UbController> controller = m_node == nullptr ? nullptr : m_node->GetObject<UbController>();
    Ptr<UbTransaction> transaction = controller == nullptr ? nullptr : controller->GetUbTransaction();
    const bool segmentComplete = completedSegment->IsSentCompleted() &&
                                 transactionState->segmentOutstandingPackets.find(
                                     completedSegment) ==
                                     transactionState->segmentOutstandingPackets.end();
    if (transaction != nullptr && segmentComplete)
    {
        Ptr<UbWqeSegment> completion = response == nullptr ? completedSegment : response;
        completion->SetTaskId(completedSegment->GetTaskId());
        completion->SetWqeSize(completedSegment->GetWqeSize());
        completion->SetJettyNum(completedSegment->GetJettyNum());
        completion->SetOriginJettyNum(completedSegment->GetOriginJettyNum());
        completion->SetTaSsn(completedSegment->GetTaSsn());
        completion->SetRequestTassn(completedSegment->GetTaSsn());
        completion->SetRequestOpcode(completedSegment->GetType());
        if (completedSegment->HasTransactionCorrelation())
        {
            completion->SetTransactionCorrelation(completedSegment->GetTransactionCorrelation());
        }
        transaction->ProcessInboundTaResponse(completion);
    }
    if (context->GetCompleteUna() != oldCompleteUna)
    {
        DrainJettys(*state);
    }
}

void
UbCtpTransportService::DoDispose()
{
    if (m_lifecycleOwner.generation != 0)
    {
        const std::vector<Callback<void>> cleanups =
            m_delayedTaAckEvents.CancelPending(m_lifecycleOwner);
        for (const Callback<void>& cleanup : cleanups)
        {
            cleanup();
        }
    }
    if (m_delayedTaAckEvents.IsEmpty())
    {
        m_delayedTaAckEvents.Clear();
    }

    Ptr<UbSwitch> sw = m_node == nullptr ? nullptr : m_node->GetObject<UbSwitch>();
    for (const auto& [key, queue] : m_queues)
    {
        (void)key;
        if (queue == nullptr)
        {
            continue;
        }
        if (m_registeredQueues.find(queue) != m_registeredQueues.end() && sw != nullptr &&
            sw->GetAllocator() != nullptr)
        {
            sw->RemoveTpFromAllocator(queue);
        }
    }
    m_registeredQueues.clear();
    m_queues.clear();
    m_node = nullptr;
    Object::DoDispose();
}

std::map<uint32_t, Ptr<UbWqeSegment>>::iterator
UbCtpTransportService::FindOutstandingSegment(UbCtpTransactionState& state, uint32_t taSsn)
{
    return state.outstandingSegments.find(taSsn);
}

uint32_t
UbCtpTransportService::ResolveWireTaSsn(UbCtpTransactionState& state, uint16_t wireTaSsn) const
{
    const auto& outstandingSegments = state.outstandingSegments;
    const uint32_t completeUna = state.context->GetCompleteUna();
    const uint32_t sendNext = state.context->GetSendNext();
    const uint32_t base = completeUna & ~0xffffu;
    std::vector<uint32_t> candidates;
    candidates.push_back(base | wireTaSsn);
    if (base >= 0x10000u)
    {
        candidates.push_back((base - 0x10000u) | wireTaSsn);
    }
    if (base <= std::numeric_limits<uint32_t>::max() - 0x10000u)
    {
        candidates.push_back((base + 0x10000u) | wireTaSsn);
    }

    for (uint32_t candidate : candidates)
    {
        if (candidate >= completeUna && candidate < sendNext &&
            outstandingSegments.find(candidate) != outstandingSegments.end())
        {
            return candidate;
        }
    }
    for (uint32_t candidate : candidates)
    {
        if (candidate >= completeUna && candidate < sendNext)
        {
            return candidate;
        }
    }
    return wireTaSsn;
}

uint32_t
UbCtpTransportService::GetQueuedPacketCountForTest(const UbCtpEntityKey& key) const
{
    uint32_t count = 0;
    for (const auto& [queueKey, queue] : m_queues)
    {
        if (std::get<0>(queueKey) == key && queue != nullptr)
        {
            count += queue->GetQueuedPacketCountForTest();
        }
    }
    return count;
}

uint32_t
UbCtpTransportService::GetQueuedPacketCountForTest(const UbCtpEntityKey& key,
                                                   uint32_t outPort,
                                                   uint32_t priority) const
{
    QueueKey queueKey = std::make_tuple(key, outPort, priority);
    auto it = m_queues.find(queueKey);
    if (it == m_queues.end() || it->second == nullptr)
    {
        return 0;
    }
    return it->second->GetQueuedPacketCountForTest();
}

uint32_t
UbCtpTransportService::GetRegisteredQueueCountForTest() const
{
    return static_cast<uint32_t>(m_registeredQueues.size());
}

bool
UbCtpTransportService::IsPacketTraceEnabledForTest() const
{
    return m_pktTraceEnabled;
}

void
UbCtpTransportService::FirstPacketSendsNotify(uint32_t taskId,
                                              const UbCtpEntityKey& key,
                                              uint32_t taSsn,
                                              uint32_t outPort,
                                              uint32_t payloadBytes,
                                              TaOpcode opcode)
{
    m_traceFirstPacketSendsNotify(m_node == nullptr ? UINT32_MAX : m_node->GetId(),
                                  taskId,
                                  m_node == nullptr ? UINT32_MAX : m_node->GetId(),
                                  key.dstNodeId,
                                  key.srcEntityId,
                                  key.dstEntityId,
                                  static_cast<uint32_t>(key.vl),
                                  taSsn,
                                  outPort,
                                  payloadBytes,
                                  static_cast<uint32_t>(opcode));
}

void
UbCtpTransportService::LastPacketACKsNotify(uint32_t taskId,
                                            const UbCtpEntityKey& key,
                                            uint32_t taSsn,
                                            uint32_t payloadBytes,
                                            TaOpcode opcode)
{
    m_traceLastPacketACKsNotify(m_node == nullptr ? UINT32_MAX : m_node->GetId(),
                                taskId,
                                m_node == nullptr ? UINT32_MAX : m_node->GetId(),
                                key.dstNodeId,
                                key.srcEntityId,
                                key.dstEntityId,
                                static_cast<uint32_t>(key.vl),
                                taSsn,
                                UINT32_MAX,
                                payloadBytes,
                                static_cast<uint32_t>(opcode));
}

void
UbCtpTransportService::CtpRecvNotify(uint32_t packetUid,
                                     const UbCtpEntityKey& key,
                                     uint32_t taSsn,
                                     PacketType type,
                                     uint32_t size,
                                     uint32_t taskId,
                                     UbPacketTraceTag traceTag)
{
    m_ctpRecvNotify(packetUid,
                    key.dstNodeId,
                    m_node == nullptr ? UINT32_MAX : m_node->GetId(),
                    key.dstEntityId,
                    key.srcEntityId,
                    static_cast<uint32_t>(key.vl),
                    taSsn,
                    type,
                    size,
                    taskId,
                    traceTag);
}

bool
UbCtpTransportService::ShouldTraceWindow(const UbCtpEntityKey& key) const
{
    if (!m_windowTraceEnabled)
    {
        return false;
    }
    if (m_windowTraceSrcNode != std::numeric_limits<uint32_t>::max() &&
        (m_node == nullptr || m_node->GetId() != m_windowTraceSrcNode))
    {
        return false;
    }
    if (m_windowTraceDstNode != std::numeric_limits<uint32_t>::max() &&
        key.dstNodeId != m_windowTraceDstNode)
    {
        return false;
    }
    return m_node != nullptr;
}

bool
UbCtpTransportService::ShouldDelayTaAck(const UbCtpEntityKey& key, uint32_t taSsn) const
{
    if (m_delayTaAckSrcNode == std::numeric_limits<uint32_t>::max() ||
        m_delayTaAckTime <= NanoSeconds(0))
    {
        return false;
    }
    const bool exactMatch =
        m_delayTaAckSequence != std::numeric_limits<uint32_t>::max() &&
        taSsn == m_delayTaAckSequence;
    const bool moduloMatch =
        m_delayTaAckModulo != 0 &&
        taSsn % m_delayTaAckModulo == m_delayTaAckRemainder % m_delayTaAckModulo;
    if (!exactMatch && !moduloMatch)
    {
        return false;
    }
    if (m_node == nullptr || m_node->GetId() != m_delayTaAckSrcNode)
    {
        return false;
    }
    if (m_delayTaAckDstNode != std::numeric_limits<uint32_t>::max() &&
        key.dstNodeId != m_delayTaAckDstNode)
    {
        return false;
    }
    return true;
}

void
UbCtpTransportService::TraceWindowEvent(const UbCtpEntityKey& key,
                                        const std::string& event,
                                        uint32_t taSsn,
                                        const UbCtpTransactionContext& context,
                                        uint32_t outPort) const
{
    if (!ShouldTraceWindow(key))
    {
        return;
    }

    std::ostringstream oss;
    oss << "CTP WINDOW"
        << " event: " << event
        << " src: " << m_node->GetId()
        << " srcEntity: " << key.srcEntityId
        << " dst: " << key.dstNodeId
        << " dstEntity: " << key.dstEntityId
        << " vl: " << static_cast<uint32_t>(key.vl)
        << " taSsn: " << taSsn
        << " sendNext: " << context.GetSendNext()
        << " completeUna: " << context.GetCompleteUna()
        << " outstanding: " << context.GetOutstandingCount();
    if (outPort != std::numeric_limits<uint32_t>::max())
    {
        oss << " outPort: " << outPort;
    }
    NS_LOG_INFO(oss.str());
}

void
UbCtpTransportService::TraceDetailEvent(const UbCtpEntityKey& key,
                                        const std::string& event,
                                        uint32_t taSsn,
                                        uint32_t bytes,
                                        uint32_t expectedBytes,
                                        uint32_t flowBytes,
                                        uint32_t flowSize,
                                        uint32_t outPort) const
{
    if (!ShouldTraceWindow(key))
    {
        return;
    }

    std::ostringstream oss;
    oss << "CTP DETAIL"
        << " event: " << event
        << " src: " << m_node->GetId()
        << " srcEntity: " << key.srcEntityId
        << " dst: " << key.dstNodeId
        << " dstEntity: " << key.dstEntityId
        << " vl: " << static_cast<uint32_t>(key.vl)
        << " taSsn: " << taSsn;
    if (bytes != UINT32_MAX)
    {
        oss << " bytes: " << bytes;
    }
    if (expectedBytes != UINT32_MAX)
    {
        oss << " expectedBytes: " << expectedBytes;
    }
    if (flowBytes != UINT32_MAX)
    {
        oss << " flowBytes: " << flowBytes;
    }
    if (flowSize != UINT32_MAX)
    {
        oss << " flowSize: " << flowSize;
    }
    if (outPort != UINT32_MAX)
    {
        oss << " outPort: " << outPort;
    }
    NS_LOG_INFO(oss.str());
}

} // namespace ns3
