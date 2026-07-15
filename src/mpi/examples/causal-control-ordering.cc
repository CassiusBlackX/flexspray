/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * @file
 * @ingroup mpi
 *
 * Verify fixed causal-control delivery to the target rank and, in hybrid mode,
 * the target node's owning LP at the requested visibility time.
 */

#include "ns3/core-module.h"
#include "ns3/mpi-interface.h"
#include "ns3/mtp-interface.h"
#include "ns3/network-module.h"

#include <iostream>
#include <limits>
#include <mpi.h>

using namespace ns3;

namespace
{

bool g_received = false;
bool g_wrongContext = false;
bool g_wrongLp = false;
uint64_t g_receiveTime = std::numeric_limits<uint64_t>::max();
uint64_t g_receivedValue = 0;

void
ReceiveControl(const ParallelCommunicationInterface::CausalControlRecord& record)
{
    g_received = true;
    g_receiveTime = Simulator::Now().GetTimeStep();
    g_receivedValue = record.payload[0];
    g_wrongContext = Simulator::GetContext() != record.targetNodeId;
    if (MtpInterface::isEnabled())
    {
        g_wrongLp = Simulator::GetSystemId() !=
                    MtpInterface::RequireNodeLocalLpId(record.targetNodeId);
    }
}

void
SendControl(uint32_t targetNodeId)
{
    ParallelCommunicationInterface::CausalControlRecord record;
    record.targetNodeId = targetNodeId;
    record.protocol = 17;
    record.visibleTs = NanoSeconds(10).GetInteger();
    record.orderKey = 3;
    record.payload[0] = 0xC0A5A1;
    MpiInterface::SendCausalControl(record);
}

} // namespace

int
main(int argc, char* argv[])
{
    bool hybrid = false;
    bool nullMessage = false;
    bool testing = false;
    CommandLine cmd(__FILE__);
    cmd.AddValue("hybrid", "Route the received record to the target node's MTP LP", hybrid);
    cmd.AddValue("nullmsg", "Verify that null-message MPI rejects causal control", nullMessage);
    cmd.AddValue("test", "Print regression-test output", testing);
    cmd.Parse(argc, argv);

    if (nullMessage)
    {
        GlobalValue::Bind("SimulatorImplementationType",
                          StringValue("ns3::NullMessageSimulatorImpl"));
    }
    if (hybrid)
    {
        MtpInterface::Enable(2);
    }
    MpiInterface::Enable(&argc, &argv);

    const uint32_t rank = MpiInterface::GetSystemId();
    if (MpiInterface::GetSize() != 2)
    {
        std::cerr << "causal-control-ordering requires exactly two MPI ranks" << std::endl;
        MpiInterface::Disable();
        return 2;
    }

    if (nullMessage)
    {
        const bool localPassed = !MpiInterface::SupportsCausalControl();
        int localResult = localPassed ? 1 : 0;
        int globalResult = 0;
        MPI_Allreduce(&localResult,
                      &globalResult,
                      1,
                      MPI_INT,
                      MPI_MIN,
                      MpiInterface::GetCommunicator());
        if (testing)
        {
            std::cout << "TEST : " << rank << " : "
                      << (globalResult ? "PASSED" : "FAILED") << std::endl;
        }
        MpiInterface::Disable();
        return globalResult ? 0 : 1;
    }

    NodeContainer nodes;
    nodes.Add(CreateObject<Node>(0));
    nodes.Add(CreateObject<Node>(1));
    MpiInterface::BoundLookAhead(NanoSeconds(10));
    MpiInterface::SetCausalControlHandler(&ReceiveControl);

    if (rank == 0)
    {
        Simulator::ScheduleWithContext(nodes.Get(0)->GetId(), NanoSeconds(1), &SendControl, 1);
    }
    Simulator::Stop(NanoSeconds(20));
    Simulator::Run();

    const bool localPassed = rank == 0 ||
                             (g_received && !g_wrongContext && !g_wrongLp &&
                              g_receiveTime == NanoSeconds(10).GetTimeStep() &&
                              g_receivedValue == 0xC0A5A1);
    int localResult = localPassed ? 1 : 0;
    int globalResult = 0;
    MPI_Allreduce(&localResult,
                  &globalResult,
                  1,
                  MPI_INT,
                  MPI_MIN,
                  MpiInterface::GetCommunicator());

    if (testing)
    {
        std::cout << "TEST : " << rank << " : " << (globalResult ? "PASSED" : "FAILED");
        if (rank == 1)
        {
            std::cout << " receive-time=" << g_receiveTime << " wrong-context=" << g_wrongContext
                      << " wrong-lp=" << g_wrongLp << " value=" << g_receivedValue;
        }
        std::cout << std::endl;
    }

    Simulator::Destroy();
    MpiInterface::Disable();
    return globalResult ? 0 : 1;
}
