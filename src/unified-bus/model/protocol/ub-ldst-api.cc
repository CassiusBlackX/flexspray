// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include "ns3/ub-datatype.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-ldst-api.h"
#include "ns3/ub-ldst-endpoint.h"

#include <limits>
#include <memory>

#include <algorithm>

namespace ns3 {
NS_LOG_COMPONENT_DEFINE("UbLdstApi");

NS_OBJECT_ENSURE_REGISTERED(UbLdstApi);

TypeId UbLdstApi::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbLdstApi")
                            .SetParent<Object>()
                            .SetGroupName("UnifiedBus")
                            .AddAttribute("RoutingType",
                                          "UB routing type used by LDST packets.",
                                          EnumValue(RoutingType::PER_FLOW_SHORTEST_PATHS),
                                          MakeEnumAccessor<RoutingType>(&UbLdstApi::m_routingType),
                                          MakeEnumChecker(RoutingType::PER_FLOW_ALL_PATHS,
                                                          "PER_FLOW_ALL_PATHS",
                                                          RoutingType::PER_PACKET_ALL_PATHS,
                                                          "PER_PACKET_ALL_PATHS",
                                                          RoutingType::PER_FLOW_SHORTEST_PATHS,
                                                          "PER_FLOW_SHORTEST_PATHS",
                                                          RoutingType::PER_PACKET_SHORTEST_PATHS,
                                                          "PER_PACKET_SHORTEST_PATHS"))
                            .AddTraceSource("LdstRecvNotify",
                                            "Fires on Ldst data or ACK reception (provides info and trace tags).",
                                            MakeTraceSourceAccessor(&UbLdstApi::m_ldstRecvNotify),
                                            "ns3::UbLdstApi::LdstRecvNotify");

    return tid;
}

UbLdstApi::UbLdstApi()
{
    BooleanValue val;
    if (GlobalValue::GetValueByNameFailSafe("UB_RECORD_PKT_TRACE", val)) {
        GlobalValue::GetValueByName("UB_RECORD_PKT_TRACE", val);
        m_pktTraceEnabled = val.Get();
    } else {
        m_pktTraceEnabled = false;
    }
}

UbLdstApi::~UbLdstApi()
{
}

void UbLdstApi::SetNodeId(uint32_t nodeId)
{
    m_nodeId = nodeId;
    ValidateRoutingConfiguration();
}

void
UbLdstApi::ValidateRoutingType(RoutingType routingType) const
{
    if (m_nodeId >= NodeList::GetNNodes())
    {
        return;
    }
    Ptr<UbSwitch> sw = NodeList::GetNode(m_nodeId)->GetObject<UbSwitch>();
    if (sw == nullptr || sw->GetRoutingProcess() == nullptr)
    {
        return;
    }
    sw->GetRoutingProcess()->ValidateRoutingType(routingType, "LDST routing policy");
}

void
UbLdstApi::ValidateRoutingConfiguration() const
{
    ValidateRoutingType(m_routingType);
}

void
UbLdstApi::SetRemoteRequestProcessor(UbLdstRemoteRequestProcessor processor)
{
    m_remoteRequestProcessor = processor;
}

void
UbLdstApi::ClearRemoteRequestProcessor()
{
    m_remoteRequestProcessor = UbLdstRemoteRequestProcessor();
}

bool
UbLdstApi::HasRemoteRequestProcessor() const
{
    return !m_remoteRequestProcessor.IsNull();
}

void UbLdstApi::LdstProcess(Ptr<UbLdstTaskSegment> taskSegment)
{
    // genpacket
    auto packet = GenDataPacket(taskSegment);
    // sendpacket
    SendPacket(taskSegment, packet);
}

