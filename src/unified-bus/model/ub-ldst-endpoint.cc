// SPDX-License-Identifier: GPL-2.0-only
#include "ub-ldst-endpoint.h"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-datalink.h"
#include "ns3/ub-header.h"
#include "ns3/ub-network-address.h"
#include "ns3/ub-port.h"
#include "ns3/ub-switch.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("UbLdstEndpoint");
NS_OBJECT_ENSURE_REGISTERED(UbLdstEndpoint);

namespace
{
constexpr uint64_t kCompactMaetahAddressMax = (uint64_t{1} << 58) - 1;
} // namespace

TypeId
UbLdstEndpoint::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UbLdstEndpoint")
                            .SetParent<Object>()
                            .SetGroupName("UnifiedBus")
                            .AddConstructor<UbLdstEndpoint>();
    return tid;
}

UbLdstEndpoint::UbLdstEndpoint()
{
}

UbLdstEndpoint::~UbLdstEndpoint()
{
}

void
UbLdstEndpoint::Init(uint32_t nodeId)
{
    m_nodeId = nodeId;
    m_initialized = true;
}

Ptr<Node>
UbLdstEndpoint::GetLocalNode() const
{
    if (!m_initialized || m_nodeId >= NodeList::GetNNodes())
    {
        return nullptr;
    }
    return NodeList::GetNode(m_nodeId);
}

bool
UbLdstEndpoint::IsEncodableMemorySize(uint32_t bytes, uint8_t& encodedLength) const
{
    if (bytes < 64 || bytes > 8192 || (bytes % 64) != 0)
    {
        return false;
    }

    uint32_t blocks = bytes / 64;
    if ((blocks & (blocks - 1)) != 0)
    {
        return false;
    }

    encodedLength = 0;
    while (blocks > 1)
    {
        blocks >>= 1;
        ++encodedLength;
    }
    return encodedLength <= 7;
}

bool
UbLdstEndpoint::ValidateRequest(const UbLdstRequest& request, uint8_t& encodedLength) const
{
    if (m_inflight.find(request.requestId) != m_inflight.end())
    {
        NS_LOG_WARN("Duplicate LDST request id " << request.requestId);
        return false;
    }
    if (request.completion.IsNull())
    {
        NS_LOG_WARN("LDST request has null completion callback");
        return false;
    }
    if (request.type != UbMemOperationType::STORE && request.type != UbMemOperationType::LOAD)
    {
        NS_LOG_WARN("Unsupported LDST request type");
        return false;
    }
    if (!IsValidPriority(request.priority))
    {
        NS_LOG_WARN("Invalid LDST priority " << static_cast<uint32_t>(request.priority));
        return false;
    }
    if (request.priority == 0)
    {
        NS_LOG_WARN("LDST data packets must use priority 1..15 because priority 0 is reserved "
                    "for control frames.");
        return false;
    }
    if (request.hasPaddr && request.paddr > kCompactMaetahAddressMax)
    {
        NS_LOG_WARN("LDST request PA does not fit compact MAETAH address field");
        return false;
    }

    Ptr<Node> node = GetLocalNode();
    if (node == nullptr || request.port >= node->GetNDevices() ||
        DynamicCast<UbPort>(node->GetDevice(request.port)) == nullptr)
    {
        NS_LOG_WARN("Invalid LDST output port " << request.port);
        return false;
    }

    if (request.type == UbMemOperationType::STORE)
    {
        if (request.payload != nullptr && request.payload->GetSize() != request.requestBytes)
        {
            NS_LOG_WARN("STORE payload size does not match requestBytes");
            return false;
        }
        return IsEncodableMemorySize(request.requestBytes, encodedLength);
    }

    if (request.requestBytes == 0)
    {
        NS_LOG_WARN("LOAD requestBytes must be nonzero");
        return false;
    }
    if (request.payload != nullptr)
    {
        NS_LOG_WARN("LOAD request must not carry payload");
        return false;
    }
    return IsEncodableMemorySize(request.responseBytes, encodedLength);
}

