// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/ub-routing-process.h"

#include "ns3/enum.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/ub-routing-policy.h"

#include <algorithm>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(UbRoutingProcess);
NS_LOG_COMPONENT_DEFINE("UbRoutingProcess");

TypeId
UbRoutingProcess::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::UbRoutingProcess")
            .SetParent<Object>()
            .SetGroupName("UnifiedBus")
            .AddConstructor<UbRoutingProcess>()
            .AddAttribute("RoutingAlgorithm",
                          "Routing algorithm applied by UbRoutingProcess.",
                          EnumValue(UbRoutingAlgorithm::HASH),
                          MakeEnumAccessor<UbRoutingAlgorithm>(&UbRoutingProcess::m_routingAlgorithm),
                          MakeEnumChecker(UbRoutingAlgorithm::HASH,
                                          "HASH",
                                          UbRoutingAlgorithm::ADAPTIVE,
                                          "ADAPTIVE"));
    return tid;
}

UbRoutingProcess::UbRoutingProcess() = default;

void
UbRoutingProcess::SetNodeId(uint32_t nodeId)
{
    m_nodeId = nodeId;
}

void
UbRoutingProcess::AddShortestRoute(uint32_t destIp, const std::vector<uint16_t>& outPorts)
{
    m_table.AddShortestRoute(destIp, outPorts);
}

void
UbRoutingProcess::AddOtherRoute(uint32_t destIp, const std::vector<uint16_t>& outPorts)
{
    m_table.AddOtherRoute(destIp, outPorts);
}

void
UbRoutingProcess::AddShortestRouteRange(uint32_t startNodeId,
                                        uint32_t endNodeId,
                                        const std::vector<uint16_t>& outPorts)
{
    m_table.AddShortestRouteRange(startNodeId, endNodeId, outPorts);
}

void
UbRoutingProcess::AddOtherRouteRange(uint32_t startNodeId,
                                     uint32_t endNodeId,
                                     const std::vector<uint16_t>& outPorts)
{
    m_table.AddOtherRouteRange(startNodeId, endNodeId, outPorts);
}

void
UbRoutingProcess::AddShortestRouteRange(uint32_t startNodeId,
                                        uint32_t endNodeId,
                                        uint32_t dstPortId,
                                        const std::vector<uint16_t>& outPorts)
{
    m_table.AddShortestRouteRange(startNodeId, endNodeId, dstPortId, outPorts);
}

void
UbRoutingProcess::AddOtherRouteRange(uint32_t startNodeId,
                                     uint32_t endNodeId,
                                     uint32_t dstPortId,
                                     const std::vector<uint16_t>& outPorts)
{
    m_table.AddOtherRouteRange(startNodeId, endNodeId, dstPortId, outPorts);
}

void
UbRoutingProcess::GetShortestOutPorts(uint32_t destIp, std::vector<uint16_t>& outPorts)
{
    m_table.GetShortestOutPorts(destIp, outPorts);
}

void
UbRoutingProcess::GetOtherOutPorts(uint32_t destIp, std::vector<uint16_t>& outPorts)
{
    m_table.GetOtherOutPorts(destIp, outPorts);
}

const std::vector<uint16_t>
UbRoutingProcess::GetAllOutPorts(uint32_t destIp)
{
    return m_table.GetAllOutPorts(destIp);
}

UbRoutingTable::PortSetView
UbRoutingProcess::LookupCandidatePorts(uint32_t& dip, bool shortest) const
{
    auto ports = shortest ? m_table.LookupShortest(dip) : m_table.LookupOther(dip);
    if (!ports.empty())
    {
        return ports;
    }

    const uint32_t maskedDip = Ipv4Address(dip).CombineMask(Ipv4Mask("255.255.255.0")).Get();
    if (maskedDip == dip)
    {
        return ports;
    }
    ports = shortest ? m_table.LookupShortest(maskedDip) : m_table.LookupOther(maskedDip);
    dip = maskedDip;
    return ports;
}

