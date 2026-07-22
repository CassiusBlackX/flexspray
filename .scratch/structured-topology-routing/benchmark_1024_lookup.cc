// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/node-list.h"
#include "ns3/simulator.h"
#include "ns3/ub-network-address.h"
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr char kQueryMagic[] = "UBQRY001";
constexpr uint64_t kDefaultIterations = 10000000;
constexpr uint64_t kDefaultWarmupIterations = 100000;
constexpr uint64_t kDefaultBatchSize = 4096;
constexpr uint64_t kRandomSeed = 0x1d50d5eedULL;

struct QueryRecord
{
    uint32_t currentNodeId;
    uint32_t destinationIp;
};

static_assert(sizeof(QueryRecord) == 8);

struct Measurement
{
    std::string distribution;
    std::string lookupPath;
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
    std::string mode;
    std::string protocolPath;
    std::string exhaustiveQueryPath;
    std::string workloadQueryPath;
    std::string outputPath;
    uint64_t iterations{kDefaultIterations};
    uint64_t warmupIterations{kDefaultWarmupIterations};
    uint64_t batchSize{kDefaultBatchSize};
};

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
            "usage: benchmark_1024_lookup MODE PROTOCOL EXHAUSTIVE_QUERIES "
            "WORKLOAD_QUERIES OUTPUT_JSON [--iterations N] [--warmup N] [--batch-size N]");
    }
    Options options{argv[1], argv[2], argv[3], argv[4], argv[5]};
    if (options.mode != "table" && options.mode != "topology-rule")
    {
        throw std::runtime_error("MODE must be table or topology-rule");
    }
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

double
Percentile(std::vector<double> values, double percentile)
{
    std::sort(values.begin(), values.end());
    const std::size_t rank =
        static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(1, rank) - 1];
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