int
UbLdstApi::SelectOutPort(RoutingKey routingKey,
                         bool hasSourcePort,
                         uint32_t sourcePort,
                         bool hasDestinationPort) const
{
    auto node = NodeList::GetNode(m_nodeId);
    auto sw = node->GetObject<UbSwitch>();
    NS_ABORT_MSG_IF(sw == nullptr || sw->GetRoutingProcess() == nullptr,
                    "LDST routing requires a source switch");

    if (!hasSourcePort && !hasDestinationPort)
    {
        bool selectedShortestPath = false;
        return sw->GetRoutingProcess()->GetOutPort(routingKey, selectedShortestPath);
    }

    if (hasSourcePort)
    {
        NS_ABORT_MSG_IF(sourcePort >= node->GetNDevices() ||
                            DynamicCast<UbPort>(node->GetDevice(sourcePort)) == nullptr,
                        "Requested LDST source endpoint port is unavailable on node "
                            << m_nodeId << ": " << sourcePort);
    }
    Ptr<UbRoutingProcess> routing = sw->GetRoutingProcess();
    std::vector<uint16_t> shortestCandidates;
    std::vector<uint16_t> nonShortestCandidates;
    uint32_t routeDip = routingKey.dip;
    const bool handledByRule = hasDestinationPort
                                   ? routing->TryGetRuleCandidatesExact(routeDip,
                                                                        shortestCandidates,
                                                                        nonShortestCandidates)
                                   : routing->TryGetRuleCandidatesWithPrimaryFallback(
                                         routeDip,
                                         UINT16_MAX,
                                         shortestCandidates,
                                         nonShortestCandidates);
    if (!handledByRule)
    {
        if (hasDestinationPort)
        {
            routing->GetShortestOutPorts(routeDip, shortestCandidates);
            if (!RoutingTypeUsesShortestPaths(routingKey.routingType))
            {
                routing->GetOtherOutPorts(routeDip, nonShortestCandidates);
            }
        }
        else
        {
            routing->GetShortestCandidates(routeDip, UINT16_MAX, shortestCandidates);
            if (!RoutingTypeUsesShortestPaths(routingKey.routingType))
            {
                routing->GetNonShortestCandidates(routeDip, UINT16_MAX, nonShortestCandidates);
            }
        }
    }
    if (hasSourcePort)
    {
        const auto isNotRequestedPort = [sourcePort](uint16_t candidate) {
            return candidate != sourcePort;
        };
        shortestCandidates.erase(std::remove_if(shortestCandidates.begin(),
                                                shortestCandidates.end(),
                                                isNotRequestedPort),
                                 shortestCandidates.end());
        nonShortestCandidates.erase(std::remove_if(nonShortestCandidates.begin(),
                                                   nonShortestCandidates.end(),
                                                   isNotRequestedPort),
                                    nonShortestCandidates.end());
    }
    if (RoutingTypeUsesShortestPaths(routingKey.routingType))
    {
        nonShortestCandidates.clear();
    }
    NS_ABORT_MSG_IF(shortestCandidates.empty() && nonShortestCandidates.empty(),
                    "Requested LDST source endpoint port is unreachable for destination "
                        << Ipv4Address(routingKey.dip));
    bool selectedShortestPath = false;
    return routing->SelectOutPort(routingKey,
                                  shortestCandidates,
                                  nonShortestCandidates,
                                  selectedShortestPath);
}

