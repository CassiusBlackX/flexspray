// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/enum.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"
#include "ns3/ub-next-hop-index.h"
#include "ns3/ub-routing-process.h"
#include "ns3/ub-switch.h"
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>

namespace
{

constexpr char kQueryMagic[] = "UBQRY001";

struct Query
{
    uint32_t currentNodeId;
    uint32_t destinationIp;
};

struct Measurement
{
    std::string name;
    double meanNs{};
    double p50BatchMeanNs{};
    double p99BatchMeanNs{};
    uint64_t checksum{};
    uint64_t missing{};
};

uint64_t
ReadLittle64(std::istream& input)
{
    uint64_t value = 0;
    for (uint32_t shift = 0; shift < 64; shift += 8)
    {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof())
        {
            throw std::runtime_error("truncated query corpus");
        }
        value |= static_cast<uint64_t>(static_cast<uint8_t>(byte)) << shift;
    }
    return value;
}

uint32_t
ReadLittle32(std::istream& input)
{
    uint32_t value = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8)
    {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof())
        {
            throw std::runtime_error("truncated query corpus");
        }
        value |= static_cast<uint32_t>(static_cast<uint8_t>(byte)) << shift;
    }
    return value;
}

std::vector<Query>
ReadQueries(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    char magic[sizeof(kQueryMagic) - 1]{};
    input.read(magic, sizeof(magic));
    if (!input || std::string_view(magic, sizeof(magic)) != kQueryMagic)
    {
        throw std::runtime_error("invalid query corpus: " + path);
    }
    const uint64_t count = ReadLittle64(input);
    std::vector<Query> queries;
    queries.reserve(count);
    for (uint64_t index = 0; index < count; ++index)
    {
        queries.push_back({ReadLittle32(input), ReadLittle32(input)});
    }
    if (!input || input.peek() != std::char_traits<char>::eof())
    {
        throw std::runtime_error("truncated or oversized query corpus: " + path);
    }
    return queries;
}

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
        throw std::runtime_error("task_info failed");
    }
    return info.resident_size;
}

uint64_t
PeakRssBytes()
{
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        throw std::runtime_error("getrusage failed");
    }
    return static_cast<uint64_t>(usage.ru_maxrss);
}

uint64_t
Mix64(uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

double
Percentile(std::vector<double> values, double percentile)
{
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(percentile * values.size()));
    return values[std::max<std::size_t>(1, rank) - 1];
}

template <typename Lookup>
Measurement
Measure(const std::string& name,
        uint64_t iterations,
        uint64_t warmup,
        uint64_t batchSize,
        Lookup&& lookup)
{
    uint64_t ignoredChecksum = 0;
    uint64_t ignoredMissing = 0;
    for (uint64_t index = 0; index < warmup; ++index)
    {
        lookup(index, ignoredChecksum, ignoredMissing, nullptr);
    }

    uint64_t checksum = 0;
    uint64_t missing = 0;
    std::vector<double> batches;
    const auto started = std::chrono::steady_clock::now();
    for (uint64_t begin = 0; begin < iterations; begin += batchSize)
    {
        const uint64_t end = std::min(iterations, begin + batchSize);
        const auto batchStarted = std::chrono::steady_clock::now();
        for (uint64_t index = begin; index < end; ++index)
        {
            lookup(index, checksum, missing, nullptr);
        }
        batches.push_back(std::chrono::duration<double, std::nano>(
                              std::chrono::steady_clock::now() - batchStarted)
                              .count() /
                          static_cast<double>(end - begin));
    }
    const double elapsed =
        std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started)
            .count();
    return {name,
            elapsed / iterations,
            Percentile(batches, 0.50),
            Percentile(batches, 0.99),
            checksum,
            missing};
}

void
WriteLittle16(std::ostream& output, uint16_t value)
{
    output.put(static_cast<char>(value));
    output.put(static_cast<char>(value >> 8));
}

} // namespace

