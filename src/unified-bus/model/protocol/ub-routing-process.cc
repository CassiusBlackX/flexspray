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

UbRoutingTable&
UbRoutingProcess::GetRoutingTable()
{
    return m_table;
}

const UbRoutingTable&
UbRoutingProcess::GetRoutingTable() const
{
    return m_table;
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
