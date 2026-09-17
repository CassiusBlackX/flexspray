// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/simulator.h"
#include "ns3/ub-utils.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <stdexcept>
#include <string>
#include <sys/resource.h>

namespace
{

uint64_t
CurrentRssBytes()
{
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t result = task_info(mach_task_self(),
                                           MACH_TASK_BASIC_INFO,
                                           reinterpret_cast<task_info_t>(&info),
                                           &count);
    if (result != KERN_SUCCESS)
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
    // macOS reports ru_maxrss in bytes. Linux reports KiB, so this benchmark is macOS-only.
    return static_cast<uint64_t>(usage.ru_maxrss);
}

malloc_statistics_t
HeapStatistics()
{
    malloc_statistics_t statistics{};
    malloc_zone_statistics(nullptr, &statistics);
    return statistics;
}

double
ElapsedSeconds(std::chrono::steady_clock::time_point started)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

} // namespace

int
main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cerr << "usage: benchmark_1024_rss MODE STRUCTURED_PROTOCOL OUTPUT_JSON\n";
        return 2;
    }

    const std::string mode = argv[1];
    std::ifstream input(argv[2]);
    if (!input)
    {
        std::cerr << "cannot open structured protocol: " << argv[2] << '\n';
        return 2;
    }
    std::ofstream output(argv[3]);
    if (!output)
    {
        std::cerr << "cannot open benchmark output: " << argv[3] << '\n';
        return 2;
    }

    const uint64_t rssBeforeParse = CurrentRssBytes();
    const uint64_t peakBeforeParse = PeakRssBytes();
    const malloc_statistics_t heapBeforeParse = HeapStatistics();
    uint64_t rssAfterParse = 0;
    uint64_t rssAfterInstall = 0;
    malloc_statistics_t heapAfterParse{};
    malloc_statistics_t heapAfterInstall{};
    double parseSeconds = 0.0;
    double installSeconds = 0.0;
    std::size_t nodeCount = 0;
    std::size_t linkCount = 0;
    std::size_t tableRouteCount = 0;
    std::size_t portGroupCount = 0;
    std::size_t exactRuleCount = 0;
    std::size_t rangeRuleCount = 0;
    std::size_t topologyRuleCount = 0;
    std::size_t topologyMembershipCount = 0;

    {
        const auto parseStarted = std::chrono::steady_clock::now();
        utils::UbStructuredTopologyConfig config =
            utils::UbUtils::ReadStructuredTopologyConfig(input);
        parseSeconds = ElapsedSeconds(parseStarted);
        rssAfterParse = CurrentRssBytes();
        heapAfterParse = HeapStatistics();

        nodeCount = config.nodes.size();
        linkCount = config.links.size();
        tableRouteCount = config.tableRouteCount != 0 ? config.tableRouteCount
                                                      : config.tableRoutes.size();
        portGroupCount = config.portGroups.size();
        exactRuleCount = config.routingRules.size();
        rangeRuleCount = config.routingRangeRules.size();
        topologyRuleCount = config.topologyRules.size();
        for (const auto& rule : config.topologyRules)
        {
            topologyMembershipCount += rule.nodes.size();
        }

        const auto installStarted = std::chrono::steady_clock::now();
        utils::UbUtils::Get()->InstallStructuredTopology(config, ns3::Time(0), 1);
        installSeconds = ElapsedSeconds(installStarted);
        rssAfterInstall = CurrentRssBytes();
        heapAfterInstall = HeapStatistics();
    }

    const uint64_t rssAfterConfigRelease = CurrentRssBytes();
    const malloc_statistics_t heapAfterConfigRelease = HeapStatistics();
    const size_t allocatorReleasedBytes = malloc_zone_pressure_relief(nullptr, 0);
    const uint64_t rssAfterAllocatorRelief = CurrentRssBytes();
    const malloc_statistics_t heapAfterAllocatorRelief = HeapStatistics();
    const uint64_t peakAfterInstall = PeakRssBytes();

    output << std::fixed << std::setprecision(6) << "{\n"
           << "  \"mode\": \"" << mode << "\",\n"
           << "  \"nodeCount\": " << nodeCount << ",\n"
           << "  \"linkCount\": " << linkCount << ",\n"
           << "  \"tableRouteCount\": " << tableRouteCount << ",\n"
           << "  \"portGroupCount\": " << portGroupCount << ",\n"
           << "  \"exactRuleCount\": " << exactRuleCount << ",\n"
           << "  \"rangeRuleCount\": " << rangeRuleCount << ",\n"
           << "  \"topologyRuleCount\": " << topologyRuleCount << ",\n"
           << "  \"topologyMembershipCount\": " << topologyMembershipCount << ",\n"
           << "  \"parseSeconds\": " << parseSeconds << ",\n"
           << "  \"installSeconds\": " << installSeconds << ",\n"
           << "  \"rssBeforeParseBytes\": " << rssBeforeParse << ",\n"
           << "  \"peakBeforeParseBytes\": " << peakBeforeParse << ",\n"
           << "  \"rssAfterParseBytes\": " << rssAfterParse << ",\n"
           << "  \"rssAfterInstallBytes\": " << rssAfterInstall << ",\n"
           << "  \"rssAfterConfigReleaseBytes\": " << rssAfterConfigRelease << ",\n"
           << "  \"allocatorReleasedBytes\": " << allocatorReleasedBytes << ",\n"
           << "  \"rssAfterAllocatorReliefBytes\": " << rssAfterAllocatorRelief << ",\n"
           << "  \"heapInUseBeforeParseBytes\": " << heapBeforeParse.size_in_use << ",\n"
           << "  \"heapInUseAfterParseBytes\": " << heapAfterParse.size_in_use << ",\n"
           << "  \"heapInUseAfterInstallBytes\": " << heapAfterInstall.size_in_use << ",\n"
           << "  \"heapInUseAfterConfigReleaseBytes\": " << heapAfterConfigRelease.size_in_use
           << ",\n"
           << "  \"heapInUseAfterAllocatorReliefBytes\": " << heapAfterAllocatorRelief.size_in_use
           << ",\n"
           << "  \"heapReservedAfterConfigReleaseBytes\": " << heapAfterConfigRelease.size_allocated
           << ",\n"
           << "  \"peakRssBytes\": " << peakAfterInstall << "\n"
           << "}\n";

    ns3::Simulator::Destroy();
    return 0;
}
