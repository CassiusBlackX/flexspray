// SPDX-License-Identifier: GPL-2.0-only

// Prototype question: can an exact, topology-independent packed next-hop index match the full
// routing table while using much less memory and reducing candidate lookup latency?

#include "ns3/ipv4-address.h"
#include "ns3/simulator.h"
#include "ns3/ub-network-address.h"
#include "ns3/ub-utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <tuple>
#include <variant>
#include <vector>

namespace
{

constexpr char kQueryMagic[] = "UBQRY001";
constexpr uint64_t kDefaultIterations = 10000000;
constexpr uint64_t kDefaultWarmupIterations = 100000;
constexpr uint64_t kDefaultBatchSize = 4096;
constexpr uint64_t kRandomSeed = 0x1d50d5eedULL;
constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kPortsPerAddressBlock = 256;

struct QueryRecord
{
    uint32_t currentNodeId;
    uint32_t destinationIp;
};

static_assert(sizeof(QueryRecord) == 8);

struct RouteCandidates
{
    std::vector<uint16_t> shortestPorts;
    std::vector<uint16_t> nonShortestPorts;
};

struct RouteCandidatesLess
{
    bool operator()(const RouteCandidates& left, const RouteCandidates& right) const
    {
        return std::tie(left.shortestPorts, left.nonShortestPorts) <
               std::tie(right.shortestPorts, right.nonShortestPorts);
    }
};

bool
operator==(const RouteCandidates& left, const RouteCandidates& right)
{
    return left.shortestPorts == right.shortestPorts &&
           left.nonShortestPorts == right.nonShortestPorts;
}

void
NormalizePorts(std::vector<uint16_t>& ports)
{
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
}

struct ProtocolScan
{
    std::vector<uint32_t> nodeIds;
    uint64_t routeCount{};
};

void
ReadExpectedToken(std::istream& input, std::string_view expected)
{
    std::string token;
    if (!(input >> token) || token != expected)
    {
        throw std::runtime_error("expected structured protocol token: " + std::string(expected));
    }
}

uint32_t
ReadProtocolUint32(std::istream& input, std::string_view field)
{
    uint64_t value = 0;
    if (!(input >> value) || value > std::numeric_limits<uint32_t>::max())
    {
        throw std::runtime_error("invalid structured protocol " + std::string(field));
    }
    return static_cast<uint32_t>(value);
}

void
ReadProtocolPorts(std::istream& input, std::vector<uint16_t>& ports, std::string_view field)
{
    const uint32_t count = ReadProtocolUint32(input, field);
    ports.resize(count);
    for (uint16_t& port : ports)
    {
        const uint32_t value = ReadProtocolUint32(input, field);
        if (value > std::numeric_limits<uint16_t>::max())
        {
            throw std::runtime_error("structured protocol port exceeds uint16");
        }
        port = static_cast<uint16_t>(value);
    }
}

template <typename Consumer>
ProtocolScan
ScanTableProtocol(const std::string& path, Consumer&& consume)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("cannot open structured protocol: " + path);
    }

    ReadExpectedToken(input, "NS3_UB_STRUCTURED");
    const uint32_t version = ReadProtocolUint32(input, "version");
    if (version != 3)
    {
        throw std::runtime_error("packed prototype requires structured protocol version 3");
    }

    ReadExpectedToken(input, "NODES");
    const uint32_t nodeCount = ReadProtocolUint32(input, "node count");
    ProtocolScan scan;
    scan.nodeIds.reserve(nodeCount);
    std::string text;
    for (uint32_t index = 0; index < nodeCount; ++index)
    {
        scan.nodeIds.push_back(ReadProtocolUint32(input, "node ID"));
        uint32_t portCount = 0;
        uint32_t systemId = 0;
        if (!(input >> text >> portCount >> text >> text >> systemId))
        {
            throw std::runtime_error("invalid structured protocol node");
        }
    }

    ReadExpectedToken(input, "LINKS");
    const uint32_t linkCount = ReadProtocolUint32(input, "link count");
    for (uint32_t index = 0; index < linkCount; ++index)
    {
        uint32_t node1 = 0;
        uint32_t port1 = 0;
        uint32_t node2 = 0;
        uint32_t port2 = 0;
        std::string bandwidth;
        std::string delay;
        if (!(input >> node1 >> port1 >> node2 >> port2 >> bandwidth >> delay))
        {
            throw std::runtime_error("invalid structured protocol link");
        }
    }

    ReadExpectedToken(input, "TABLE");
    scan.routeCount = ReadProtocolUint32(input, "table route count");
    utils::UbStructuredTableRoute route;
    for (uint64_t index = 0; index < scan.routeCount; ++index)
    {
        route.currentNodeId = ReadProtocolUint32(input, "table current node");
        route.destinationNodeId = ReadProtocolUint32(input, "table destination node");
        route.destinationPortId = ReadProtocolUint32(input, "table destination port");
        ReadProtocolPorts(input, route.shortestPorts, "shortest table ports");
        ReadProtocolPorts(input, route.nonShortestPorts, "non-shortest table ports");
        consume(route);
    }
    return scan;
}