void
UbRoutingProcess::GetShortestCandidates(uint32_t& dip,
                                        uint16_t inPortId,
                                        std::vector<uint16_t>& outPorts)
{
    const auto ports = LookupCandidatePorts(dip, true);
    outPorts.assign(ports.begin(), ports.end());
    if (inPortId != UINT16_MAX)
    {
        outPorts.erase(std::remove(outPorts.begin(), outPorts.end(), inPortId), outPorts.end());
    }
}

void
UbRoutingProcess::GetNonShortestCandidates(uint32_t& dip,
                                           uint16_t inPortId,
                                           std::vector<uint16_t>& outPorts)
{
    const auto ports = LookupCandidatePorts(dip, false);
    outPorts.assign(ports.begin(), ports.end());
    if (inPortId != UINT16_MAX)
    {
        outPorts.erase(std::remove(outPorts.begin(), outPorts.end(), inPortId), outPorts.end());
    }
}

int
UbRoutingProcess::SelectOutPort(RoutingKey& key,
                                const std::vector<uint16_t>& shortestPorts,
                                const std::vector<uint16_t>& nonShortestPorts,
                                bool& selectedShortestPath)
{
    const UbRoutingCandidateView candidates{shortestPorts, nonShortestPorts, UINT16_MAX};
    return UbRoutingPolicy::SelectHash(key, candidates, m_nodeId, selectedShortestPath);
}

int
UbRoutingProcess::SelectAdaptiveOutPort(RoutingKey& key,
                                        const std::vector<uint16_t>& shortestPorts,
                                        const std::vector<uint16_t>& nonShortestPorts,
                                        bool& selectedShortestPath)
{
    const UbRoutingCandidateView candidates{shortestPorts, nonShortestPorts, UINT16_MAX};
    return UbRoutingPolicy::SelectAdaptive(key, candidates, m_nodeId, selectedShortestPath);
}

int
UbRoutingProcess::GetOutPort(RoutingKey& key,
                             bool& selectedShortestPath,
                             uint16_t inPort)
{
    NS_LOG_DEBUG("[UbRoutingProcess GetOutPort]: sip: " << Ipv4Address(key.sip)
                                                        << " dip: " << Ipv4Address(key.dip)
                                                        << " sport: " << key.sport
                                                        << " dport: " << key.dport
                                                        << " priority: "
                                                        << static_cast<uint16_t>(key.priority)
                                                        << " useShortestPath: "
                                                        << key.useShortestPath
                                                        << " usePacketSpray: "
                                                        << key.usePacketSpray);

    ++m_hotPathLookups;
    uint32_t lookupDip = key.dip;
    const auto shortest = LookupCandidatePorts(lookupDip, true);
    UbRoutingTable::PortSetView nonShortest;
    if (!key.useShortestPath)
    {
        nonShortest = LookupCandidatePorts(lookupDip, false);
    }

    const UbRoutingCandidateView candidates{shortest, nonShortest, inPort};
    if (UbRoutingPolicy::Count(candidates) == 0)
    {
        NS_LOG_ERROR("No candidate ports found for dip: " << Ipv4Address(key.dip));
        return -1;
    }

    int outPort = -1;
    switch (m_routingAlgorithm)
    {
    case UbRoutingAlgorithm::HASH:
        outPort = UbRoutingPolicy::SelectHash(key, candidates, m_nodeId, selectedShortestPath);
        break;
    case UbRoutingAlgorithm::ADAPTIVE:
        outPort = UbRoutingPolicy::SelectAdaptive(key,
                                                  candidates,
                                                  m_nodeId,
                                                  selectedShortestPath);
        break;
    }

    NS_ASSERT_MSG(outPort != -1, "No available output port found");
    return outPort;
}

bool
UbRoutingProcess::RemoveShortestRoute(uint32_t destIp)
{
    return m_table.RemoveShortestRoute(destIp);
}

bool
UbRoutingProcess::RemoveOtherRoute(uint32_t destIp)
{
    return m_table.RemoveOtherRoute(destIp);
}

uint64_t
UbRoutingProcess::GetHotPathLookupCountForTest() const
{
    return m_hotPathLookups;
}

uint64_t
UbRoutingProcess::GetHotPathTemporaryContainerAllocationCountForTest() const
{
    return m_hotPathTemporaryContainerAllocations;
}

} // namespace ns3
