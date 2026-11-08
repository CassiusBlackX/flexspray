// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_LINK_HEALTH_H
#define UB_LINK_HEALTH_H

#include "ns3/data-rate.h"
#include "ns3/nstime.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

/**
 * @brief Runtime link sub-health actions from an optional case-local `link_health.csv`.
 *
 * The schedule models bus sub-health produced by optical-module protection: a port that loses
 * lanes keeps running at a lower rate (DEGRADE/RESTORE), and a port that is protection-isolated
 * stops accepting new traffic while already-queued packets drain (ISOLATE/UNISOLATE). Isolation is
 * graceful by design: the physical link stays up so that in-flight packets are delivered. Hard
 * link loss with in-flight packet loss remains the job of `link_fault.csv` (reliable rerouting).
 *
 * Isolation semantics per endpoint role:
 * - switch endpoint (switch-to-switch link): the output is soft-isolated in routing
 *   (SetOutputIsolated): avoided while another candidate exists, used only to drain;
 * - device endpoint: every RTP TP whose local port is the isolated port is suspended inside its
 *   TP group, and so is every remote TP whose peer port is the isolated port, after
 *   `ns3::UbLinkHealth::DeviceIsolationDelay`. Suspended members admit no new WQE segments.
 */
enum class UbLinkHealthAction : uint8_t
{
    DEGRADE,        //!< value = new rate ("300Gbps") or lane fraction ("3/4" of original rate)
    RESTORE,        //!< restore the original rate of the link
    ISOLATE,        //!< gracefully isolate the link (both endpoints)
    UNISOLATE,      //!< undo ISOLATE
    ISOLATE_NODE,   //!< ISOLATE every attached link of the node (portId ignored)
    UNISOLATE_NODE, //!< undo ISOLATE_NODE
    DOWN,           //!< hard link-down: both directions lose every packet from now on
    DOWN_NODE,      //!< DOWN on every attached link of the node (portId ignored)
};

/** @brief One validated `link_health.csv` row. */
struct UbLinkHealthRow
{
    uint32_t line{0};
    uint64_t timeNs{0};
    uint32_t nodeId{0};
    uint32_t portId{0};
    UbLinkHealthAction action{UbLinkHealthAction::DEGRADE};
    std::string value;
    // Resolved DEGRADE parameters (one of the two is set).
    bool hasRate{false};
    DataRate rate;
    uint32_t laneNum{0};
    uint32_t laneDen{0};
};

/**
 * @brief Read and validate `link_health.csv`.
 *
 * Header must be exactly `timeNs,nodeId,portId,action,value`. Missing or header-only files mean no
 * actions. Node/port identity and link attachment are validated against the built topology.
 */
bool TryReadLinkHealthSchedule(const std::string& path,
                               std::vector<UbLinkHealthRow>& rows,
                               std::string& error);

/** @brief Schedule validated actions at their absolute simulation times (file order on ties). */
void ScheduleLinkHealthActions(const std::vector<UbLinkHealthRow>& rows);

/** @brief Immediate runtime operations (also used by tests). */
void UbLinkHealthSetLinkRate(uint32_t nodeId, uint32_t portId, DataRate rate);
void UbLinkHealthDegradeLanes(uint32_t nodeId, uint32_t portId, uint32_t num, uint32_t den);
void UbLinkHealthRestoreLinkRate(uint32_t nodeId, uint32_t portId);
void UbLinkHealthSetLinkIsolated(uint32_t nodeId, uint32_t portId, bool isolated);
void UbLinkHealthSetLinkDown(uint32_t nodeId, uint32_t portId);
void UbLinkHealthSetNodeIsolated(uint32_t nodeId, bool isolated);
/** @brief Forget saved original rates (between independent simulations in one process). */
void UbLinkHealthReset();

} // namespace ns3

#endif /* UB_LINK_HEALTH_H */