class PackedIds
{
  public:
    explicit PackedIds(std::size_t size)
        : m_values(std::vector<uint8_t>(size, 0))
    {
    }

    void Set(std::size_t index, uint32_t value)
    {
        EnsureWidth(value);
        std::visit(
            [&](auto& values) {
                using Value = typename std::decay_t<decltype(values)>::value_type;
                values[index] = static_cast<Value>(value);
            },
            m_values);
    }

    uint32_t Get(std::size_t index) const
    {
        return std::visit([&](const auto& values) { return static_cast<uint32_t>(values[index]); },
                          m_values);
    }

    uint32_t WidthBits() const
    {
        return std::visit(
            [](const auto& values) {
                using Value = typename std::decay_t<decltype(values)>::value_type;
                return static_cast<uint32_t>(sizeof(Value) * 8);
            },
            m_values);
    }

    uint64_t PayloadBytes() const
    {
        return std::visit(
            [](const auto& values) {
                using Value = typename std::decay_t<decltype(values)>::value_type;
                return static_cast<uint64_t>(values.size()) * sizeof(Value);
            },
            m_values);
    }

    uint64_t AllocatedBytes() const
    {
        return std::visit(
            [](const auto& values) {
                using Value = typename std::decay_t<decltype(values)>::value_type;
                return static_cast<uint64_t>(values.capacity()) * sizeof(Value);
            },
            m_values);
    }

  private:
    using Storage =
        std::variant<std::vector<uint8_t>, std::vector<uint16_t>, std::vector<uint32_t>>;

    template <typename Destination, typename Source>
    static std::vector<Destination> Widen(const std::vector<Source>& source)
    {
        return std::vector<Destination>(source.begin(), source.end());
    }

    void EnsureWidth(uint32_t value)
    {
        if (value <= std::numeric_limits<uint8_t>::max())
        {
            return;
        }
        if (std::holds_alternative<std::vector<uint8_t>>(m_values))
        {
            const auto& source = std::get<std::vector<uint8_t>>(m_values);
            m_values = Widen<uint16_t>(source);
        }
        if (value <= std::numeric_limits<uint16_t>::max())
        {
            return;
        }
        if (std::holds_alternative<std::vector<uint16_t>>(m_values))
        {
            const auto& source = std::get<std::vector<uint16_t>>(m_values);
            m_values = Widen<uint32_t>(source);
        }
    }

    Storage m_values;
};

class PackedNextHopIndex
{
  public:
    explicit PackedNextHopIndex(const std::string& protocolPath)
    {
        Build(protocolPath);
    }

    bool Lookup(uint32_t currentNodeId,
                uint32_t destinationIp,
                std::vector<uint16_t>& shortestPorts,
                std::vector<uint16_t>& nonShortestPorts) const
    {
        shortestPorts.clear();
        nonShortestPorts.clear();
        if (currentNodeId >= m_rowByNodeId.size())
        {
            return false;
        }
        const uint32_t row = m_rowByNodeId[currentNodeId];
        if (row == kInvalidIndex)
        {
            return false;
        }

        const uint32_t destinationNodeId = utils::IpToNodeId(ns3::Ipv4Address(destinationIp));
        if (destinationNodeId >= m_hostIndexByNodeId.size())
        {
            return false;
        }

        uint32_t classId = 0;
        const uint32_t lastByte = destinationIp & 0xff;
        if (lastByte != 0)
        {
            const std::size_t addressIndex =
                static_cast<std::size_t>(destinationNodeId) * kPortsPerAddressBlock +
                (lastByte - 1);
            if (addressIndex < m_endpointByAddress.size())
            {
                const uint32_t endpoint = m_endpointByAddress[addressIndex];
                if (endpoint != kInvalidIndex)
                {
                    classId = m_portRoutes.Get(static_cast<std::size_t>(row) * m_endpointCount +
                                               endpoint);
                }
            }
        }

        // Match the current table behavior: a missing port-specific route uses the primary address.
        if (classId == 0)
        {
            const uint32_t hostIndex = m_hostIndexByNodeId[destinationNodeId];
            if (hostIndex == kInvalidIndex)
            {
                return false;
            }
            classId = m_nodeRoutes.Get(static_cast<std::size_t>(row) * m_hostCount + hostIndex);
        }
        if (classId == 0 || classId >= m_classes.size())
        {
            return false;
        }

        shortestPorts = m_classes[classId].shortestPorts;
        nonShortestPorts = m_classes[classId].nonShortestPorts;
        return true;
    }