void
UbLdstApi::SendPacket(Ptr<UbLdstTaskSegment> taskSegment, Ptr<Packet> packet)
{
    RoutingKey rtKey;
    rtKey.sip = taskSegment->HasSrcPortId()
                    ? utils::NodeIdToIp(taskSegment->GetSrc(), taskSegment->GetSrcPortId()).Get()
                    : utils::NodeIdToIp(taskSegment->GetSrc()).Get();
    rtKey.dip = taskSegment->HasDstPortId()
                    ? utils::NodeIdToIp(taskSegment->GetDest(), taskSegment->GetDstPortId()).Get()
                    : utils::NodeIdToIp(taskSegment->GetDest()).Get();
    rtKey.sport = m_lbHashSalt;
    rtKey.dport =
        taskSegment->HasDstPortId() ? static_cast<uint16_t>(taskSegment->GetDstPortId()) : 0;
    rtKey.priority = taskSegment->GetPriority();
    rtKey.routingType = m_routingType;

    auto node = NodeList::GetNode(m_nodeId);
    auto sw = node->GetObject<UbSwitch>();
    int outPort = SelectOutPort(rtKey,
                                taskSegment->HasSrcPortId(),
                                taskSegment->GetSrcPortId(),
                                taskSegment->HasDstPortId());
    // TODO: Source injection currently ignores selectedShortestPath. If this lookup selects a
    // non-shortest port, mark the packet shortest-only before enqueueing it so a downstream
    // switch cannot consume the UB one-detour allowance again.
    if (outPort < 0) {
        // Route failed
        NS_ASSERT_MSG(0, "The route cannot be found");
    }
    uint16_t destPort = outPort;
    sw->PushPacketToVoq(packet, destPort, taskSegment->GetPriority(), destPort);
    Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(destPort));
    Simulator::ScheduleNow(&UbPort::TriggerTransmit, port);
}

Ptr<Packet> UbLdstApi::GenDataPacket(Ptr<UbLdstTaskSegment> taskSegment)
{
    // Store/load request: DLH cNTH cTAH(0x03/0x06) [cMAETAH] Payload
    UbCompactMAExtTah cMAETah;
    UbCompactTransactionHeader cTaHeader;
    UbCna16NetworkHeader cna16NetworkHeader;
    uint32_t length = taskSegment->GetLength();
    uint32_t payloadSize = 0;
    uint32_t dataSize = taskSegment->PeekNextDataSize();
    // add cTAH (Compact Transaction Header)
    if (taskSegment->GetType() == UbMemOperationType::STORE) {
        cTaHeader.SetTaOpcode(TaOpcode::TA_OPCODE_WRITE);
        payloadSize = dataSize;
    } else if (taskSegment->GetType() == UbMemOperationType::LOAD) {
        cTaHeader.SetTaOpcode(TaOpcode::TA_OPCODE_READ);
        payloadSize = taskSegment->GetPacketSize();
    }

    if (RoutingTypeIsPerPacket(m_routingType)) {
        if (m_lbHashSalt == MAX_LB) {
            m_lbHashSalt = MIN_LB;
        } else {
            m_lbHashSalt++;
        }
    }
    Ptr<Packet> packet = Create<Packet>(payloadSize);
    taskSegment->UpdateSentBytes(dataSize);
    // Gen Headers
    cMAETah.SetLength((uint8_t)length);
    cTaHeader.SetIniTaSsn(taskSegment->GetTaskSegmentId()); // taskid
    NS_ABORT_MSG_IF(taskSegment->HasSrcPortId() && taskSegment->GetSrcPortId() >= 15,
                    "LDST source endpoint port cannot be encoded as CNA16: "
                        << taskSegment->GetSrcPortId());
    NS_ABORT_MSG_IF(taskSegment->HasDstPortId() && taskSegment->GetDstPortId() >= 15,
                    "LDST destination endpoint port cannot be encoded as CNA16: "
                        << taskSegment->GetDstPortId());
    uint16_t scna = static_cast<uint16_t>(taskSegment->HasSrcPortId()
                                              ? utils::NodeIdToCna16(taskSegment->GetSrc(),
                                                                     taskSegment->GetSrcPortId())
                                              : utils::NodeIdToCna16(taskSegment->GetSrc()));
    cna16NetworkHeader.SetScna(scna);
    uint16_t dcna = static_cast<uint16_t>(taskSegment->HasDstPortId()
                                              ? utils::NodeIdToCna16(taskSegment->GetDest(),
                                                                     taskSegment->GetDstPortId())
                                              : utils::NodeIdToCna16(taskSegment->GetDest()));
    cna16NetworkHeader.SetDcna(dcna);
    cna16NetworkHeader.SetLb(m_lbHashSalt);
    cna16NetworkHeader.SetServiceLevel(taskSegment->GetPriority());

    packet->AddHeader(cMAETah);
    packet->AddHeader(cTaHeader);
    packet->AddHeader(cna16NetworkHeader);

    // add dl header
    UbDataLink::AddPacketHeader(packet,
                                false,
                                false,
                                taskSegment->GetPriority(),
                                taskSegment->GetPriority(),
                                m_routingType,
                                UbDatalinkHeaderConfig::PACKET_UB_MEM);
    UbFlowTag flowTag(taskSegment->GetTaskId(), taskSegment->GetSize());
    packet->AddPacketTag(flowTag);
    NS_LOG_DEBUG("[UbLdstApi GenDataPacket] packetUid: " << packet->GetUid() << " payload size:" << payloadSize);
    return packet;
}