template <typename Lookup>
Measurement
Measure(std::string distribution,
        std::string lookupPath,
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
        const auto batchElapsed = std::chrono::steady_clock::now() - batchStarted;
        batchMeans.push_back(std::chrono::duration<double, std::nano>(batchElapsed).count() /
                             static_cast<double>(end - begin));
    }
    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - allStarted).count();

    return Measurement{
        std::move(distribution),
        std::move(lookupPath),
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

std::string
EscapeJson(const std::string& value)
{
    std::string escaped;
    for (char character : value)
    {
        if (character == '\\' || character == '"')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

void
WriteJson(const Options& options,
          std::size_t nodeCount,
          std::size_t exhaustiveQueryCount,
          std::size_t workloadQueryCount,
          double parseSeconds,
          double installSeconds,
          double corpusSeconds,
          const std::vector<Measurement>& measurements)
{
    std::ofstream output(options.outputPath);
    if (!output)
    {
        throw std::runtime_error("cannot open benchmark output: " + options.outputPath);
    }
    output << std::fixed << std::setprecision(6) << "{\n"
           << "  \"mode\": \"" << EscapeJson(options.mode) << "\",\n"
           << "  \"nodeCount\": " << nodeCount << ",\n"
           << "  \"exhaustiveCorpusQueries\": " << exhaustiveQueryCount << ",\n"
           << "  \"workloadCorpusQueries\": " << workloadQueryCount << ",\n"
           << "  \"iterationsPerMeasurement\": " << options.iterations << ",\n"
           << "  \"warmupIterationsPerMeasurement\": " << options.warmupIterations << ",\n"
           << "  \"randomSeed\": " << kRandomSeed << ",\n"
           << "  \"latencyStatistic\": \"per-query mean within timed batches\",\n"
           << "  \"parseSeconds\": " << parseSeconds << ",\n"
           << "  \"installSeconds\": " << installSeconds << ",\n"
           << "  \"corpusLoadAndSortSeconds\": " << corpusSeconds << ",\n"
           << "  \"measurements\": [\n";
    for (std::size_t index = 0; index < measurements.size(); ++index)
    {
        const auto& measurement = measurements[index];
        output << "    {\n"
               << "      \"distribution\": \"" << measurement.distribution << "\",\n"
               << "      \"lookupPath\": \"" << measurement.lookupPath << "\",\n"
               << "      \"queryCount\": " << measurement.queryCount << ",\n"
               << "      \"batchSize\": " << measurement.batchSize << ",\n"
               << "      \"batchCount\": " << measurement.batchCount << ",\n"
               << "      \"meanNs\": " << measurement.meanNs << ",\n"
               << "      \"p50BatchMeanNs\": " << measurement.p50BatchMeanNs << ",\n"
               << "      \"p99BatchMeanNs\": " << measurement.p99BatchMeanNs << ",\n"
               << "      \"queriesPerSecond\": " << measurement.queriesPerSecond << ",\n"
               << "      \"checksum\": " << measurement.checksum << ",\n"
               << "      \"missing\": " << measurement.missing << "\n"
               << "    }" << (index + 1 == measurements.size() ? "\n" : ",\n");
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
        std::ifstream protocol(options.protocolPath);
        if (!protocol)
        {
            throw std::runtime_error("cannot open structured protocol: " + options.protocolPath);
        }

        std::size_t nodeCount = 0;
        double parseSeconds = 0.0;
        double installSeconds = 0.0;
        {
            const auto parseStarted = std::chrono::steady_clock::now();
            utils::UbStructuredTopologyConfig config =
                utils::UbUtils::ReadStructuredTopologyConfig(protocol);
            parseSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - parseStarted)
                    .count();
            nodeCount = config.nodes.size();
            const auto installStarted = std::chrono::steady_clock::now();
            utils::UbUtils::Get()->InstallStructuredTopology(config, ns3::Time(0), 1);
            installSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - installStarted)
                    .count();
        }

        std::vector<ns3::Ptr<ns3::UbRoutingProcess>> routingProcesses;
        routingProcesses.reserve(ns3::NodeList::GetNNodes());
        for (uint32_t nodeId = 0; nodeId < ns3::NodeList::GetNNodes(); ++nodeId)
        {
            routingProcesses.push_back(
                ns3::NodeList::GetNode(nodeId)->GetObject<ns3::UbSwitch>()->GetRoutingProcess());
        }

        const auto corpusStarted = std::chrono::steady_clock::now();
        std::vector<QueryRecord> exhaustiveQueries = ReadQueries(options.exhaustiveQueryPath);
        std::sort(exhaustiveQueries.begin(),
                  exhaustiveQueries.end(),
                  [](const QueryRecord& left, const QueryRecord& right) {
                      return left.currentNodeId < right.currentNodeId ||
                             (left.currentNodeId == right.currentNodeId &&
                              left.destinationIp < right.destinationIp);
                  });
        const std::vector<QueryRecord> workloadQueries = ReadQueries(options.workloadQueryPath);
        const double corpusSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - corpusStarted).count();

        auto validateCorpus = [&](const std::vector<QueryRecord>& queries,
                                  const std::string& name) {
            for (const auto& query : queries)
            {
                if (query.currentNodeId >= routingProcesses.size())
                {
                    throw std::runtime_error(name + " query references an unknown current node");
                }
            }
        };
        validateCorpus(exhaustiveQueries, "exhaustive");
        validateCorpus(workloadQueries, "workload");

        std::vector<Measurement> measurements;
        auto addMeasurements = [&](const std::string& distribution,
                                   const std::vector<QueryRecord>& queries,
                                   bool randomOrder) {
            auto queryAt = [&](uint64_t index) -> const QueryRecord& {
                const uint64_t selected = randomOrder ? Mix64(index ^ kRandomSeed) % queries.size()
                                                      : index % queries.size();
                return queries[static_cast<std::size_t>(selected)];
            };

            std::vector<uint16_t> shortestPorts;
            std::vector<uint16_t> nonShortestPorts;
            shortestPorts.reserve(32);
            nonShortestPorts.reserve(32);
            measurements.push_back(Measure(
                distribution,
                "candidate-lookup",
                options.iterations,
                options.warmupIterations,
                options.batchSize,
                [&](uint64_t index, uint64_t& checksum, uint64_t& missing) {
                    const QueryRecord& query = queryAt(index);
                    const auto& routing = routingProcesses[query.currentNodeId];
                    bool found = true;
                    if (options.mode == "table")
                    {
                        uint32_t destinationIp = query.destinationIp;
                        routing->GetShortestCandidates(destinationIp,
                                                       std::numeric_limits<uint16_t>::max(),
                                                       shortestPorts);
                    }
                    else
                    {
                        found = routing->TryGetRuleCandidates(query.destinationIp,
                                                              std::numeric_limits<uint16_t>::max(),
                                                              shortestPorts,
                                                              nonShortestPorts);
                    }
                    if (!found || shortestPorts.empty())
                    {
                        ++missing;
                    }
                    checksum += ChecksumPorts(shortestPorts) + index;
                }));

            measurements.push_back(
                Measure(distribution,
                        "get-out-port",
                        options.iterations,
                        options.warmupIterations,
                        options.batchSize,
                        [&](uint64_t index, uint64_t& checksum, uint64_t& missing) {
                            const QueryRecord& query = queryAt(index);
                            ns3::RoutingKey key{
                                utils::NodeIdToIp(query.currentNodeId).Get(),
                                query.destinationIp,
                                static_cast<uint16_t>(index),
                                static_cast<uint16_t>(4792),
                                0,
                                true,
                                false,
                                false,
                            };
                            bool selectedShortestPath = false;
                            const int outPort = routingProcesses[query.currentNodeId]->GetOutPort(
                                key,
                                selectedShortestPath,
                                std::numeric_limits<uint16_t>::max());
                            if (outPort < 0)
                            {
                                ++missing;
                            }
                            checksum += (static_cast<uint64_t>(outPort + 1) << 1) |
                                        static_cast<uint64_t>(selectedShortestPath);
                            checksum += index;
                        }));
        };

        addMeasurements("exhaustive-sequential", exhaustiveQueries, false);
        addMeasurements("exhaustive-random", exhaustiveQueries, true);
        addMeasurements("existing-workload-endpoints", workloadQueries, false);

        WriteJson(options,
                  nodeCount,
                  exhaustiveQueries.size(),
                  workloadQueries.size(),
                  parseSeconds,
                  installSeconds,
                  corpusSeconds,
                  measurements);
        ns3::Simulator::Destroy();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "benchmark_1024_lookup: " << error.what() << '\n';
        ns3::Simulator::Destroy();
        return 2;
    }
}