    uint32_t RouteClassCount() const
    {
        return static_cast<uint32_t>(m_classes.size() - 1);
    }

    uint32_t PortIdWidthBits() const
    {
        return m_portRoutes.WidthBits();
    }

    uint32_t NodeIdWidthBits() const
    {
        return m_nodeRoutes.WidthBits();
    }

    uint32_t RowCount() const
    {
        return m_rowCount;
    }

    uint32_t EndpointCount() const
    {
        return m_endpointCount;
    }

    uint32_t HostCount() const
    {
        return m_hostCount;
    }

    uint64_t SourceRouteCount() const
    {
        return m_sourceRouteCount;
    }

    double DiscoverySeconds() const
    {
        return m_discoverySeconds;
    }

    double CompileSeconds() const
    {
        return m_compileSeconds;
    }

    uint64_t PortPayloadBytes() const
    {
        return m_portRoutes.PayloadBytes();
    }

    uint64_t NodePayloadBytes() const
    {
        return m_nodeRoutes.PayloadBytes();
    }

    uint64_t AllocatedBytes() const
    {
        uint64_t bytes = m_portRoutes.AllocatedBytes() + m_nodeRoutes.AllocatedBytes();
        bytes += static_cast<uint64_t>(m_rowByNodeId.capacity()) * sizeof(uint32_t);
        bytes += static_cast<uint64_t>(m_hostIndexByNodeId.capacity()) * sizeof(uint32_t);
        bytes += static_cast<uint64_t>(m_endpointByAddress.capacity()) * sizeof(uint32_t);
        for (const auto& endpoints : m_endpointsByHost)
        {
            bytes += static_cast<uint64_t>(endpoints.capacity()) * sizeof(uint32_t);
        }
        bytes +=
            static_cast<uint64_t>(m_endpointsByHost.capacity()) * sizeof(std::vector<uint32_t>);
        bytes += static_cast<uint64_t>(m_classes.capacity()) * sizeof(RouteCandidates);
        for (const auto& candidates : m_classes)
        {
            bytes += static_cast<uint64_t>(candidates.shortestPorts.capacity()) * sizeof(uint16_t);
            bytes +=
                static_cast<uint64_t>(candidates.nonShortestPorts.capacity()) * sizeof(uint16_t);
        }
        return bytes;
    }

  private:
    uint32_t Intern(RouteCandidates candidates)
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

    void Build(const std::string& protocolPath)
    {
        const auto discoveryStarted = std::chrono::steady_clock::now();
        std::set<std::pair<uint32_t, uint32_t>> endpointKeys;
        std::set<uint32_t> hostIds;
        ProtocolScan discovery =
            ScanTableProtocol(protocolPath, [&](const utils::UbStructuredTableRoute& route) {
                if (route.destinationPortId >= kPortsPerAddressBlock - 1)
                {
                    throw std::runtime_error("destination port cannot be encoded in a UB address");
                }
                endpointKeys.emplace(route.destinationNodeId, route.destinationPortId);
                hostIds.insert(route.destinationNodeId);
            });
        m_discoverySeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - discoveryStarted)
                .count();
        m_sourceRouteCount = discovery.routeCount;
        if (discovery.nodeIds.empty() || discovery.routeCount == 0)
        {
            throw std::runtime_error("packed index requires nodes and table routes");
        }

        const auto compileStarted = std::chrono::steady_clock::now();
        std::vector<uint32_t> nodeIds = std::move(discovery.nodeIds);
        std::sort(nodeIds.begin(), nodeIds.end());
        if (std::adjacent_find(nodeIds.begin(), nodeIds.end()) != nodeIds.end())
        {
            throw std::runtime_error("packed index received duplicate node IDs");
        }
        const uint32_t largestNodeId = nodeIds.back();
        m_rowCount = static_cast<uint32_t>(nodeIds.size());
        m_rowByNodeId.assign(static_cast<std::size_t>(largestNodeId) + 1, kInvalidIndex);
        for (uint32_t row = 0; row < nodeIds.size(); ++row)
        {
            m_rowByNodeId[nodeIds[row]] = row;
        }

