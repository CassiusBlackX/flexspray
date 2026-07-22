// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/object.h"
#include "ns3/ub-routing-process.h"
#include "ns3/ub-rule-routing.h"
#include "ns3/ub-utils.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr uint32_t kDestinationCount = 1024;
constexpr uint32_t kIterations = 5000000;

struct TopologyQuery
{
    uint32_t current;
    uint32_t destination;
    uint32_t destinationPort;
};

struct TopologyFixture
{
    std::shared_ptr<ns3::UbRuleRoutingTable> rules;
    std::vector<TopologyQuery> queries;
};

template <typename Lookup>
double
Measure(Lookup&& lookup, uint64_t& checksum)
{
    std::vector<uint16_t> shortestPorts;
    std::vector<uint16_t> nonShortestPorts;
    const auto started = std::chrono::steady_clock::now();
    for (uint32_t index = 0; index < kIterations; ++index)
    {
        lookup(index, shortestPorts, nonShortestPorts);
        checksum += shortestPorts.front();
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    return std::chrono::duration<double, std::nano>(elapsed).count() / kIterations;
}

class TopologyBuilder
{
  public:
    TopologyBuilder()
        : m_rules(std::make_shared<ns3::UbRuleRoutingTable>())
    {
    }

    void AddLink(uint32_t left, uint32_t right)
    {
        const uint16_t leftPort = AllocatePort(left);
        const uint16_t rightPort = AllocatePort(right);
        m_rules->AddTopologyLink(ns3::UbTopologyLink{left, leftPort, right, rightPort});
    }

    void AddRule(ns3::UbTopologyRule rule)
    {
        m_rules->AddTopologyRule(rule);
    }

    TopologyFixture Finish(const std::vector<uint32_t>& hosts)
    {
        m_rules->PrepareForLookup();
        std::vector<TopologyQuery> queries;
        std::vector<uint16_t> shortestPorts;
        std::vector<uint16_t> nonShortestPorts;
        for (uint32_t destination : hosts)
        {
            for (uint32_t destinationPort = 0; destinationPort < m_nextPort[destination];
                 ++destinationPort)
            {
                for (uint32_t current = 0; current < m_nextPort.size(); ++current)
                {
                    if (current == destination)
                    {
                        continue;
                    }
                    if (m_rules->TryLookup(current,
                                           destination,
                                           destinationPort,
                                           shortestPorts,
                                           nonShortestPorts))
                    {
                        queries.push_back({current, destination, destinationPort});
                    }
                }
            }
        }
        if (queries.empty())
        {
            throw std::runtime_error("topology benchmark generated no valid lookups");
        }
        return {m_rules, std::move(queries)};
    }

  private:
    uint16_t AllocatePort(uint32_t node)
    {
        if (m_nextPort.size() <= node)
        {
            m_nextPort.resize(node + 1);
        }
        return m_nextPort[node]++;
    }

    std::shared_ptr<ns3::UbRuleRoutingTable> m_rules;
    std::vector<uint16_t> m_nextPort;
};

std::vector<std::vector<uint32_t>>
Coordinates(const std::vector<uint32_t>& shape)
{
    uint32_t nodeCount = 1;
    for (uint32_t size : shape)
    {
        nodeCount *= size;
    }
    std::vector<std::vector<uint32_t>> coordinates(nodeCount, std::vector<uint32_t>(shape.size()));
    for (uint32_t node = 0; node < nodeCount; ++node)
    {
        uint32_t remaining = node;
        for (std::size_t dimension = shape.size(); dimension-- > 0;)
        {
            coordinates[node][dimension] = remaining % shape[dimension];
            remaining /= shape[dimension];
        }
    }
    return coordinates;
}

void
AddDimensionStructure(TopologyBuilder& builder,
                      const std::vector<uint32_t>& nodeIds,
                      const std::vector<uint32_t>& shape,
                      const std::vector<ns3::UbDimensionConnection>& connections)
{
    const auto coordinates = Coordinates(shape);
    if (nodeIds.size() != coordinates.size() || shape.size() != connections.size())
    {
        throw std::runtime_error("dimension benchmark shape mismatch");
    }
    std::set<std::pair<uint32_t, uint32_t>> links;
    for (uint32_t node = 0; node < nodeIds.size(); ++node)
    {
        for (std::size_t dimension = 0; dimension < shape.size(); ++dimension)
        {
            std::vector<uint32_t> peers;
            if (connections[dimension] == ns3::UbDimensionConnection::FULL_MESH)
            {
                for (uint32_t value = coordinates[node][dimension] + 1; value < shape[dimension];
                     ++value)
                {
                    peers.push_back(value);
                }
            }
            else if (connections[dimension] == ns3::UbDimensionConnection::RING)
            {
                peers.push_back((coordinates[node][dimension] + 1) % shape[dimension]);
            }
            else if (coordinates[node][dimension] + 1 < shape[dimension])
            {
                peers.push_back(coordinates[node][dimension] + 1);
            }

            for (uint32_t value : peers)
            {
                auto peerCoordinates = coordinates[node];
                peerCoordinates[dimension] = value;
                const auto peer =
                    std::find(coordinates.begin(), coordinates.end(), peerCoordinates) -
                    coordinates.begin();
                const auto edge = std::minmax(nodeIds[node], nodeIds[peer]);
                if (links.insert(edge).second)
                {
                    builder.AddLink(edge.first, edge.second);
                }
            }
        }
    }

    std::vector<ns3::UbTopologyRuleNode> nodes;
    for (uint32_t node = 0; node < nodeIds.size(); ++node)
    {
        nodes.push_back({nodeIds[node], true, coordinates[node]});
    }
    builder.AddRule(ns3::UbTopologyRule{
        ns3::UbTopologyRuleType::DIMENSION,
        shape,
        connections,
        std::move(nodes),
    });
}

TopologyFixture
BuildDimensionFixture(const std::vector<uint32_t>& shape,
                      const std::vector<ns3::UbDimensionConnection>& connections)
{
    uint32_t nodeCount = 1;
    for (uint32_t size : shape)
    {
        nodeCount *= size;
    }
    std::vector<uint32_t> hosts(nodeCount);
    for (uint32_t node = 0; node < nodeCount; ++node)
    {
        hosts[node] = node;
    }
    TopologyBuilder builder;
    AddDimensionStructure(builder, hosts, shape, connections);
    return builder.Finish(hosts);
}

TopologyFixture
BuildUbxFixture()
{
    constexpr uint32_t hostCount = 16;
    TopologyBuilder builder;
    std::vector<uint32_t> hosts(hostCount);
    for (uint32_t host = 0; host < hostCount; ++host)
    {
        hosts[host] = host;
    }
    AddDimensionStructure(
        builder,
        hosts,
        {4, 4},
        {ns3::UbDimensionConnection::FULL_MESH, ns3::UbDimensionConnection::FULL_MESH});
    for (uint32_t switchIndex = 0; switchIndex < 4; ++switchIndex)
    {
        const uint32_t switchNode = hostCount + switchIndex;
        std::vector<ns3::UbTopologyRuleNode> nodes;
        for (uint32_t host : hosts)
        {
            builder.AddLink(host, switchNode);
            nodes.push_back({host, true, {0, host}});
        }
        nodes.push_back({switchNode, false, {1, 0}});
        builder.AddRule(ns3::UbTopologyRule{
            ns3::UbTopologyRuleType::DIRECT_SWITCH,
            {},
            {},
            std::move(nodes),
        });
    }
    return builder.Finish(hosts);
}

TopologyFixture
BuildOneDimensionalPodFixture()
{
    constexpr uint32_t groupCount = 4;
    constexpr uint32_t hostsPerGroup = 4;
    constexpr uint32_t hostCount = groupCount * hostsPerGroup;
    constexpr uint32_t planeCount = 3;
    constexpr uint32_t switchesPerPlane = 7;
    TopologyBuilder builder;
    std::vector<uint32_t> hosts(hostCount);
    for (uint32_t host = 0; host < hostCount; ++host)
    {
        hosts[host] = host;
    }
    for (uint32_t group = 0; group < groupCount; ++group)
    {
        std::vector<uint32_t> groupHosts;
        for (uint32_t local = 0; local < hostsPerGroup; ++local)
        {
            groupHosts.push_back(group * hostsPerGroup + local);
        }
        AddDimensionStructure(builder,
                              groupHosts,
                              {hostsPerGroup},
                              {ns3::UbDimensionConnection::FULL_MESH});
    }

    for (uint32_t plane = 0; plane < planeCount; ++plane)
    {
        const uint32_t planeStart = hostCount + plane * switchesPerPlane;
        const uint32_t localUpperStart = planeStart + groupCount;
        const uint32_t sharedUpper = localUpperStart + 2;
        std::vector<ns3::UbTopologyRuleNode> nodes;
        for (uint32_t group = 0; group < groupCount; ++group)
        {
            const uint32_t leaf = planeStart + group;
            nodes.push_back({leaf, false, {1, group, 0}});
            for (uint32_t local = 0; local < hostsPerGroup; ++local)
            {
                const uint32_t host = group * hostsPerGroup + local;
                builder.AddLink(host, leaf);
                nodes.push_back({host, true, {0, group, local}});
            }
            builder.AddLink(leaf, localUpperStart + group / 2);
            builder.AddLink(leaf, sharedUpper);
        }
        nodes.push_back({localUpperStart, false, {2, 0, 0}});
        nodes.push_back({localUpperStart + 1, false, {2, 1, 0}});
        nodes.push_back({sharedUpper, false, {2, 2, 0}});
        builder.AddRule(ns3::UbTopologyRule{
            ns3::UbTopologyRuleType::TWO_LEVEL_SWITCH,
            {},
            {},
            std::move(nodes),
        });
    }
    return builder.Finish(hosts);
}

TopologyFixture
BuildDragonflyFixture()
{
    constexpr uint32_t routersPerGroup = 4;
    constexpr uint32_t groupCount = 5;
    constexpr uint32_t routerCount = routersPerGroup * groupCount;
    constexpr uint32_t hostStart = routerCount;
    TopologyBuilder builder;
    std::vector<uint32_t> hosts;
    std::vector<ns3::UbTopologyRuleNode> nodes;

    auto routerId = [](uint32_t group, uint32_t router) {
        return group * routersPerGroup + router;
    };
    for (uint32_t group = 0; group < groupCount; ++group)
    {
        for (uint32_t router = 0; router < routersPerGroup; ++router)
        {
            const uint32_t routerNode = routerId(group, router);
            const uint32_t host = hostStart + routerNode;
            builder.AddLink(host, routerNode);
            hosts.push_back(host);
            nodes.push_back({host, true, {0, group, router, 0}});
            nodes.push_back({routerNode, false, {1, group, router, 0}});
        }
        for (uint32_t left = 0; left < routersPerGroup; ++left)
        {
            for (uint32_t right = left + 1; right < routersPerGroup; ++right)
            {
                builder.AddLink(routerId(group, left), routerId(group, right));
            }
        }
    }

    std::vector<uint32_t> nextGlobalRouter(groupCount);
    for (uint32_t leftGroup = 0; leftGroup < groupCount; ++leftGroup)
    {
        for (uint32_t rightGroup = leftGroup + 1; rightGroup < groupCount; ++rightGroup)
        {
            builder.AddLink(routerId(leftGroup, nextGlobalRouter[leftGroup]++),
                            routerId(rightGroup, nextGlobalRouter[rightGroup]++));
        }
    }
    builder.AddRule(ns3::UbTopologyRule{
        ns3::UbTopologyRuleType::DRAGONFLY,
        {},
        {},
        std::move(nodes),
    });
    return builder.Finish(hosts);
}

double
MeasureTopology(const TopologyFixture& fixture, uint64_t& checksum)
{
    return Measure(
        [&fixture](uint32_t index,
                   std::vector<uint16_t>& shortestPorts,
                   std::vector<uint16_t>& nonShortestPorts) {
            const auto& query = fixture.queries[index % fixture.queries.size()];
            fixture.rules->TryLookup(query.current,
                                     query.destination,
                                     query.destinationPort,
                                     shortestPorts,
                                     nonShortestPorts);
        },
        checksum);
}

} // namespace

