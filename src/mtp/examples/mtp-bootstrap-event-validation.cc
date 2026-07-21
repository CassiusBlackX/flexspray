/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * @file
 * @ingroup mtp
 *
 * Verify that automatic MTP partitioning localizes pre-run node events before
 * any bootstrap callback executes.
 */

#include "ns3/core-module.h"
#include "ns3/mtp-interface.h"
#include "ns3/network-module.h"

#include <array>
#include <atomic>
#include <iostream>

using namespace ns3;

namespace
{

constexpr uint32_t WORKER_COUNT = 2;
constexpr uint32_t BOOTSTRAP_EVENTS_PER_WORKER = 2;

std::array<std::atomic<uint32_t>, WORKER_COUNT + 1> g_bootstrapCounts{};
std::array<std::atomic<uint32_t>, WORKER_COUNT + 1> g_followupCounts{};
std::atomic<uint32_t> g_pendingChecks{0};
std::atomic<uint32_t> g_incompleteBootstrapRounds{0};
std::atomic<uint32_t> g_earlyFollowups{0};
std::atomic<uint32_t> g_positiveEvents{0};
std::atomic<uint32_t> g_publicEvents{0};
std::atomic<uint32_t> g_wrongLp{0};

void
RunFollowup(uint32_t expectedLp)
{
    if (Simulator::GetSystemId() != expectedLp)
    {
        g_wrongLp++;
    }
    if (g_bootstrapCounts[expectedLp].load() != BOOTSTRAP_EVENTS_PER_WORKER)
    {
        g_earlyFollowups++;
    }
    g_followupCounts[expectedLp]++;
}

void
RunBootstrap(uint32_t expectedLp)
{
    if (Simulator::GetSystemId() != expectedLp)
    {
        g_wrongLp++;
    }
    g_bootstrapCounts[expectedLp]++;

    constexpr uint32_t expectedLocalizedEvents = BOOTSTRAP_EVENTS_PER_WORKER + 1;
    for (uint32_t workerId = 1; workerId <= WORKER_COUNT; ++workerId)
    {
        if (MtpInterface::GetSystem(workerId)->GetPendingEventCount() < expectedLocalizedEvents)
        {
            g_incompleteBootstrapRounds++;
        }
    }

    EventId followup = Simulator::ScheduleNow(&RunFollowup, expectedLp);
    if (followup.IsPending() && Simulator::GetDelayLeft(followup) == TimeStep(0))
    {
        g_pendingChecks++;
    }
}

void
RunPositiveTimeEvent(uint32_t expectedLp)
{
    if (Simulator::GetSystemId() != expectedLp)
    {
        g_wrongLp++;
    }
    g_positiveEvents++;
}

void
RunPublicEvent()
{
    if (Simulator::GetSystemId() != 0)
    {
        g_wrongLp++;
    }
    g_publicEvents++;
}

} // namespace

int
main(int argc, char* argv[])
{
    bool testing = false;
    CommandLine cmd(__FILE__);
    cmd.AddValue("test", "Print regression-test output", testing);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::MultithreadedSimulatorImpl::MaxThreads", UintegerValue(WORKER_COUNT));
    GlobalValue::Bind("SimulatorImplementationType",
                      StringValue("ns3::MultithreadedSimulatorImpl"));

    NodeContainer nodes;
    nodes.Create(WORKER_COUNT);

    EventId publicEvent = Simulator::ScheduleNow(&RunPublicEvent);
    const bool publicPendingBeforeRun = publicEvent.IsPending();

    for (uint32_t eventIndex = 0; eventIndex < BOOTSTRAP_EVENTS_PER_WORKER; ++eventIndex)
    {
        for (uint32_t nodeIndex = 0; nodeIndex < WORKER_COUNT; ++nodeIndex)
        {
            Simulator::ScheduleWithContext(nodes.Get(nodeIndex)->GetId(),
                                           TimeStep(0),
                                           &RunBootstrap,
                                           nodeIndex + 1);
        }
    }
    for (uint32_t nodeIndex = 0; nodeIndex < WORKER_COUNT; ++nodeIndex)
    {
        Simulator::ScheduleWithContext(nodes.Get(nodeIndex)->GetId(),
                                       NanoSeconds(1),
                                       &RunPositiveTimeEvent,
                                       nodeIndex + 1);
    }

    Simulator::Run();

    uint32_t bootstrapEvents = 0;
    uint32_t followupEvents = 0;
    for (uint32_t workerId = 1; workerId <= WORKER_COUNT; ++workerId)
    {
        bootstrapEvents += g_bootstrapCounts[workerId].load();
        followupEvents += g_followupCounts[workerId].load();
    }
    const uint32_t expectedBootstrapEvents = WORKER_COUNT * BOOTSTRAP_EVENTS_PER_WORKER;
    const bool publicExpiredAfterRun = publicEvent.IsExpired();
    const bool passed = publicPendingBeforeRun && publicExpiredAfterRun &&
                        bootstrapEvents == expectedBootstrapEvents &&
                        followupEvents == expectedBootstrapEvents &&
                        g_pendingChecks.load() == expectedBootstrapEvents &&
                        g_incompleteBootstrapRounds.load() == 0 && g_earlyFollowups.load() == 0 &&
                        g_positiveEvents.load() == WORKER_COUNT && g_publicEvents.load() == 1 &&
                        g_wrongLp.load() == 0;

    if (testing)
    {
        std::cout << "TEST : 0 : " << (passed ? "PASSED" : "FAILED")
                  << " bootstrap=" << bootstrapEvents << " followup=" << followupEvents
                  << " pending-checks=" << g_pendingChecks.load()
                  << " incomplete-bootstrap=" << g_incompleteBootstrapRounds.load()
                  << " early-followups=" << g_earlyFollowups.load()
                  << " positive=" << g_positiveEvents.load() << " public=" << g_publicEvents.load()
                  << " wrong-lp=" << g_wrongLp.load() << std::endl;
    }

    Simulator::Destroy();
    return passed ? 0 : 1;
}