        m_endpointCount = static_cast<uint32_t>(endpointKeys.size());
        m_hostCount = static_cast<uint32_t>(hostIds.size());
        m_hostIndexByNodeId.assign(static_cast<std::size_t>(largestNodeId) + 1, kInvalidIndex);
        m_endpointByAddress.assign((static_cast<std::size_t>(largestNodeId) + 1) *
                                       kPortsPerAddressBlock,
                                   kInvalidIndex);
        m_endpointsByHost.resize(m_hostCount);

        uint32_t hostIndex = 0;
        for (uint32_t nodeId : hostIds)
        {
            if (nodeId >= m_hostIndexByNodeId.size())
            {
                throw std::runtime_error("table route references a destination outside node IDs");
            }
            m_hostIndexByNodeId[nodeId] = hostIndex++;
        }
        uint32_t endpointIndex = 0;
        for (const auto& [nodeId, portId] : endpointKeys)
        {
            if (nodeId >= m_hostIndexByNodeId.size())
            {
                throw std::runtime_error("table route references a destination outside node IDs");
            }
            m_endpointByAddress[static_cast<std::size_t>(nodeId) * kPortsPerAddressBlock + portId] =
                endpointIndex;
            m_endpointsByHost[m_hostIndexByNodeId[nodeId]].push_back(endpointIndex);
            ++endpointIndex;
        }

        m_classes.push_back(RouteCandidates{});
        m_portRoutes = PackedIds(static_cast<std::size_t>(m_rowCount) * m_endpointCount);
        const ProtocolScan fill =
            ScanTableProtocol(protocolPath, [&](const utils::UbStructuredTableRoute& route) {
                if (route.currentNodeId >= m_rowByNodeId.size() ||
                    m_rowByNodeId[route.currentNodeId] == kInvalidIndex)
                {
                    throw std::runtime_error("table route references an unknown current node");
                }
                const uint32_t endpoint =
                    m_endpointByAddress[static_cast<std::size_t>(route.destinationNodeId) *
                                            kPortsPerAddressBlock +
                                        route.destinationPortId];
                const std::size_t cell =
                    static_cast<std::size_t>(m_rowByNodeId[route.currentNodeId]) * m_endpointCount +
                    endpoint;
                if (m_portRoutes.Get(cell) != 0)
                {
                    throw std::runtime_error("duplicate packed route lookup");
                }
                RouteCandidates expected{route.shortestPorts, route.nonShortestPorts};
                NormalizePorts(expected.shortestPorts);
                NormalizePorts(expected.nonShortestPorts);
                const uint32_t classId = Intern(expected);
                m_portRoutes.Set(cell, classId);
                if (!(m_classes[classId] == expected))
                {
                    throw std::runtime_error("packed index differs from a source table route");
                }
            });
        std::vector<uint32_t> fillNodeIds = fill.nodeIds;
        std::sort(fillNodeIds.begin(), fillNodeIds.end());
        if (fillNodeIds != nodeIds || fill.routeCount != m_sourceRouteCount)
        {
            throw std::runtime_error("structured protocol changed between packed index passes");
        }

        m_nodeRoutes = PackedIds(static_cast<std::size_t>(m_rowCount) * m_hostCount);
        std::vector<uint16_t> shortestPorts;
        std::vector<uint16_t> nonShortestPorts;
        shortestPorts.reserve(64);
        nonShortestPorts.reserve(64);
        for (uint32_t row = 0; row < m_rowCount; ++row)
        {
            for (uint32_t destination = 0; destination < m_hostCount; ++destination)
            {
                shortestPorts.clear();
                nonShortestPorts.clear();
                for (uint32_t endpoint : m_endpointsByHost[destination])
                {
                    const uint32_t classId = m_portRoutes.Get(
                        static_cast<std::size_t>(row) * m_endpointCount + endpoint);
                    if (classId == 0)
                    {
                        continue;
                    }
                    const auto& candidates = m_classes[classId];
                    shortestPorts.insert(shortestPorts.end(),
                                         candidates.shortestPorts.begin(),
                                         candidates.shortestPorts.end());
                    nonShortestPorts.insert(nonShortestPorts.end(),
                                            candidates.nonShortestPorts.begin(),
                                            candidates.nonShortestPorts.end());
                }
                if (!shortestPorts.empty() || !nonShortestPorts.empty())
                {
                    m_nodeRoutes.Set(static_cast<std::size_t>(row) * m_hostCount + destination,
                                     Intern({shortestPorts, nonShortestPorts}));
                }
            }
        }