int
main()
{
    using namespace ns3;

    Ptr<UbRoutingProcess> table = CreateObject<UbRoutingProcess>();
    for (uint32_t destination = 0; destination < kDestinationCount; ++destination)
    {
        table->AddShortestRoute(utils::NodeIdToIp(destination, 0).Get(), {3, 4});
    }

    auto exactRules = std::make_shared<UbRuleRoutingTable>();
    exactRules->AddPortGroup(UbPortGroup{0, 0, {3, 4}});
    for (uint32_t destination = 0; destination < kDestinationCount; ++destination)
    {
        exactRules->AddRule(UbRoutingRule{0, UbRuleDestination{destination, 0}, {0}, {}});
    }
    exactRules->PrepareForLookup();

    auto rangeRules = std::make_shared<UbRuleRoutingTable>();
    rangeRules->AddPortGroup(UbPortGroup{0, 0, {3, 4}});
    rangeRules->AddRangeRule(UbRoutingRangeRule{0, 0, kDestinationCount - 1, 0, {0}, {}});
    rangeRules->PrepareForLookup();

    const auto ndFullMesh = BuildDimensionFixture({4, 4, 4},
                                                  {UbDimensionConnection::FULL_MESH,
                                                   UbDimensionConnection::FULL_MESH,
                                                   UbDimensionConnection::FULL_MESH});
    const auto ring = BuildDimensionFixture({64}, {UbDimensionConnection::RING});
    const auto torus =
        BuildDimensionFixture({8, 8}, {UbDimensionConnection::RING, UbDimensionConnection::RING});
    const auto ubx = BuildUbxFixture();
    const auto oneDimensionalPod = BuildOneDimensionalPodFixture();
    const auto dragonfly = BuildDragonflyFixture();

    uint64_t checksum = 0;
    const double tableNs = Measure(
        [&table](uint32_t index,
                 std::vector<uint16_t>& shortestPorts,
                 std::vector<uint16_t>& nonShortestPorts) {
            nonShortestPorts.clear();
            table->GetShortestOutPorts(utils::NodeIdToIp(index % kDestinationCount, 0).Get(),
                                       shortestPorts);
        },
        checksum);
    const double exactRuleNs = Measure(
        [&exactRules](uint32_t index,
                      std::vector<uint16_t>& shortestPorts,
                      std::vector<uint16_t>& nonShortestPorts) {
            exactRules->TryLookup(0, index % kDestinationCount, 0, shortestPorts, nonShortestPorts);
        },
        checksum);
    const double rangeRuleNs = Measure(
        [&rangeRules](uint32_t index,
                      std::vector<uint16_t>& shortestPorts,
                      std::vector<uint16_t>& nonShortestPorts) {
            rangeRules->TryLookup(0, index % kDestinationCount, 0, shortestPorts, nonShortestPorts);
        },
        checksum);

    std::cout << "table_ns_per_lookup=" << tableNs << '\n'
              << "exact_rule_ns_per_lookup=" << exactRuleNs << '\n'
              << "range_rule_ns_per_lookup=" << rangeRuleNs << '\n'
              << "nd_fullmesh_rule_ns_per_lookup=" << MeasureTopology(ndFullMesh, checksum) << '\n'
              << "ring_rule_ns_per_lookup=" << MeasureTopology(ring, checksum) << '\n'
              << "torus_rule_ns_per_lookup=" << MeasureTopology(torus, checksum) << '\n'
              << "ubx_rule_ns_per_lookup=" << MeasureTopology(ubx, checksum) << '\n'
              << "one_d_pod_rule_ns_per_lookup=" << MeasureTopology(oneDimensionalPod, checksum)
              << '\n'
              << "dragonfly_rule_ns_per_lookup=" << MeasureTopology(dragonfly, checksum) << '\n'
              << "checksum=" << checksum << '\n';
    return 0;
}
