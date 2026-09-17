/*
 *  Copyright 2013. Lawrence Livermore National Security, LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Steven Smith <smith84@llnl.gov>
 *
 */

/**
 * @file
 * @ingroup mpi
 * Declaration of class ns3::ParallelCommunicationInterface.
 */

#ifndef NS3_PARALLEL_COMMUNICATION_INTERFACE_H
#define NS3_PARALLEL_COMMUNICATION_INTERFACE_H

#include "ns3/buffer.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"

#include <array>
#include <cstdint>
#include <list>
#include <functional>
#include <mpi.h>
#include <stdint.h>

namespace ns3
{

/**
 * @ingroup mpi
 *
 * @brief Pure virtual base class for the interface between ns-3 and
 * the parallel communication layer being used.
 *
 * This class is implemented for each of the parallel versions of
 * SimulatorImpl to manage communication between process (ranks).
 *
 * This interface is called through the singleton MpiInterface class.
 * MpiInterface has the same API as ParallelCommunicationInterface but
 * being a singleton uses static methods to delegate to methods
 * defined in classes that implement the
 * ParallelCommunicationInterface.  For example, SendPacket is likely
 * to be specialized for a specific parallel SimulatorImpl.
 */
class ParallelCommunicationInterface
{
  public:
    using TaskCompletionHandler = std::function<void(uint32_t taskId)>;

    static constexpr std::size_t CAUSAL_CONTROL_PAYLOAD_WORDS = 12;

    struct CausalControlRecord
    {
        uint32_t targetNodeId{0};
        uint32_t protocol{0};
        uint64_t visibleTs{0};
        uint64_t orderKey{0};
        std::array<uint64_t, CAUSAL_CONTROL_PAYLOAD_WORDS> payload{};
    };

    using CausalControlHandler = std::function<void(const CausalControlRecord& record)>;

    /**
     * Destructor
     */
    virtual ~ParallelCommunicationInterface()
    {
    }

    /**
     * @copydoc MpiInterface::Destroy
     */
    virtual void Destroy() = 0;
    /**
     * @copydoc MpiInterface::GetSystemId
     */
    virtual uint32_t GetSystemId() = 0;
    /**
     * @copydoc MpiInterface::GetSize
     */
    virtual uint32_t GetSize() = 0;
    /**
     * @copydoc MpiInterface::IsEnabled
     */
    virtual bool IsEnabled() = 0;
    /**
     * @copydoc MpiInterface::Enable(int* pargc,char*** pargv)
     */
    virtual void Enable(int* pargc, char*** pargv) = 0;
    /**
     * @copydoc MpiInterface::Enable(MPI_Comm communicator)
     */
    virtual void Enable(MPI_Comm communicator) = 0;
    /**
     * @copydoc MpiInterface::Disable
     */
    virtual void Disable() = 0;
    /**
     * @copydoc MpiInterface::SendPacket
     */
    virtual void SendPacket(Ptr<Packet> p, const Time& rxTime, uint32_t node, uint32_t dev) = 0;
    /**
     * @copydoc MpiInterface::SendTaskCompletion
     */
    virtual void SendTaskCompletion(uint32_t taskId,
                                    const Time& completionVisibleTs,
                                    uint32_t rank) = 0;
    /**
     * @copydoc MpiInterface::SetTaskCompletionHandler
     */
    virtual void SetTaskCompletionHandler(TaskCompletionHandler handler) = 0;
    /**
     * Send one fixed-size causal control record to its target node owner.
     */
    virtual void SendCausalControl(const CausalControlRecord& record) = 0;
    /**
     * Register the process-local causal control dispatcher.
     */
    virtual void SetCausalControlHandler(CausalControlHandler handler) = 0;
    /**
     * Return whether this parallel transport supports causal control records.
     */
    virtual bool SupportsCausalControl() const = 0;
    /**
     * @copydoc MpiInterface::GetCommunicator
     */
    virtual MPI_Comm GetCommunicator() = 0;

  private:
};

} // namespace ns3

#endif /* NS3_PARALLEL_COMMUNICATION_INTERFACE_H */