        m_classIds.clear();
        m_compileSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - compileStarted)
                .count();
    }

    uint64_t m_sourceRouteCount{};
    double m_discoverySeconds{};
    double m_compileSeconds{};
    uint32_t m_rowCount{};
    uint32_t m_endpointCount{};
    uint32_t m_hostCount{};
    std::vector<uint32_t> m_rowByNodeId;
    std::vector<uint32_t> m_hostIndexByNodeId;
    std::vector<uint32_t> m_endpointByAddress;
    std::vector<std::vector<uint32_t>> m_endpointsByHost;
    std::vector<RouteCandidates> m_classes;
    std::map<RouteCandidates, uint32_t, RouteCandidatesLess> m_classIds;
    PackedIds m_portRoutes{0};
    PackedIds m_nodeRoutes{0};
};

struct Measurement
{
    std::string distribution;
    uint64_t queryCount{};
    uint64_t batchSize{};
    uint64_t batchCount{};
    double meanNs{};
    double p50BatchMeanNs{};
    double p99BatchMeanNs{};
    double queriesPerSecond{};
    uint64_t checksum{};
    uint64_t missing{};
};

struct Options
{
    std::string protocolPath;
    std::string exhaustiveQueryPath;
    std::string nodeQueryPath;
    std::string workloadQueryPath;
    std::string outputPath;
    std::string topologyProtocolPath;
    uint64_t iterations{kDefaultIterations};
    uint64_t warmupIterations{kDefaultWarmupIterations};
    uint64_t batchSize{kDefaultBatchSize};
};

uint64_t
CurrentRssBytes()
{
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(),
                  MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info),
                  &count) != KERN_SUCCESS)
    {
        throw std::runtime_error("task_info failed while reading current RSS");
    }
    return info.resident_size;
}

uint64_t
PeakRssBytes()
{
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        throw std::runtime_error("getrusage failed while reading peak RSS");
    }
    return static_cast<uint64_t>(usage.ru_maxrss);
}

uint64_t
ReadLittleEndian64(std::istream& input)
{
    uint64_t value = 0;
    for (uint32_t shift = 0; shift < 64; shift += 8)
    {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof())
        {
            throw std::runtime_error("truncated query corpus header");
        }
        value |= static_cast<uint64_t>(static_cast<uint8_t>(byte)) << shift;
    }
    return value;
}

uint32_t
ReadLittleEndian32(std::istream& input)
{
    uint32_t value = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8)
    {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof())
        {
            throw std::runtime_error("truncated query corpus record");
        }
        value |= static_cast<uint32_t>(static_cast<uint8_t>(byte)) << shift;
    }
    return value;
}

std::vector<QueryRecord>
ReadQueries(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("cannot open query corpus: " + path);
    }
    char magic[sizeof(kQueryMagic) - 1]{};
    input.read(magic, sizeof(magic));
    if (input.gcount() != sizeof(magic) ||
        std::string_view(magic, sizeof(magic)) != std::string_view(kQueryMagic))
    {
        throw std::runtime_error("invalid query corpus magic: " + path);
    }
    const uint64_t count = ReadLittleEndian64(input);
    if (count == 0 || count > std::numeric_limits<std::size_t>::max())
    {
        throw std::runtime_error("invalid query corpus count: " + path);
    }
    std::vector<QueryRecord> queries;
    queries.reserve(static_cast<std::size_t>(count));
    for (uint64_t index = 0; index < count; ++index)
    {
        queries.push_back({ReadLittleEndian32(input), ReadLittleEndian32(input)});
    }
    if (input.peek() != std::char_traits<char>::eof())
    {
        throw std::runtime_error("query corpus contains trailing data: " + path);
    }
    return queries;
}

uint64_t
ParsePositiveInteger(const char* text, const char* option)
{
    std::size_t consumed = 0;
    const uint64_t value = std::stoull(text, &consumed);
    if (consumed != std::string(text).size() || value == 0)
    {
        throw std::runtime_error(std::string(option) + " must be a positive integer");
    }
    return value;
}

Options
ParseOptions(int argc, char* argv[])
{
    if (argc < 6)
    {
        throw std::runtime_error(
            "usage: benchmark_packed_next_hop PROTOCOL EXHAUSTIVE_QUERIES NODE_QUERIES "
            "WORKLOAD_QUERIES OUTPUT_JSON [--topology-protocol PATH] [--iterations N] "
            "[--warmup N] [--batch-size N]");
    }
    Options options{argv[1], argv[2], argv[3], argv[4], argv[5]};
    for (int index = 6; index < argc; index += 2)
    {
        if (index + 1 >= argc)
        {
            throw std::runtime_error(std::string("missing value for ") + argv[index]);
        }
        const std::string option = argv[index];
        if (option == "--iterations")
        {
            options.iterations = ParsePositiveInteger(argv[index + 1], "--iterations");
        }
        else if (option == "--warmup")
        {
            options.warmupIterations = ParsePositiveInteger(argv[index + 1], "--warmup");
        }
        else if (option == "--batch-size")
        {
            options.batchSize = ParsePositiveInteger(argv[index + 1], "--batch-size");
        }
        else if (option == "--topology-protocol")
        {
            options.topologyProtocolPath = argv[index + 1];
        }
        else
        {
            throw std::runtime_error("unknown option: " + option);
        }
    }
    return options;
}