bool
UbLdstApi::BuildRemoteRequestContext(Ptr<Packet> packet,
                                     uint32_t ingressPort,
                                     UbDatalinkPacketHeader& linkPacketHeader,
                                     UbCna16NetworkHeader& cna16NetworkHeader,
                                     UbCompactTransactionHeader& cTaHeader,
                                     UbLdstRemoteRequestContext& context)
{
    if (packet == nullptr)
    {
        return false;
    }

    packet->RemoveHeader(linkPacketHeader);
    packet->RemoveHeader(cna16NetworkHeader);
    packet->RemoveHeader(cTaHeader);

    const uint32_t tracePacketSize = packet->GetSize();
    UbCompactMAExtTah cMAETah;
    packet->RemoveHeader(cMAETah);

    const uint8_t opcode = cTaHeader.GetTaOpcode();
    if (opcode != static_cast<uint8_t>(TaOpcode::TA_OPCODE_WRITE) &&
        opcode != static_cast<uint8_t>(TaOpcode::TA_OPCODE_READ))
    {
        NS_ASSERT_MSG(0, "packet Ta Op code is wrong!");
    }

    context.requestId = cTaHeader.GetIniTaSsn();
    context.srcNode = utils::Cna16ToNodeId(cna16NetworkHeader.GetScna());
    context.destNode = utils::Cna16ToNodeId(cna16NetworkHeader.GetDcna());
    context.type = opcode == static_cast<uint8_t>(TaOpcode::TA_OPCODE_READ)
                       ? UbMemOperationType::LOAD
                       : UbMemOperationType::STORE;
    context.requestBytes = packet->GetSize();
    context.responseBytes =
        context.type == UbMemOperationType::LOAD ? 64u * (1u << cMAETah.GetLength()) : 0u;
    context.priority = linkPacketHeader.GetPacketVL();
    const uint64_t paddr = cMAETah.GetVirtualAddress();
    context.hasPaddr = paddr != 0;
    context.paddr = paddr;
    context.ingressPort = ingressPort;
    context.creditTargetVl = linkPacketHeader.GetCreditTargetVL();
    context.packetVl = linkPacketHeader.GetPacketVL();
    context.usePacketSpray = RoutingTypeIsPerPacket(linkPacketHeader.GetRoutingType());
    context.useShortestPaths = RoutingTypeUsesShortestPaths(linkPacketHeader.GetRoutingType());
    context.lb = cna16NetworkHeader.GetLb();
    context.packetUid = packet->GetUid();
    context.packetSize = tracePacketSize;
    context.payload = packet->GetSize() > 0 ? packet->Copy() : nullptr;

    UbFlowTag flowTag;
    if (packet->PeekPacketTag(flowTag))
    {
        context.flowId = flowTag.GetFlowId();
    }
    packet->PeekPacketTag(context.traceTag);
    return true;
}