bool
UbLdstEndpoint::SendRequest(const UbLdstRequest& request)
{
    uint8_t encodedLength = 0;
    if (!ValidateRequest(request, encodedLength))
    {
        return false;
    }

    uint32_t expectedResponseBytes =
        request.type == UbMemOperationType::LOAD ? request.responseBytes : 0;
    m_inflight.emplace(request.requestId,
                       InflightRequest{request.type,
                                       expectedResponseBytes,
                                       ResponseContext{request.destNode, m_nodeId},
                                       request.completion});
    if (!BuildAndSendRequestPacket(request, encodedLength))
    {
        m_inflight.erase(request.requestId);
        return false;
    }
    return true;
}

bool
UbLdstEndpoint::BuildAndSendRequestPacket(const UbLdstRequest& request, uint8_t encodedLength)
{
    Ptr<Node> node = GetLocalNode();
    if (node == nullptr)
    {
        return false;
    }

    Ptr<UbSwitch> sw = node->GetObject<UbSwitch>();
    Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(request.port));
    if (sw == nullptr || port == nullptr)
    {
        return false;
    }

    uint32_t payloadBytes = request.requestBytes;
    Ptr<Packet> packet = request.type == UbMemOperationType::STORE && request.payload != nullptr
                             ? request.payload->Copy()
                             : Create<Packet>(payloadBytes);

    UbCompactMAExtTah maHeader;
    maHeader.SetLength(encodedLength);
    if (request.hasPaddr)
    {
        maHeader.SetVirtualAddress(request.paddr);
    }
    packet->AddHeader(maHeader);

    UbCompactTransactionHeader taHeader;
    taHeader.SetTaOpcode(request.type == UbMemOperationType::STORE ? TaOpcode::TA_OPCODE_WRITE
                                                                   : TaOpcode::TA_OPCODE_READ);
    taHeader.SetIniTaSsn(request.requestId);
    packet->AddHeader(taHeader);

    UbCna16NetworkHeader networkHeader;
    networkHeader.SetScna(static_cast<uint16_t>(utils::NodeIdToCna16(m_nodeId)));
    networkHeader.SetDcna(static_cast<uint16_t>(utils::NodeIdToCna16(request.destNode)));
    networkHeader.SetServiceLevel(request.priority);
    packet->AddHeader(networkHeader);

    UbDataLink::AddPacketHeader(packet,
                                false,
                                false,
                                request.priority,
                                request.priority,
                                RoutingType::PER_FLOW_SHORTEST_PATHS,
                                UbDatalinkHeaderConfig::PACKET_UB_MEM);

    sw->PushPacketToVoq(packet, request.port, request.priority, request.port);
    Simulator::ScheduleNow(&UbPort::TriggerTransmit, port);
    return true;
}

bool
UbLdstEndpoint::CompleteRequest(uint16_t requestId,
                                const ResponseContext& responseContext,
                                TaOpcode responseOpcode,
                                Ptr<Packet> payload)
{
    auto it = m_inflight.find(requestId);
    if (it == m_inflight.end())
    {
        return false;
    }
    if (it->second.responseContext.srcNode != responseContext.srcNode ||
        it->second.responseContext.destNode != responseContext.destNode)
    {
        return false;
    }

    InflightRequest inflight = it->second;
    m_inflight.erase(it);

    bool success = false;
    uint32_t payloadBytes = payload == nullptr ? 0 : payload->GetSize();
    if (inflight.type == UbMemOperationType::STORE)
    {
        success = responseOpcode == TaOpcode::TA_OPCODE_TRANSACTION_ACK && payloadBytes == 0;
    }
    else if (inflight.type == UbMemOperationType::LOAD)
    {
        success = responseOpcode == TaOpcode::TA_OPCODE_READ_RESPONSE && payload != nullptr &&
                  payloadBytes == inflight.expectedResponseBytes;
    }

    inflight.completion(requestId, success, payload);
    return true;
}

} // namespace ns3
