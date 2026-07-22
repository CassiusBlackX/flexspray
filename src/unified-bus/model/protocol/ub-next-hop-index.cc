// SPDX-License-Identifier: GPL-2.0-only
#include "ub-next-hop-index.h"

#include "ns3/abort.h"
#include "ns3/ub-utils.h"

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>

namespace ns3
{

namespace
{

void
NormalizePorts(std::vector<uint16_t>& ports)
{
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
}

} // namespace

bool
UbNextHopIndex::RouteCandidatesLess::operator()(const RouteCandidates& left,
                                                const RouteCandidates& right) const
{
    return std::tie(left.shortestPorts, left.nonShortestPorts) <
           std::tie(right.shortestPorts, right.nonShortestPorts);
}

UbNextHopIndex::PackedIds::PackedIds(std::size_t size, uint32_t largestValue)
{
    if (largestValue <= std::numeric_limits<uint8_t>::max())
    {
        m_values = std::vector<uint8_t>(size, 0);
    }
    else if (largestValue <= std::numeric_limits<uint16_t>::max())
    {
        m_values = std::vector<uint16_t>(size, 0);
    }
    else
    {
        m_values = std::vector<uint32_t>(size, 0);
    }
}

void
UbNextHopIndex::PackedIds::Set(std::size_t index, uint32_t value)
{
    EnsureWidth(value);
    std::visit(
        [&](auto& values) {
            using Value = typename std::decay_t<decltype(values)>::value_type;
            values[index] = static_cast<Value>(value);
        },
        m_values);
}

uint32_t
UbNextHopIndex::PackedIds::Get(std::size_t index) const
{
    return std::visit([&](const auto& values) { return static_cast<uint32_t>(values[index]); },
                      m_values);
}

uint32_t
UbNextHopIndex::PackedIds::GetWidthBits() const
{
    return std::visit(
        [](const auto& values) {
            using Value = typename std::decay_t<decltype(values)>::value_type;
            return static_cast<uint32_t>(sizeof(Value) * 8);
        },
        m_values);
}

void
UbNextHopIndex::PackedIds::EnsureWidth(uint32_t value)
{
    if (value <= std::numeric_limits<uint8_t>::max())
    {
        return;
    }
    if (std::holds_alternative<std::vector<uint8_t>>(m_values))
    {
        m_values = Widen<uint16_t>(std::get<std::vector<uint8_t>>(m_values));
    }
    if (value <= std::numeric_limits<uint16_t>::max())
    {
        return;
    }
    if (std::holds_alternative<std::vector<uint16_t>>(m_values))
    {
        m_values = Widen<uint32_t>(std::get<std::vector<uint16_t>>(m_values));
    }
}

std::shared_ptr<const UbNextHopIndex>
UbNextHopIndex::Build(const std::vector<utils::UbStructuredTableRoute>& routes, uint32_t nodeCount)
{
    UbNextHopIndexBuilder builder(nodeCount, routes.size());
    for (const auto& route : routes)
    {
        builder.AddRoute(route);
    }
    return builder.Build();
}

uint32_t
UbNextHopIndex::Intern(RouteCandidates candidates)
{
    NormalizePorts(candidates.shortestPorts);
    NormalizePorts(candidates.nonShortestPorts);
    const auto existing = m_classIds.find(candidates);
    if (existing != m_classIds.end())
    {
        return existing->second;
    }
    const uint32_t classId = static_cast<uint32_t>(m_classes.size());
    m_classes.push_back(std::move(candidates));
    m_classIds.emplace(m_classes.back(), classId);
    return classId;
}

UbNextHopIndexBuilder::UbNextHopIndexBuilder(uint32_t nodeCount, std::size_t expectedRouteCount)
    : m_nodeCount(nodeCount),
      m_index(std::shared_ptr<UbNextHopIndex>(new UbNextHopIndex()))
{
    NS_ABORT_MSG_IF(nodeCount == 0, "next-hop index requires at least one node");
    m_index->m_classes.push_back(UbNextHopIndex::RouteCandidates{});
    m_routes.reserve(expectedRouteCount);
}

void
UbNextHopIndexBuilder::AddRoute(const utils::UbStructuredTableRoute& route)
{
    NS_ABORT_MSG_IF(m_built, "cannot add a route after building the next-hop index");
    NS_ABORT_MSG_IF(route.currentNodeId >= m_nodeCount || route.destinationNodeId >= m_nodeCount,
                    "next-hop route references an unknown node");
    NS_ABORT_MSG_IF(route.destinationPortId >= UbNextHopIndex::PORTS_PER_ADDRESS_BLOCK - 1,
                    "next-hop destination port cannot be encoded in a UB address");
    NS_ABORT_MSG_IF(route.shortestPorts.empty() && route.nonShortestPorts.empty(),
                    "next-hop route must contain at least one output port");

    m_currentNodeIds.insert(route.currentNodeId);
    m_destinationNodeIds.insert(route.destinationNodeId);
    m_endpointKeys.emplace(route.destinationNodeId, route.destinationPortId);
    const uint32_t classId = m_index->Intern({route.shortestPorts, route.nonShortestPorts});
    m_routes.push_back(
        {route.currentNodeId, route.destinationNodeId, route.destinationPortId, classId});
}

std::shared_ptr<const UbNextHopIndex>
UbNextHopIndexBuilder::Build()
{
    NS_ABORT_MSG_IF(m_built, "next-hop index builder can only build once");
    NS_ABORT_MSG_IF(m_routes.empty(), "next-hop index requires at least one route");
    m_built = true;
    auto& index = *m_index;

    index.m_rowCount = static_cast<uint32_t>(m_currentNodeIds.size());
    index.m_endpointCount = static_cast<uint32_t>(m_endpointKeys.size());
    index.m_destinationNodeCount = static_cast<uint32_t>(m_destinationNodeIds.size());
    index.m_rowByNodeId.assign(m_nodeCount, UbNextHopIndex::INVALID_INDEX);
    index.m_destinationIndexByNodeId.assign(m_nodeCount, UbNextHopIndex::INVALID_INDEX);
    index.m_endpointByAddress.assign(static_cast<std::size_t>(m_nodeCount) *
                                         UbNextHopIndex::PORTS_PER_ADDRESS_BLOCK,
                                     UbNextHopIndex::INVALID_INDEX);
    index.m_endpointsByDestination.resize(index.m_destinationNodeCount);

    uint32_t row = 0;
    for (uint32_t nodeId : m_currentNodeIds)
    {
        index.m_rowByNodeId[nodeId] = row++;
    }
    uint32_t destinationIndex = 0;
    for (uint32_t nodeId : m_destinationNodeIds)
    {
        index.m_destinationIndexByNodeId[nodeId] = destinationIndex++;
    }
    uint32_t endpointIndex = 0;
    for (const auto& [nodeId, portId] : m_endpointKeys)
    {
        index.m_endpointByAddress[static_cast<std::size_t>(nodeId) *
                                      UbNextHopIndex::PORTS_PER_ADDRESS_BLOCK +
                                  portId] = endpointIndex;
        index.m_endpointsByDestination[index.m_destinationIndexByNodeId[nodeId]].push_back(
            endpointIndex);
        ++endpointIndex;
    }

    index.m_portRoutes = UbNextHopIndex::PackedIds(
        static_cast<std::size_t>(index.m_rowCount) * index.m_endpointCount,
        static_cast<uint32_t>(index.m_classes.size() - 1));
    for (const auto& route : m_routes)
    {
        const uint32_t routeRow = index.m_rowByNodeId[route.currentNodeId];
        const uint32_t routeEndpoint =
            index.m_endpointByAddress[static_cast<std::size_t>(route.destinationNodeId) *
                                          UbNextHopIndex::PORTS_PER_ADDRESS_BLOCK +
                                      route.destinationPortId];
        const std::size_t cell =
            static_cast<std::size_t>(routeRow) * index.m_endpointCount + routeEndpoint;
        uint32_t classId = route.classId;
        const uint32_t existingClassId = index.m_portRoutes.Get(cell);
        if (existingClassId != 0)
        {
            UbNextHopIndex::RouteCandidates candidates = index.m_classes[classId];
            const auto& existing = index.m_classes[existingClassId];
            candidates.shortestPorts.insert(candidates.shortestPorts.end(),
                                            existing.shortestPorts.begin(),
                                            existing.shortestPorts.end());
            candidates.nonShortestPorts.insert(candidates.nonShortestPorts.end(),
                                               existing.nonShortestPorts.begin(),
                                               existing.nonShortestPorts.end());
            classId = index.Intern(std::move(candidates));
        }
        index.m_portRoutes.Set(cell, classId);
    }

    index.m_nodeRoutes = UbNextHopIndex::PackedIds(
        static_cast<std::size_t>(index.m_rowCount) * index.m_destinationNodeCount,
        static_cast<uint32_t>(index.m_classes.size() - 1));
    for (uint32_t routeRow = 0; routeRow < index.m_rowCount; ++routeRow)
    {
        for (uint32_t destination = 0; destination < index.m_destinationNodeCount; ++destination)
        {
            UbNextHopIndex::RouteCandidates candidates;
            for (uint32_t endpoint : index.m_endpointsByDestination[destination])
            {
                const uint32_t classId = index.m_portRoutes.Get(
                    static_cast<std::size_t>(routeRow) * index.m_endpointCount + endpoint);
                if (classId == 0)
                {
                    continue;
                }
                const auto& endpointCandidates = index.m_classes[classId];
                candidates.shortestPorts.insert(candidates.shortestPorts.end(),
                                                endpointCandidates.shortestPorts.begin(),
                                                endpointCandidates.shortestPorts.end());
                candidates.nonShortestPorts.insert(candidates.nonShortestPorts.end(),
                                                   endpointCandidates.nonShortestPorts.begin(),
                                                   endpointCandidates.nonShortestPorts.end());
            }
            if (!candidates.shortestPorts.empty() || !candidates.nonShortestPorts.empty())
            {
                index.m_nodeRoutes.Set(
                    static_cast<std::size_t>(routeRow) * index.m_destinationNodeCount + destination,
                    index.Intern(std::move(candidates)));
            }
        }
    }
    index.m_classIds.clear();
    std::vector<PendingRoute>().swap(m_routes);
    return m_index;
}

bool
UbNextHopIndex::TryLookup(uint32_t currentNodeId,
                          uint32_t destinationNodeId,
                          uint32_t destinationPortId,
                          std::vector<uint16_t>& shortestPorts,
                          std::vector<uint16_t>& nonShortestPorts) const
{
    shortestPorts.clear();
    nonShortestPorts.clear();
    if (currentNodeId >= m_rowByNodeId.size() ||
        destinationNodeId >= m_destinationIndexByNodeId.size())
    {
        return false;
    }
    const uint32_t row = m_rowByNodeId[currentNodeId];
    const uint32_t destination = m_destinationIndexByNodeId[destinationNodeId];
    if (row == INVALID_INDEX || destination == INVALID_INDEX)
    {
        return false;
    }

    uint32_t classId = 0;
    if (destinationPortId != ANY_PORT)
    {
        if (destinationPortId >= PORTS_PER_ADDRESS_BLOCK - 1)
        {
            return false;
        }
        const uint32_t endpoint = m_endpointByAddress[static_cast<std::size_t>(destinationNodeId) *
                                                          PORTS_PER_ADDRESS_BLOCK +
                                                      destinationPortId];
        if (endpoint != INVALID_INDEX)
        {
            classId = m_portRoutes.Get(static_cast<std::size_t>(row) * m_endpointCount + endpoint);
        }
    }
    else
    {
        classId =
            m_nodeRoutes.Get(static_cast<std::size_t>(row) * m_destinationNodeCount + destination);
    }
    if (classId == 0 || classId >= m_classes.size())
    {
        return false;
    }

    shortestPorts = m_classes[classId].shortestPorts;
    nonShortestPorts = m_classes[classId].nonShortestPorts;
    return true;
}

uint32_t
UbNextHopIndex::GetRouteClassCount() const
{
    return static_cast<uint32_t>(m_classes.size() - 1);
}

uint32_t
UbNextHopIndex::GetPortIdWidthBits() const
{
    return m_portRoutes.GetWidthBits();
}

uint32_t
UbNextHopIndex::GetNodeIdWidthBits() const
{
    return m_nodeRoutes.GetWidthBits();
}

} // namespace ns3