void
UbLdstApi::SendRemoteResponse(const UbLdstRemoteRequestContext& context,
                              TaOpcode responseOpcode,
                              uint32_t payloadBytes,
                              Ptr<Packet> payload)
{
    Ptr<Packet> ackp = payload != nullptr ? payload->Copy() : Create<Packet>(payloadBytes);
    NS_ABORT_MSG_IF(ackp->GetSize() != payloadBytes, "LDST response payload size mismatch");

    UbCompactAckTransactionHeader caTaHeader;
    caTaHeader.SetTaOpcode(responseOpcode);
    caTaHeader.SetIniTaSsn(context.requestId);

    UbCna16NetworkHeader cna16NetworkHeader;
    cna16NetworkHeader.SetScna(static_cast<uint16_t>(utils::NodeIdToCna16(context.destNode)));
    cna16NetworkHeader.SetDcna(static_cast<uint16_t>(utils::NodeIdToCna16(context.srcNode)));
    cna16NetworkHeader.SetLb(context.lb);
    cna16NetworkHeader.SetServiceLevel(context.priority);

    ackp->AddHeader(caTaHeader);
    ackp->AddHeader(cna16NetworkHeader);
    const RoutingType responseRoutingType =
        MakeRoutingType(context.usePacketSpray, context.useShortestPaths);
    UbDataLink::AddPacketHeader(ackp,
                                false,
                                true,
                                context.creditTargetVl,
                                context.packetVl,
                                responseRoutingType,
                                UbDatalinkHeaderConfig::PACKET_UB_MEM);

    RoutingKey rtKey;
    rtKey.sip = utils::NodeIdToIp(context.destNode).Get();
    rtKey.dip = utils::NodeIdToIp(context.srcNode).Get();
    rtKey.sport = context.lb;
    rtKey.dport = 0;
    rtKey.priority = context.packetVl;
    rtKey.routingType = responseRoutingType;

    auto node = NodeList::GetNode(m_nodeId);
    auto sw = node->GetObject<UbSwitch>();

    bool selectedShortestPath = false;
    int destPort = sw->GetRoutingProcess()->GetOutPort(rtKey, selectedShortestPath);
    if (destPort < 0)
    {
        NS_ASSERT_MSG(0, "The route cannot be found");
    }

    sw->PushPacketToVoq(ackp, destPort, context.packetVl, destPort);
    NS_LOG_DEBUG("[UbLdstApi SendRemoteResponse] NodeId: " << m_nodeId
                  << " PacketUid: " << ackp->GetUid()
                  << " packetSize: " << ackp->GetSize()
                  << " destPort: " << destPort);

    if (m_pktTraceEnabled)
    {
        LdstRecvNotify(context.packetUid,
                       context.srcNode,
                       context.destNode,
                       PacketType::PACKET,
                       context.packetSize,
                       context.flowId,
                       context.traceTag);
    }

    Ptr<UbPort> triggerPort = DynamicCast<UbPort>(node->GetDevice(destPort));
    triggerPort->TriggerTransmit();
}

void
UbLdstApi::SendBoundRemoteResponse(Ptr<UbLdstApi> api,
                                   std::shared_ptr<const UbLdstRemoteRequestContext> context,
                                   TaOpcode responseOpcode,
                                   uint32_t payloadBytes,
                                   Ptr<Packet> payload)
{
    if (api == nullptr || context == nullptr)
    {
        return;
    }
    api->SendRemoteResponse(*context, responseOpcode, payloadBytes, payload);
}

/**
 * 接收到一个数据包，调用此函数处理，产生ack
 */