uint64_t
Mix64(uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

uint64_t
ChecksumPorts(const std::vector<uint16_t>& ports)
{
    uint64_t checksum = Mix64(ports.size());
    for (uint16_t port : ports)
    {
        checksum = Mix64(checksum ^ (static_cast<uint64_t>(port) + 1));
    }
    return checksum;
}

double
Percentile(std::vector<double> values, double percentile)
{
    std::sort(values.begin(), values.end());
    const std::size_t rank =
        static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(1, rank) - 1];
}

template <typename Lookup>
Measurement
Measure(std::string distribution,
        uint64_t iterations,
        uint64_t warmupIterations,
        uint64_t batchSize,
        Lookup&& lookup)
{
    uint64_t warmupChecksum = 0;
    uint64_t warmupMissing = 0;
    for (uint64_t index = 0; index < warmupIterations; ++index)
    {
        lookup(index, warmupChecksum, warmupMissing);
    }
    if (warmupMissing != 0)
    {
        throw std::runtime_error("warmup contained missing routes");
    }

    uint64_t checksum = 0;
    uint64_t missing = 0;
    std::vector<double> batchMeans;
    batchMeans.reserve(static_cast<std::size_t>((iterations + batchSize - 1) / batchSize));
    const auto allStarted = std::chrono::steady_clock::now();
    for (uint64_t begin = 0; begin < iterations; begin += batchSize)
    {
        const uint64_t end = std::min(iterations, begin + batchSize);
        const auto batchStarted = std::chrono::steady_clock::now();
        for (uint64_t index = begin; index < end; ++index)
        {
            lookup(index, checksum, missing);
        }
        const auto elapsed = std::chrono::steady_clock::now() - batchStarted;
        batchMeans.push_back(std::chrono::duration<double, std::nano>(elapsed).count() /
                             static_cast<double>(end - begin));
    }
    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - allStarted).count();
    return {
        std::move(distribution),
        iterations,
        batchSize,
        batchMeans.size(),
        elapsedSeconds * 1e9 / static_cast<double>(iterations),
        Percentile(batchMeans, 0.50),
        Percentile(batchMeans, 0.99),
        static_cast<double>(iterations) / elapsedSeconds,
        checksum,
        missing,
    };
}

uint64_t
ValidateCorpus(const PackedNextHopIndex& index,
               const std::vector<QueryRecord>& queries,
               const std::string& name)
{
    std::vector<uint16_t> shortestPorts;
    std::vector<uint16_t> nonShortestPorts;
    uint64_t checksum = 0;
    for (std::size_t queryIndex = 0; queryIndex < queries.size(); ++queryIndex)
    {
        const auto& query = queries[queryIndex];
        if (!index.Lookup(query.currentNodeId,
                          query.destinationIp,
                          shortestPorts,
                          nonShortestPorts) ||
            shortestPorts.empty())
        {
            throw std::runtime_error(name + " corpus contains a missing packed route");
        }
        checksum += ChecksumPorts(shortestPorts) + queryIndex;
    }
    return checksum;
}