int
main(int argc, char* argv[])
{
    try
    {
        if (argc != 10)
        {
            throw std::runtime_error(
                "usage: benchmark MODE TOPOLOGY BINARY|- QUERIES ITERATIONS SIGNATURE "
                "COMMON_SIGNATURE OUTPUT SEED");
        }
        const std::string mode = argv[1];
        const uint64_t iterations = std::stoull(argv[5]);
        const uint64_t seed = std::stoull(argv[9]);
        constexpr uint64_t warmup = 100000;
        constexpr uint64_t batchSize = 4096;
        constexpr uint64_t signatureCount = 100000;

        const uint64_t rssBefore = CurrentRssBytes();
        double parseSeconds = 0.0;
        double installSeconds = 0.0;
        {
            const auto parseStarted = std::chrono::steady_clock::now();
            std::ifstream topologyInput(argv[2]);
            auto config = utils::UbUtils::ReadStructuredTopologyConfig(topologyInput);
            if (mode == "compressed-table-lookup")
            {
                std::ifstream binaryInput(argv[3], std::ios::binary);
                std::vector<std::vector<bool>> connectedPorts(config.nodes.size());
                for (const auto& node : config.nodes)
                {
                    connectedPorts[node.nodeId].resize(node.portCount);
                }
                for (const auto& link : config.links)
                {
                    connectedPorts[link.node1][link.port1] = true;
                    connectedPorts[link.node2][link.port2] = true;
                }
                config.tableNextHopIndex = ns3::UbNextHopIndex::Read(binaryInput,
                                                                     connectedPorts,
                                                                     config.routingFingerprint);
                config.portGroups.clear();
                config.routingRules.clear();
                config.routingRangeRules.clear();
                config.topologyRules.clear();
            }
            else if (mode != "structured-rule-compute" && mode != "text-table-lookup")
            {
                throw std::runtime_error("unknown mode: " + mode);
            }
            parseSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - parseStarted)
                    .count();
            const auto installStarted = std::chrono::steady_clock::now();
            utils::UbUtils::Get()->InstallStructuredTopology(config, ns3::Time(0), 1);
            installSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - installStarted)
                    .count();
        }
        const uint64_t rssAfterInstall = CurrentRssBytes();
        const malloc_statistics_t heapAfterInstall = [] {
            malloc_statistics_t value{};
            malloc_zone_statistics(nullptr, &value);
            return value;
        }();
        const std::size_t allocatorReleased = malloc_zone_pressure_relief(nullptr, 0);
        const uint64_t rssAfterAllocatorRelief = CurrentRssBytes();
        malloc_statistics_t heapAfterAllocatorRelief{};
        malloc_zone_statistics(nullptr, &heapAfterAllocatorRelief);
        const uint64_t peakRss = PeakRssBytes();

        std::vector<ns3::Ptr<ns3::UbRoutingProcess>> routing;
        for (uint32_t nodeId = 0; nodeId < ns3::NodeList::GetNNodes(); ++nodeId)
        {
            routing.push_back(
                ns3::NodeList::GetNode(nodeId)->GetObject<ns3::UbSwitch>()->GetRoutingProcess());
        }
        const auto queries = ReadQueries(argv[4]);
        auto queryAt = [&](uint64_t index) -> const Query& {
            return queries[Mix64(index ^ seed) % queries.size()];
        };

        std::ofstream signature(argv[6], std::ios::binary);
        signature.write("UBRTSIG1", 8);
        std::ofstream commonSignature(argv[7], std::ios::binary);
        commonSignature.write("UBRTCMN1", 8);
        std::vector<Measurement> measurements;
        measurements.push_back(
            Measure("candidate-set",
                    iterations,
                    warmup,
                    batchSize,
                    [&](uint64_t index, uint64_t& checksum, uint64_t& missing, std::ostream*) {
                        const auto& query = queryAt(index);
                        const auto ports =
                            routing[query.currentNodeId]->GetAllOutPorts(query.destinationIp);
                        if (ports.empty())
                        {
                            ++missing;
                        }
                        for (uint16_t port : ports)
                        {
                            checksum = Mix64(checksum ^ (static_cast<uint64_t>(port) + 1));
                        }
                    }));
        for (uint64_t index = 0; index < signatureCount; ++index)
        {
            const auto& query = queryAt(index);
            const auto ports = routing[query.currentNodeId]->GetAllOutPorts(query.destinationIp);
            WriteLittle16(signature, static_cast<uint16_t>(ports.size()));
            WriteLittle16(commonSignature, static_cast<uint16_t>(ports.size()));
            for (uint16_t port : ports)
            {
                WriteLittle16(signature, port);
                WriteLittle16(commonSignature, port);
            }
        }

        const std::vector<std::pair<ns3::MultipathSelector, std::string>> selectors = {
            {ns3::MultipathSelector::HASH64, "HASH64"},
            {ns3::MultipathSelector::CRC32, "CRC32"},
            {ns3::MultipathSelector::TOEPLITZ, "TOEPLITZ"},
            {ns3::MultipathSelector::ROUND_ROBIN, "ROUND_ROBIN"},
            {ns3::MultipathSelector::ADAPTIVE, "ADAPTIVE"},
            {ns3::MultipathSelector::INGRESS_PORT_STRIPE, "INGRESS_PORT_STRIPE"},
        };
        for (const auto& [selector, name] : selectors)
        {
            for (const auto& process : routing)
            {
                process->SetAttribute("MultipathSelector", ns3::EnumValue(selector));
            }
            const ns3::RoutingType routingType =
                selector == ns3::MultipathSelector::ROUND_ROBIN ||
                        selector == ns3::MultipathSelector::ADAPTIVE
                    ? ns3::RoutingType::PER_PACKET_ALL_PATHS
                    : ns3::RoutingType::PER_FLOW_ALL_PATHS;
            auto lookup =
                [&](uint64_t index, uint64_t& checksum, uint64_t& missing, std::ostream* bytes) {
                    const auto& query = queryAt(index);
                    ns3::RoutingKey key{utils::NodeIdToIp(query.currentNodeId).Get(),
                                        query.destinationIp,
                                        static_cast<uint16_t>(index),
                                        4792,
                                        0,
                                        routingType};
                    bool selectedShortest = false;
                    const uint16_t inPort = selector == ns3::MultipathSelector::INGRESS_PORT_STRIPE
                                                ? UINT16_MAX - 1
                                                : UINT16_MAX;
                    const int port =
                        routing[query.currentNodeId]->GetOutPort(key, selectedShortest, inPort);
                    if (port < 0)
                    {
                        ++missing;
                    }
                    checksum =
                        Mix64(checksum ^ (static_cast<uint64_t>(port + 1) << 1) ^ selectedShortest);
                    if (bytes != nullptr)
                    {
                        WriteLittle16(*bytes, static_cast<uint16_t>(port));
                        bytes->put(static_cast<char>(selectedShortest));
                    }
                };
            measurements.push_back(Measure(name, iterations, warmup, batchSize, lookup));
            for (uint64_t index = 0; index < signatureCount; ++index)
            {
                uint64_t checksum = 0;
                uint64_t missing = 0;
                lookup(index, checksum, missing, &signature);
                if (selector == ns3::MultipathSelector::HASH64 ||
                    selector == ns3::MultipathSelector::ADAPTIVE)
                {
                    lookup(index, checksum, missing, &commonSignature);
                }
            }
        }

        std::ofstream output(argv[8]);
        output << std::fixed << std::setprecision(6) << "{\n"
               << "  \"mode\": \"" << mode << "\",\n"
               << "  \"queryCount\": " << queries.size() << ",\n"
               << "  \"iterations\": " << iterations << ",\n"
               << "  \"parseSeconds\": " << parseSeconds << ",\n"
               << "  \"installSeconds\": " << installSeconds << ",\n"
               << "  \"rssBeforeBytes\": " << rssBefore << ",\n"
               << "  \"rssAfterInstallBytes\": " << rssAfterInstall << ",\n"
               << "  \"heapInUseAfterInstallBytes\": " << heapAfterInstall.size_in_use << ",\n"
               << "  \"allocatorReleasedBytes\": " << allocatorReleased << ",\n"
               << "  \"rssAfterAllocatorReliefBytes\": " << rssAfterAllocatorRelief << ",\n"
               << "  \"heapInUseAfterAllocatorReliefBytes\": "
               << heapAfterAllocatorRelief.size_in_use << ",\n"
               << "  \"peakRssBytes\": " << peakRss << ",\n"
               << "  \"measurements\": [\n";
        for (std::size_t index = 0; index < measurements.size(); ++index)
        {
            const auto& value = measurements[index];
            output << "    {\"name\": \"" << value.name << "\", \"meanNs\": " << value.meanNs
                   << ", \"p50BatchMeanNs\": " << value.p50BatchMeanNs
                   << ", \"p99BatchMeanNs\": " << value.p99BatchMeanNs
                   << ", \"checksum\": " << value.checksum << ", \"missing\": " << value.missing
                   << "}" << (index + 1 == measurements.size() ? "\n" : ",\n");
        }
        output << "  ]\n}\n";
        ns3::Simulator::Destroy();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        ns3::Simulator::Destroy();
        return 2;
    }
}
