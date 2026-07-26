/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * @file
 * @ingroup unified-bus
 *
 * Verify causal lifecycle control delivery between local MTP worker LPs.
 */

#include "ns3/core-module.h"
#include "ns3/mtp-interface.h"
#include "ns3/network-module.h"
#include "ns3/ub-lifecycle.h"

#include <iostream>

using namespace ns3;

namespace
{

bool g_targetReachedFiveNs = false;
bool g_terminalHiddenAtTenNs = false;
bool g_terminalVisibleAtTwelveNs = false;
uint32_t g_wrongLp = 0;

void
CheckTargetLp(uint32_t expectedLp)
{
    if (Simulator::GetSystemId() != expectedLp)
    {
        ++g_wrongLp;
    }
}

void
MarkTargetProgress(uint32_t expectedLp)
{
    CheckTargetLp(expectedLp);
    g_targetReachedFiveNs = Simulator::Now() == NanoSeconds(5);
}

void
CheckTerminalVisibility(Ptr<UbLifecycleCoordinator> coordinator,
                        UbLifecycleResourceIdentity owner,
                        uint32_t expectedLp,
                        bool shouldBeVisible)
{
    CheckTargetLp(expectedLp);
    const UbLifecycleResourceSnapshot snapshot = coordinator->GetSnapshot(owner);
    const bool visible = snapshot.endpointConsumed == 1 && snapshot.inFlightPackets == 0;
    if (shouldBeVisible)
    {
        g_terminalVisibleAtTwelveNs = visible && Simulator::Now() == NanoSeconds(12);
    }
    else
    {
        g_terminalHiddenAtTenNs = !visible && Simulator::Now() == NanoSeconds(10);
    }
}

void
TerminateRemotePacket(Ptr<Packet> packet, uint32_t expectedLp)
{
    CheckTargetLp(expectedLp);
    UbLifecycleCoordinator::TerminatePacket(packet, UbPacketTerminal::ENDPOINT_CONSUMED);
}

} // namespace

int
main(int argc, char* argv[])
{
    bool testing = false;
    bool bindLookAhead = true;
    CommandLine cmd(__FILE__);
    cmd.AddValue("test", "Print regression-test output", testing);
    cmd.AddValue("bind-lookahead", "Bind a finite positive MTP lookahead", bindLookAhead);
    cmd.Parse(argc, argv);

    constexpr uint32_t ownerLp = 1;
    constexpr uint32_t sourceLp = 2;
    MtpInterface::Enable(2, 2);
    if (bindLookAhead)
    {
        MtpInterface::BoundLookAhead(NanoSeconds(10));
    }

    NodeContainer nodes;
    nodes.Add(CreateObject<Node>(ownerLp));
    nodes.Add(CreateObject<Node>(sourceLp));

    Ptr<UbLifecycleCoordinator> coordinator = UbLifecycleCoordinator::GetOrCreate(nodes.Get(0));
    const UbLifecycleResourceIdentity owner =
        coordinator->OpenPacketOwner(UbLifecycleResourceKind::RTP_ENDPOINT, 17);
    Ptr<Packet> packet = Create<Packet>(64);
    coordinator->RegisterPacketAttempt(owner, packet);

    Simulator::ScheduleWithContext(nodes.Get(1)->GetId(),
                                   NanoSeconds(1),
                                   &TerminateRemotePacket,
                                   packet,
                                   sourceLp);
    Simulator::ScheduleWithContext(nodes.Get(0)->GetId(),
                                   NanoSeconds(5),
                                   &MarkTargetProgress,
                                   ownerLp);
    Simulator::ScheduleWithContext(nodes.Get(0)->GetId(),
                                   NanoSeconds(10),
                                   &CheckTerminalVisibility,
                                   coordinator,
                                   owner,
                                   ownerLp,
                                   false);
    Simulator::ScheduleWithContext(nodes.Get(0)->GetId(),
                                   NanoSeconds(12),
                                   &CheckTerminalVisibility,
                                   coordinator,
                                   owner,
                                   ownerLp,
                                   true);
    Simulator::Stop(NanoSeconds(30));
    Simulator::Run();

    const UbLifecycleResourceSnapshot resource = coordinator->GetSnapshot(owner);
    const UbParallelLifecycleSnapshot ownerControl =
        UbParallelLifecycleAdapter::GetOrCreate(nodes.Get(0))->GetSnapshot();
    const UbParallelLifecycleSnapshot sourceControl =
        UbParallelLifecycleAdapter::GetOrCreate(nodes.Get(1))->GetSnapshot();
    const bool passed = g_targetReachedFiveNs && g_terminalHiddenAtTenNs &&
                        g_terminalVisibleAtTwelveNs && g_wrongLp == 0 &&
                        resource.inFlightPackets == 0 && resource.endpointConsumed == 1 &&
                        ownerControl.received == 1 && ownerControl.applied == 1 &&
                        ownerControl.pending == 0 && sourceControl.sent == 1;

    if (testing)
    {
        std::cout << "TEST : 0 : " << (passed ? "PASSED" : "FAILED")
                  << " reached-5ns=" << g_targetReachedFiveNs
                  << " hidden-10ns=" << g_terminalHiddenAtTenNs
                  << " visible-12ns=" << g_terminalVisibleAtTwelveNs
                  << " terminal=" << resource.endpointConsumed
                  << " source-sent=" << sourceControl.sent
                  << " owner-received=" << ownerControl.received
                  << " owner-applied=" << ownerControl.applied << " wrong-lp=" << g_wrongLp
                  << std::endl;
    }

    Simulator::Destroy();
    return passed ? 0 : 1;
}
