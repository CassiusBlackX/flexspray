// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_LIFECYCLE_TRACE_H
#define UB_LIFECYCLE_TRACE_H

#include "ns3/ub-lifecycle.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace ns3
{

enum class UbLifecycleTraceLevel : uint8_t
{
    OFF = 0,
    SUMMARY = 1,
    EVENT = 2,
    DETAIL = 3,
};

enum class UbLifecycleTraceRecordType : uint8_t
{
    RESOURCE_STATE = 1,
    LEDGER_DELTA = 2,
    IDENTITY_LIFETIME = 3,
    TERMINAL_OUTCOME = 4,
    SUMMARY = 5,
};

enum class UbLifecycleTraceSubjectKind : uint8_t
{
    NONE = 0,
    RESOURCE = 1,
    PACKET = 2,
    LEASE = 3,
    WORK = 4,
};

enum class UbLifecycleTraceEvent : uint16_t
{
    RESOURCE_OPEN = 1,
    RESOURCE_RETIRE = 2,
    LEASE_ACQUIRE = 3,
    LEASE_RELEASE = 4,
    WORK_TERMINAL = 5,
    PACKET_BIRTH = 6,
    PACKET_TRANSFER = 7,
    PACKET_TERMINAL = 8,
    SUMMARY_IN_FLIGHT_PACKETS = 100,
    SUMMARY_REGISTERED_PACKETS = 101,
    SUMMARY_TRANSFER_OBSERVATIONS = 102,
    SUMMARY_ENDPOINT_CONSUMED = 103,
    SUMMARY_ROUTE_NOT_FOUND_DROPS = 104,
    SUMMARY_EGRESS_QUEUE_FULL_DROPS = 105,
    SUMMARY_INGRESS_QUEUE_FULL_DROPS = 106,
    SUMMARY_FAULT_DROPS = 107,
    SUMMARY_CHANNEL_FAILURE_DROPS = 108,
    SUMMARY_MALFORMED_PACKET_DROPS = 109,
    SUMMARY_ACTIVE_WORK_LEASES = 110,
    SUMMARY_ACTIVE_CHILD_LEASES = 111,
    SUMMARY_ACTIVE_TRANSPORT_USE_LEASES = 112,
    SUMMARY_ACTIVE_BINDING_LEASES = 113,
    SUMMARY_ACTIVE_TRACKED_EVENT_LEASES = 114,
    SUMMARY_ACTIVE_TRANSPORT_WORK_LEASES = 115,
    SUMMARY_SUCCESSFUL_TERMINALS = 116,
    SUMMARY_COMPLETED_TERMINALS = 117,
    SUMMARY_FAILED_TERMINALS = 118,
    SUMMARY_CANCELED_TERMINALS = 119,
    SUMMARY_FLUSHED_TERMINALS = 120,
    SUMMARY_RETIRED = 121,
};

enum class UbLifecycleTraceOutcome : uint16_t
{
    NONE = 0,
    COMPLETED = 1,
    FAILED = 2,
    CANCELED = 3,
    FLUSHED = 4,
    ENDPOINT_CONSUMED = 10,
    ROUTE_NOT_FOUND = 11,
    EGRESS_QUEUE_FULL = 12,
    INGRESS_QUEUE_FULL = 13,
    FAULT_DROP = 14,
    CHANNEL_FAILURE = 15,
    MALFORMED_PACKET = 16,
};

enum class UbLifecycleTraceReason : uint16_t
{
    NONE = 0,
    WORK_LEASE = 1,
    CHILD_LEASE = 2,
    TRANSPORT_USE_LEASE = 3,
    BINDING_LEASE = 4,
    TRACKED_EVENT_LEASE = 5,
    TRANSPORT_WORK_LEASE = 6,
    EGRESS_QUEUE_ENQUEUE = 10,
    EGRESS_QUEUE_DEQUEUE = 11,
    LOCAL_LINK = 12,
};

enum class UbLifecycleTraceSite : uint16_t
{
    COORDINATOR_OPEN = 1,
    COORDINATOR_RETIRE = 2,
    COORDINATOR_ACQUIRE_LEASE = 3,
    COORDINATOR_RELEASE_LEASE = 4,
    COORDINATOR_WORK_TERMINAL = 5,
    COORDINATOR_PACKET_BIRTH = 6,
    COORDINATOR_PACKET_TRANSFER = 7,
    COORDINATOR_PACKET_TERMINAL = 8,
    COORDINATOR_SUMMARY = 9,
};

struct UbLifecycleTraceRecord
{
    int64_t timeNs{0};
    uint32_t ownerNode{0};
    uint64_t ownerSequence{0};
    UbLifecycleTraceRecordType recordType{UbLifecycleTraceRecordType::RESOURCE_STATE};
    UbLifecycleResourceKind resourceKind{UbLifecycleResourceKind::RTP_ENDPOINT};
    uint64_t localId{0};
    uint64_t generation{0};
    UbLifecycleTraceSubjectKind subjectKind{UbLifecycleTraceSubjectKind::NONE};
    uint64_t subjectId{0};
    UbLifecycleTraceEvent event{UbLifecycleTraceEvent::RESOURCE_OPEN};
    int64_t before{0};
    int64_t delta{0};
    int64_t after{0};
    UbLifecycleTraceOutcome outcome{UbLifecycleTraceOutcome::NONE};
    UbLifecycleTraceReason reason{UbLifecycleTraceReason::NONE};
    UbLifecycleTraceSite site{UbLifecycleTraceSite::COORDINATOR_OPEN};
};

class UbLifecycleTrace
{
  public:
    using Append = std::function<void(const std::string&, const std::string&)>;

    static constexpr int64_t NOT_APPLICABLE = INT64_MIN;

    static UbLifecycleTraceLevel ResolveLevel(std::string_view preset,
                                              std::string_view customLevel);
    static void Configure(const std::string& outputRoot,
                          UbLifecycleTraceLevel level,
                          uint64_t detailSampleStride,
                          uint64_t detailSampleOffset,
                          uint32_t rank,
                          bool rankScoped,
                          Append append);
    static void Reset();

    static UbLifecycleTraceLevel GetLevel();
    static bool IsEventEnabled();
    static bool IsDetailEnabled();
    static bool SelectResource(const UbLifecycleResourceIdentity& identity);
    static bool SelectPacket(const UbPacketAttemptIdentity& identity);

    static void EmitMetadata();
    static void Emit(const UbLifecycleTraceRecord& record);
    static std::string BuildMetadataJson(uint32_t rank);
    static std::string EncodeRecord(const UbLifecycleTraceRecord& record);
    static const char* GetRecordHeader();
};

} // namespace ns3

#endif // UB_LIFECYCLE_TRACE_H
