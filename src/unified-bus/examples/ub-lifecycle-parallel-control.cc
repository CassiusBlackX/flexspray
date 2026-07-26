/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * @file
 * @ingroup unified-bus
 *
 * Tracer bullet for owner-routed packet lifecycle control under MPI and hybrid execution.
 */

#include "ns3/core-module.h"
#include "ns3/mpi-interface.h"
#include "ns3/mtp-interface.h"
#include "ns3/network-module.h"
#include "ns3/ub-lifecycle.h"

#include <iostream>
#include <limits>
#include <mpi.h>

using namespace ns3;

namespace
{

uint32_t g_terminalLp = std::numeric_limits<uint32_t>::max();

void
TerminateRemotePacket(Ptr<Packet> packet)
{
    if (MtpInterface::isEnabled())
    {
        g_terminalLp = MtpInterface::GetSystem()->GetSystemId();
    }
    UbLifecycleCoordinator::TerminatePacket(packet, UbPacketTerminal::ENDPOINT_CONSUMED);
}

} // namespace

int
main(int argc, char* argv[])
{
    bool hybrid = false;
    bool localMtp = false;
    bool preRunControl = false;
    bool testing = false;
    CommandLine cmd(__FILE__);
    cmd.AddValue("hybrid", "Route lifecycle control to the target node's MTP LP", hybrid);
    cmd.AddValue("local-mtp", "Route lifecycle control between local MTP LPs", localMtp);
    cmd.AddValue("pre-run-control",
                 "Route an explicitly sourced lifecycle control before Simulator::Run",
                 preRunControl);
    cmd.AddValue("test", "Print regression-test output", testing);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(hybrid && localMtp, "Select either hybrid or local MTP execution");
    NS_ABORT_MSG_IF(preRunControl && !localMtp,
                    "Pre-run lifecycle control is a local MTP regression mode");
    if (localMtp)
    {
        MtpInterface::Enable(2, 2);
    }
    else if (hybrid)
    {
        MtpInterface::Enable(2);
    }
    if (!localMtp)
    {
        MpiInterface::Enable(&argc, &argv);
        if (MpiInterface::GetSize() != 2)
        {
            std::cerr << "ub-lifecycle-parallel-control requires exactly two MPI ranks"
                      << std::endl;
            MpiInterface::Disable();
            return 2;
        }
    }
    const uint32_t rank = localMtp ? 0 : MpiInterface::GetSystemId();

    NodeContainer nodes;
    nodes.Add(CreateObject<Node>(localMtp ? 1 : 0));
    nodes.Add(CreateObject<Node>(localMtp ? 2 : 1));
    if (!localMtp)
    {
        MpiInterface::BoundLookAhead(NanoSeconds(10));
    }

    const UbLifecycleResourceIdentity ownerIdentity{
        nodes.Get(0)->GetId(),
        UbLifecycleResourceKind::RTP_ENDPOINT,
        17,
        1,
    };
    const UbPacketAttemptIdentity packetIdentity{ownerIdentity, 1};
    Ptr<UbLifecycleCoordinator> coordinator;
    Ptr<Packet> packet = Create<Packet>(64);
    if (rank == 0)
    {
        coordinator = UbLifecycleCoordinator::GetOrCreate(nodes.Get(0));
        const UbLifecycleResourceIdentity opened =
            coordinator->OpenPacketOwner(UbLifecycleResourceKind::RTP_ENDPOINT, 17);
        const UbPacketAttemptIdentity registered =
            coordinator->RegisterPacketAttempt(opened, packet);
        NS_ABORT_MSG_IF(opened != ownerIdentity || registered != packetIdentity,
                        "Parallel lifecycle tracer bullet constructed an unexpected identity");
    }
    if (preRunControl)
    {
        UbLifecycleControlRecord record;
        record.sourceNodeId = nodes.Get(1)->GetId();
        record.targetNodeId = nodes.Get(0)->GetId();
        record.operation = UbLifecycleControlOperation::PACKET_TERMINAL;
        record.resource = ownerIdentity;
        record.packet = packetIdentity;
        record.value = static_cast<uint64_t>(UbPacketTerminal::ENDPOINT_CONSUMED);
        UbParallelLifecycleAdapter::RouteControl(record);
    }
    else if (localMtp || rank == 1)
    {
        if (!localMtp)
        {
            UbPacketLifecycleTag tag;
            tag.SetIdentity(packetIdentity);
            packet->AddPacketTag(tag);
        }
        Simulator::ScheduleWithContext(nodes.Get(1)->GetId(),
                                       NanoSeconds(1),
                                       &TerminateRemotePacket,
                                       packet);
    }

    Simulator::Stop(NanoSeconds(30));
    Simulator::Run();

    const uint32_t statsNodeId = rank == 0 ? nodes.Get(0)->GetId() : nodes.Get(1)->GetId();
    const UbParallelLifecycleSnapshot control =
        UbParallelLifecycleAdapter::GetOrCreate(NodeList::GetNode(statsNodeId))->GetSnapshot();
    uint64_t balance = 0;
    uint64_t terminal = 0;
    if (rank == 0)
    {
        const UbLifecycleResourceSnapshot resource = coordinator->GetSnapshot(ownerIdentity);
        balance = resource.inFlightPackets;
        terminal = resource.endpointConsumed;
    }
    const UbParallelLifecycleSnapshot sourceControl =
        localMtp ? UbParallelLifecycleAdapter::GetOrCreate(nodes.Get(1))->GetSnapshot() : control;
    const uint32_t ownerLp = localMtp ? MtpInterface::RequireNodeLocalLpId(nodes.Get(0)->GetId()) : 0;
    const uint32_t sourceLp = localMtp ? MtpInterface::RequireNodeLocalLpId(nodes.Get(1)->GetId()) : 0;
    const bool localPassed = rank == 0
                                 ? control.ownerSequence == 1 && control.received == 1 &&
                                       control.applied == 1 && control.pending == 0 && balance == 0 &&
                                       terminal == 1 && (!localMtp || sourceControl.sent == 1)
                                 : control.sent == 1 && control.pending == 0;
    int localResult = localPassed ? 1 : 0;
    int globalResult = localResult;
    if (!localMtp)
    {
        MPI_Allreduce(&localResult,
                      &globalResult,
                      1,
                      MPI_INT,
                      MPI_MIN,
                      MpiInterface::GetCommunicator());
    }

    if (testing)
    {
        std::cout << "TEST : " << rank << " : " << (globalResult ? "PASSED" : "FAILED")
                  << " owner-seq=" << control.ownerSequence << " sent=" << control.sent
                  << " received=" << control.received << " applied=" << control.applied
                  << " duplicate=" << control.duplicate << " stale=" << control.stale
                  << " pending=" << control.pending << " balance=" << balance
                  << " terminal=" << terminal << " source-sent=" << sourceControl.sent
                  << " terminal-lp=" << g_terminalLp << " owner-lp=" << ownerLp
                  << " source-lp=" << sourceLp << std::endl;
    }

    Simulator::Destroy();
    if (!localMtp)
    {
        MpiInterface::Disable();
    }
    return globalResult ? 0 : 1;
}
