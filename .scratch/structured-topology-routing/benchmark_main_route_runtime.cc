// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/enum.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
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
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>

namespace
{
struct Query
{
    uint32_t current;
    uint32_t destination;
};

struct Measurement
{
    std::string name;
    double meanNs;
    double p50;
    double p99;
    uint64_t checksum;
    uint64_t missing;
};

uint64_t
Read64(std::istream& input)
{
    uint64_t value = 0;
    for (uint32_t shift = 0; shift < 64; shift += 8)
    {
        const int byte = input.get();
        if (byte < 0)
        {
            throw std::runtime_error("truncated query corpus");
        }
        value |= static_cast<uint64_t>(static_cast<uint8_t>(byte)) << shift;
    }
    return value;
}

uint32_t
Read32(std::istream& input)
{
    uint32_t value = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8)
    {
        const int byte = input.get();
        if (byte < 0)
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
    char magic[8]{};
    input.read(magic, 8);
    if (std::string_view(magic, 8) != "UBQRY001")
    {
        throw std::runtime_error("bad query corpus");
    }
    const uint64_t count = Read64(input);
    std::vector<Query> result;
    result.reserve(count);
    for (uint64_t index = 0; index < count; ++index)
    {
        result.push_back({Read32(input), Read32(input)});
    }
    return result;
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
CurrentRss()
{
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
    return info.resident_size;
}

uint64_t
PeakRss()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

double
Percentile(std::vector<double> values, double percentile)
{
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(percentile * values.size()));
    return values[std::max<std::size_t>(1, rank) - 1];
}

template <typename Function>
Measurement
Measure(std::string name, uint64_t iterations, Function&& function)
{
    constexpr uint64_t warmup = 100000;
    constexpr uint64_t batch = 4096;
    uint64_t checksum = 0, missing = 0;
    for (uint64_t index = 0; index < warmup; ++index)
    {
        function(index, checksum, missing);
    }
    checksum = missing = 0;
    std::vector<double> batches;
    const auto started = std::chrono::steady_clock::now();
    for (uint64_t begin = 0; begin < iterations; begin += batch)
    {
        const uint64_t end = std::min(iterations, begin + batch);
        const auto batchStarted = std::chrono::steady_clock::now();
        for (uint64_t index = begin; index < end; ++index)
        {
            function(index, checksum, missing);
        }
        batches.push_back(std::chrono::duration<double, std::nano>(
                              std::chrono::steady_clock::now() - batchStarted)
                              .count() /
                          (end - begin));
    }
    const double elapsed =
        std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started)
            .count();
    return {name,
            elapsed / iterations,
            Percentile(batches, 0.5),
            Percentile(batches, 0.99),
            checksum,
            missing};
}
} // namespace

int
main(int argc, char* argv[])
{
    if (argc != 8)
    {
        std::cerr << "usage: benchmark NODE TOPOLOGY ROUTING QUERIES ITERATIONS OUTPUT SEED\n";
        return 2;
    }
    const uint64_t iterations = std::stoull(argv[5]);
    const uint64_t seed = std::stoull(argv[7]);
    const uint64_t rssBefore = CurrentRss();
    const auto installStarted = std::chrono::steady_clock::now();
    utils::UbUtils::Get()->CreateNode(argv[1]);
    utils::UbUtils::Get()->CreateTopo(argv[2]);
    const double topologySeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - installStarted).count();
    const auto routeStarted = std::chrono::steady_clock::now();
    utils::UbUtils::Get()->AddRoutingTable(argv[3]);
    const double routeSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - routeStarted).count();
    const uint64_t rssAfter = CurrentRss();
    malloc_statistics_t heapAfter{};
    malloc_zone_statistics(nullptr, &heapAfter);
    const std::size_t allocatorReleased = malloc_zone_pressure_relief(nullptr, 0);
    const uint64_t rssAfterRelief = CurrentRss();
    malloc_statistics_t heapAfterRelief{};
    malloc_zone_statistics(nullptr, &heapAfterRelief);
    const uint64_t peak = PeakRss();

    std::vector<ns3::Ptr<ns3::UbRoutingProcess>> routing;
    for (uint32_t node = 0; node < ns3::NodeList::GetNNodes(); ++node)
    {
        routing.push_back(
            ns3::NodeList::GetNode(node)->GetObject<ns3::UbSwitch>()->GetRoutingProcess());
    }
    const auto queries = ReadQueries(argv[4]);
    auto queryAt = [&](uint64_t index) -> const Query& {
        return queries[Mix64(index ^ seed) % queries.size()];
    };

    std::vector<Measurement> measurements;
    measurements.push_back(Measure("candidate-set",
                                   iterations,
                                   [&](uint64_t index, uint64_t& checksum, uint64_t& missing) {
                                       const auto& query = queryAt(index);
                                       const auto ports = routing[query.current]->GetAllOutPorts(
                                           query.destination);
                                       if (ports.empty())
                                       {
                                           ++missing;
                                       }
                                       for (uint16_t port : ports)
                                       {
                                           checksum = Mix64(checksum ^ (port + 1));
                                       }
                                   }));
    for (const std::string algorithm : {"HASH", "ADAPTIVE"})
    {
        for (const auto& process : routing)
        {
            process->SetAttribute("RoutingAlgorithm", ns3::StringValue(algorithm));
        }
        measurements.push_back(Measure(
            algorithm,
            iterations,
            [&](uint64_t index, uint64_t& checksum, uint64_t& missing) {
                const auto& query = queryAt(index);
                ns3::RoutingKey key{utils::NodeIdToIp(query.current).Get(),
                                    query.destination,
                                    static_cast<uint16_t>(index),
                                    4792,
                                    0,
                                    false,
                                    false,
                                    true};
                bool shortest = false;
                const int port = routing[query.current]->GetOutPort(key, shortest);
                if (port < 0)
                {
                    ++missing;
                }
                checksum = Mix64(checksum ^ (static_cast<uint64_t>(port + 1) << 1) ^ shortest);
            }));
    }

    std::ofstream output(argv[6]);
    output << std::fixed << std::setprecision(6) << "{\n"
           << "  \"commit\": \"0ca0e95\",\n"
           << "  \"queryCount\": " << queries.size() << ",\n"
           << "  \"iterations\": " << iterations << ",\n"
           << "  \"topologyInstallSeconds\": " << topologySeconds << ",\n"
           << "  \"routingLoadSeconds\": " << routeSeconds << ",\n"
           << "  \"rssBeforeBytes\": " << rssBefore << ",\n"
           << "  \"rssAfterInstallBytes\": " << rssAfter << ",\n"
           << "  \"heapInUseAfterInstallBytes\": " << heapAfter.size_in_use << ",\n"
           << "  \"allocatorReleasedBytes\": " << allocatorReleased << ",\n"
           << "  \"rssAfterAllocatorReliefBytes\": " << rssAfterRelief << ",\n"
           << "  \"heapInUseAfterAllocatorReliefBytes\": " << heapAfterRelief.size_in_use << ",\n"
           << "  \"peakRssBytes\": " << peak << ",\n"
           << "  \"measurements\": [\n";
    for (std::size_t index = 0; index < measurements.size(); ++index)
    {
        const auto& value = measurements[index];
        output << "    {\"name\": \"" << value.name << "\", \"meanNs\": " << value.meanNs
               << ", \"p50BatchMeanNs\": " << value.p50 << ", \"p99BatchMeanNs\": " << value.p99
               << ", \"checksum\": " << value.checksum << ", \"missing\": " << value.missing << "}"
               << (index + 1 == measurements.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    ns3::Simulator::Destroy();
}