void
WriteJson(const Options& options,
          const PackedNextHopIndex& index,
          uint64_t sourceRouteCount,
          uint64_t exhaustiveCount,
          uint64_t nodeCount,
          uint64_t workloadCount,
          uint64_t exhaustiveChecksum,
          uint64_t nodeChecksum,
          uint64_t workloadChecksum,
          double discoverySeconds,
          double buildSeconds,
          double topologyInstallSeconds,
          double corpusSeconds,
          uint64_t rssBeforeBuild,
          uint64_t rssAfterBuild,
          uint64_t rssAfterTopologyInstall,
          uint64_t rssAfterAllocatorRelief,
          uint64_t allocatorReleasedBytes,
          uint64_t heapAfterBuild,
          uint64_t peakRss,
          const std::vector<Measurement>& measurements)
{
    std::ofstream output(options.outputPath);
    if (!output)
    {
        throw std::runtime_error("cannot open benchmark output: " + options.outputPath);
    }
    output << std::fixed << std::setprecision(6) << "{\n"
           << "  \"mode\": \"packed-next-hop-prototype\",\n"
           << "  \"sourceTableRoutes\": " << sourceRouteCount << ",\n"
           << "  \"rowCount\": " << index.RowCount() << ",\n"
           << "  \"endpointCount\": " << index.EndpointCount() << ",\n"
           << "  \"hostCount\": " << index.HostCount() << ",\n"
           << "  \"routeClassCount\": " << index.RouteClassCount() << ",\n"
           << "  \"portIdWidthBits\": " << index.PortIdWidthBits() << ",\n"
           << "  \"nodeIdWidthBits\": " << index.NodeIdWidthBits() << ",\n"
           << "  \"portPayloadBytes\": " << index.PortPayloadBytes() << ",\n"
           << "  \"nodePayloadBytes\": " << index.NodePayloadBytes() << ",\n"
           << "  \"estimatedAllocatedIndexBytes\": " << index.AllocatedBytes() << ",\n"
           << "  \"exhaustiveCorpusQueries\": " << exhaustiveCount << ",\n"
           << "  \"nodeCorpusQueries\": " << nodeCount << ",\n"
           << "  \"workloadCorpusQueries\": " << workloadCount << ",\n"
           << "  \"exhaustiveValidationChecksum\": " << exhaustiveChecksum << ",\n"
           << "  \"nodeValidationChecksum\": " << nodeChecksum << ",\n"
           << "  \"workloadValidationChecksum\": " << workloadChecksum << ",\n"
           << "  \"discoveryScanSeconds\": " << discoverySeconds << ",\n"
           << "  \"compileSeconds\": " << buildSeconds << ",\n"
           << "  \"totalBuildSeconds\": " << discoverySeconds + buildSeconds << ",\n"
           << "  \"topologyInstallSeconds\": " << topologyInstallSeconds << ",\n"
           << "  \"corpusLoadSortValidateSeconds\": " << corpusSeconds << ",\n"
           << "  \"rssBeforeBuildBytes\": " << rssBeforeBuild << ",\n"
           << "  \"rssAfterBuildBytes\": " << rssAfterBuild << ",\n"
           << "  \"rssAfterTopologyInstallBytes\": " << rssAfterTopologyInstall << ",\n"
           << "  \"allocatorReleasedBytes\": " << allocatorReleasedBytes << ",\n"
           << "  \"rssAfterAllocatorReliefBytes\": " << rssAfterAllocatorRelief << ",\n"
           << "  \"heapInUseAfterBuildBytes\": " << heapAfterBuild << ",\n"
           << "  \"peakRssBytes\": " << peakRss << ",\n"
           << "  \"iterationsPerMeasurement\": " << options.iterations << ",\n"
           << "  \"warmupIterationsPerMeasurement\": " << options.warmupIterations << ",\n"
           << "  \"randomSeed\": " << kRandomSeed << ",\n"
           << "  \"measurements\": [\n";
    for (std::size_t measurementIndex = 0; measurementIndex < measurements.size();
         ++measurementIndex)
    {
        const auto& measurement = measurements[measurementIndex];
        output << "    {\n"
               << "      \"distribution\": \"" << measurement.distribution << "\",\n"
               << "      \"lookupPath\": \"candidate-lookup\",\n"
               << "      \"queryCount\": " << measurement.queryCount << ",\n"
               << "      \"batchSize\": " << measurement.batchSize << ",\n"
               << "      \"batchCount\": " << measurement.batchCount << ",\n"
               << "      \"meanNs\": " << measurement.meanNs << ",\n"
               << "      \"p50BatchMeanNs\": " << measurement.p50BatchMeanNs << ",\n"
               << "      \"p99BatchMeanNs\": " << measurement.p99BatchMeanNs << ",\n"
               << "      \"queriesPerSecond\": " << measurement.queriesPerSecond << ",\n"
               << "      \"checksum\": " << measurement.checksum << ",\n"
               << "      \"missing\": " << measurement.missing << "\n"
               << "    }" << (measurementIndex + 1 == measurements.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

} // namespace

int
main(int argc, char* argv[])
{
    try
    {
        const Options options = ParseOptions(argc, argv);
        const uint64_t rssBeforeBuild = CurrentRssBytes();
        auto index = std::make_unique<PackedNextHopIndex>(options.protocolPath);
        const uint64_t rssAfterBuild = CurrentRssBytes();
        double topologyInstallSeconds = 0.0;
        if (!options.topologyProtocolPath.empty())
        {
            std::ifstream topologyProtocol(options.topologyProtocolPath);
            if (!topologyProtocol)
            {
                throw std::runtime_error("cannot open topology protocol: " +
                                         options.topologyProtocolPath);
            }
            const auto installStarted = std::chrono::steady_clock::now();
            utils::UbStructuredTopologyConfig topology =
                utils::UbUtils::ReadStructuredTopologyConfig(topologyProtocol);
            topology.tableRoutes.clear();
            topology.tableNextHopIndex.reset();
            topology.tableRouteCount = 0;
            topology.portGroups.clear();
            topology.routingRules.clear();
            topology.routingRangeRules.clear();
            topology.topologyRules.clear();
            utils::UbUtils::Get()->InstallStructuredTopology(topology, ns3::Time(0), 1);
            topologyInstallSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - installStarted)
                    .count();
        }
        const uint64_t rssAfterTopologyInstall = CurrentRssBytes();
        const malloc_statistics_t heapAfterBuild = [] {
            malloc_statistics_t statistics{};
            malloc_zone_statistics(nullptr, &statistics);
            return statistics;
        }();
        const uint64_t allocatorReleasedBytes = malloc_zone_pressure_relief(nullptr, 0);
        const uint64_t rssAfterAllocatorRelief = CurrentRssBytes();

        const auto corpusStarted = std::chrono::steady_clock::now();
        std::vector<QueryRecord> exhaustiveQueries = ReadQueries(options.exhaustiveQueryPath);
        std::vector<QueryRecord> nodeQueries = ReadQueries(options.nodeQueryPath);
        const std::vector<QueryRecord> workloadQueries = ReadQueries(options.workloadQueryPath);
        auto sortQueries = [](std::vector<QueryRecord>& queries) {
            std::sort(queries.begin(),
                      queries.end(),
                      [](const QueryRecord& left, const QueryRecord& right) {
                          return left.currentNodeId < right.currentNodeId ||
                                 (left.currentNodeId == right.currentNodeId &&
                                  left.destinationIp < right.destinationIp);
                      });
        };
        sortQueries(exhaustiveQueries);
        sortQueries(nodeQueries);
        const uint64_t exhaustiveChecksum = ValidateCorpus(*index, exhaustiveQueries, "exhaustive");
        const uint64_t nodeChecksum = ValidateCorpus(*index, nodeQueries, "node");
        const uint64_t workloadChecksum = ValidateCorpus(*index, workloadQueries, "workload");
        const double corpusSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - corpusStarted).count();

        std::vector<Measurement> measurements;
        auto addMeasurement = [&](const std::string& distribution,
                                  const std::vector<QueryRecord>& queries,
                                  bool randomOrder) {
            std::vector<uint16_t> shortestPorts;
            std::vector<uint16_t> nonShortestPorts;
            shortestPorts.reserve(32);
            nonShortestPorts.reserve(32);
            measurements.push_back(
                Measure(distribution,
                        options.iterations,
                        options.warmupIterations,
                        options.batchSize,
                        [&](uint64_t measurementIndex, uint64_t& checksum, uint64_t& missing) {
                            const uint64_t selected =
                                randomOrder ? Mix64(measurementIndex ^ kRandomSeed) % queries.size()
                                            : measurementIndex % queries.size();
                            const auto& query = queries[static_cast<std::size_t>(selected)];
                            if (!index->Lookup(query.currentNodeId,
                                               query.destinationIp,
                                               shortestPorts,
                                               nonShortestPorts) ||
                                shortestPorts.empty())
                            {
                                ++missing;
                            }
                            checksum += ChecksumPorts(shortestPorts) + measurementIndex;
                        }));
        };

        addMeasurement("exhaustive-sequential", exhaustiveQueries, false);
        addMeasurement("exhaustive-random", exhaustiveQueries, true);
        addMeasurement("node-primary-sequential", nodeQueries, false);
        addMeasurement("existing-workload-endpoints", workloadQueries, false);

        WriteJson(options,
                  *index,
                  index->SourceRouteCount(),
                  exhaustiveQueries.size(),
                  nodeQueries.size(),
                  workloadQueries.size(),
                  exhaustiveChecksum,
                  nodeChecksum,
                  workloadChecksum,
                  index->DiscoverySeconds(),
                  index->CompileSeconds(),
                  topologyInstallSeconds,
                  corpusSeconds,
                  rssBeforeBuild,
                  rssAfterBuild,
                  rssAfterTopologyInstall,
                  rssAfterAllocatorRelief,
                  allocatorReleasedBytes,
                  heapAfterBuild.size_in_use,
                  PeakRssBytes(),
                  measurements);
        ns3::Simulator::Destroy();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "benchmark_packed_next_hop: " << error.what() << '\n';
        ns3::Simulator::Destroy();
        return 2;
    }
}