void UbLdstApi::RecvDataPacket(Ptr<Packet> packet, uint32_t ingressPort)
{
    // Store/load request: DLH cNTH cTAH(0x03/0x06) [cMAETAH] Payload
    // Store/load response: DLH cNTH cATAH(0x11/0x12) Payload
    if (packet == nullptr) {
        NS_LOG_ERROR("Null packet received");
        return;
    }

    NS_LOG_DEBUG("[UbLdstApi RecvDataPacket] nodeId: " << m_nodeId << " packetUid: " << packet->GetUid());
    UbDatalinkPacketHeader linkPacketHeader;
    UbCna16NetworkHeader cna16NetworkHeader;
    UbCompactTransactionHeader cTaHeader;
    UbLdstRemoteRequestContext context;
    if (!BuildRemoteRequestContext(packet,
                                   ingressPort,
                                   linkPacketHeader,
                                   cna16NetworkHeader,
                                   cTaHeader,
                                   context))
    {
        return;
    }

    Ptr<UbLdstApi> self(this);
    auto responseContext = std::make_shared<UbLdstRemoteRequestContext>(context);
    UbLdstResponseSender sender =
        MakeBoundCallback(&UbLdstApi::SendBoundRemoteResponse, self, responseContext);
    if (!m_remoteRequestProcessor.IsNull())
    {
        const bool accepted = m_remoteRequestProcessor(context, sender);
        if (!accepted)
        {
            NS_LOG_WARN("LDST remote processor rejected requestId "
                        << static_cast<uint32_t>(context.requestId)
                        << " nodeId " << m_nodeId);
        }
        return;
    }

    const TaOpcode responseOpcode = context.type == UbMemOperationType::LOAD
                                        ? TaOpcode::TA_OPCODE_READ_RESPONSE
                                        : TaOpcode::TA_OPCODE_TRANSACTION_ACK;
    SendRemoteResponse(context, responseOpcode, context.responseBytes, nullptr);
}

void UbLdstApi::RecvResponse(Ptr<Packet> packet)
{
    if (packet == nullptr) {
        NS_LOG_ERROR("in RecvResponse, packet is null");
        return;
    }
    NS_LOG_DEBUG("[UbLdstApi RecvResponse] packetUid: " << packet->GetUid());
    // Store/load response: DLH cNTH cATAH(0x11/0x12) Payload
    UbDatalinkPacketHeader linkPacketHeader;
    UbCna16NetworkHeader cna16NetworkHeader;
    UbCompactAckTransactionHeader caTaHeader;
    packet->RemoveHeader(linkPacketHeader);
    packet->RemoveHeader(cna16NetworkHeader);
    packet->RemoveHeader(caTaHeader);

    if (m_pktTraceEnabled) {
        UbFlowTag flowTag;
        packet->PeekPacketTag(flowTag);
        UbPacketTraceTag traceTag;
        packet->PeekPacketTag(traceTag);
        LdstRecvNotify(packet->GetUid(), utils::Cna16ToNodeId(cna16NetworkHeader.GetDcna()),
                       utils::Cna16ToNodeId(cna16NetworkHeader.GetScna()),
                       PacketType::ACK, packet->GetSize(), flowTag.GetFlowId(), traceTag);
    }
    uint16_t requestId = caTaHeader.GetIniTaSsn();
    TaOpcode responseOpcode = static_cast<TaOpcode>(caTaHeader.GetTaOpcode());
    const UbLdstEndpoint::ResponseContext responseContext{
        utils::Cna16ToNodeId(cna16NetworkHeader.GetScna()),
        utils::Cna16ToNodeId(cna16NetworkHeader.GetDcna())};
    Ptr<Node> node = NodeList::GetNode(m_nodeId);
    Ptr<UbController> controller = node != nullptr ? node->GetObject<UbController>() : nullptr;
    if (controller != nullptr && controller->GetLdstEndpoint() != nullptr &&
        controller->GetLdstEndpoint()->CompleteRequest(requestId,
                                                       responseContext,
                                                       responseOpcode,
                                                       packet))
    {
        return;
    }

    Ptr<UbLdstInstance> ldstInst = node != nullptr ? node->GetObject<UbLdstInstance>() : nullptr;
    if (ldstInst != nullptr && ldstInst->TryRecvAck(requestId))
    {
        return;
    }

    NS_LOG_WARN("LDST response requestId " << requestId
                                           << " has no endpoint inflight entry or legacy task segment");
}

void UbLdstApi::SetRoutingType(RoutingType routingType)
{
    ValidateRoutingType(routingType);
    m_routingType = routingType;
}

void UbLdstApi::LdstRecvNotify(uint32_t packetUid, uint32_t src, uint32_t dst,
                               PacketType type, uint32_t size, uint32_t taskId, UbPacketTraceTag traceTag)
{
    m_ldstRecvNotify(packetUid, src, dst, type, size, taskId, traceTag);
}

} // namespace ns3
