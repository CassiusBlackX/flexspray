// SPDX-License-Identifier: GPL-2.0-only
#include "ub-lifecycle-trace.h"

#include "ns3/abort.h"

#include <array>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <utility>

namespace ns3
{

namespace
{

template <typename Enum>
struct EnumName
{
    Enum value;
    const char* name;
};

constexpr std::array kRecordTypes = {
    EnumName{UbLifecycleTraceRecordType::RESOURCE_STATE, "RESOURCE_STATE"},
    EnumName{UbLifecycleTraceRecordType::LEDGER_DELTA, "LEDGER_DELTA"},
    EnumName{UbLifecycleTraceRecordType::IDENTITY_LIFETIME, "IDENTITY_LIFETIME"},
    EnumName{UbLifecycleTraceRecordType::TERMINAL_OUTCOME, "TERMINAL_OUTCOME"},
    EnumName{UbLifecycleTraceRecordType::SUMMARY, "SUMMARY"},
};

constexpr std::array kResourceKinds = {
    EnumName{UbLifecycleResourceKind::RTP_ENDPOINT, "RTP_ENDPOINT"},
    EnumName{UbLifecycleResourceKind::WQE, "WQE"},
    EnumName{UbLifecycleResourceKind::TRANSACTION_OPERATION, "TRANSACTION_OPERATION"},
    EnumName{UbLifecycleResourceKind::TRANSACTION_SEGMENT, "TRANSACTION_SEGMENT"},
    EnumName{UbLifecycleResourceKind::JETTY, "JETTY"},
    EnumName{UbLifecycleResourceKind::RTP_TPG, "RTP_TPG"},
    EnumName{UbLifecycleResourceKind::RTP_CONNECTION, "RTP_CONNECTION"},
    EnumName{UbLifecycleResourceKind::CTP_SERVICE, "CTP_SERVICE"},
    EnumName{UbLifecycleResourceKind::LDST_INSTANCE, "LDST_INSTANCE"},
    EnumName{UbLifecycleResourceKind::LDST_SEGMENT, "LDST_SEGMENT"},
    EnumName{UbLifecycleResourceKind::DATALINK_PORT_VL, "DATALINK_PORT_VL"},
};

constexpr std::array kSubjectKinds = {
    EnumName{UbLifecycleTraceSubjectKind::NONE, "NONE"},
    EnumName{UbLifecycleTraceSubjectKind::RESOURCE, "RESOURCE"},
    EnumName{UbLifecycleTraceSubjectKind::PACKET, "PACKET"},
    EnumName{UbLifecycleTraceSubjectKind::LEASE, "LEASE"},
    EnumName{UbLifecycleTraceSubjectKind::WORK, "WORK"},
};

constexpr std::array kEvents = {
    EnumName{UbLifecycleTraceEvent::RESOURCE_OPEN, "RESOURCE_OPEN"},
    EnumName{UbLifecycleTraceEvent::RESOURCE_RETIRE, "RESOURCE_RETIRE"},
    EnumName{UbLifecycleTraceEvent::LEASE_ACQUIRE, "LEASE_ACQUIRE"},
    EnumName{UbLifecycleTraceEvent::LEASE_RELEASE, "LEASE_RELEASE"},
    EnumName{UbLifecycleTraceEvent::WORK_TERMINAL, "WORK_TERMINAL"},
    EnumName{UbLifecycleTraceEvent::PACKET_BIRTH, "PACKET_BIRTH"},
    EnumName{UbLifecycleTraceEvent::PACKET_TRANSFER, "PACKET_TRANSFER"},
    EnumName{UbLifecycleTraceEvent::PACKET_TERMINAL, "PACKET_TERMINAL"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_IN_FLIGHT_PACKETS, "SUMMARY_IN_FLIGHT_PACKETS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_REGISTERED_PACKETS, "SUMMARY_REGISTERED_PACKETS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_TRANSFER_OBSERVATIONS,
             "SUMMARY_TRANSFER_OBSERVATIONS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_ENDPOINT_CONSUMED, "SUMMARY_ENDPOINT_CONSUMED"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_ROUTE_NOT_FOUND_DROPS,
             "SUMMARY_ROUTE_NOT_FOUND_DROPS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_EGRESS_QUEUE_FULL_DROPS,
             "SUMMARY_EGRESS_QUEUE_FULL_DROPS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_INGRESS_QUEUE_FULL_DROPS,
             "SUMMARY_INGRESS_QUEUE_FULL_DROPS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_FAULT_DROPS, "SUMMARY_FAULT_DROPS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_CHANNEL_FAILURE_DROPS,
             "SUMMARY_CHANNEL_FAILURE_DROPS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_MALFORMED_PACKET_DROPS,
             "SUMMARY_MALFORMED_PACKET_DROPS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_ACTIVE_WORK_LEASES, "SUMMARY_ACTIVE_WORK_LEASES"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_ACTIVE_CHILD_LEASES, "SUMMARY_ACTIVE_CHILD_LEASES"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_ACTIVE_TRANSPORT_USE_LEASES,
             "SUMMARY_ACTIVE_TRANSPORT_USE_LEASES"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_ACTIVE_BINDING_LEASES,
             "SUMMARY_ACTIVE_BINDING_LEASES"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_ACTIVE_TRACKED_EVENT_LEASES,
             "SUMMARY_ACTIVE_TRACKED_EVENT_LEASES"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_ACTIVE_TRANSPORT_WORK_LEASES,
             "SUMMARY_ACTIVE_TRANSPORT_WORK_LEASES"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_SUCCESSFUL_TERMINALS,
             "SUMMARY_SUCCESSFUL_TERMINALS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_COMPLETED_TERMINALS,
             "SUMMARY_COMPLETED_TERMINALS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_FAILED_TERMINALS, "SUMMARY_FAILED_TERMINALS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_CANCELED_TERMINALS, "SUMMARY_CANCELED_TERMINALS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_FLUSHED_TERMINALS, "SUMMARY_FLUSHED_TERMINALS"},
    EnumName{UbLifecycleTraceEvent::SUMMARY_RETIRED, "SUMMARY_RETIRED"},
};

constexpr std::array kOutcomes = {
    EnumName{UbLifecycleTraceOutcome::NONE, "NONE"},
    EnumName{UbLifecycleTraceOutcome::COMPLETED, "COMPLETED"},
    EnumName{UbLifecycleTraceOutcome::FAILED, "FAILED"},
    EnumName{UbLifecycleTraceOutcome::CANCELED, "CANCELED"},
    EnumName{UbLifecycleTraceOutcome::FLUSHED, "FLUSHED"},
    EnumName{UbLifecycleTraceOutcome::ENDPOINT_CONSUMED, "ENDPOINT_CONSUMED"},
    EnumName{UbLifecycleTraceOutcome::ROUTE_NOT_FOUND, "ROUTE_NOT_FOUND"},
    EnumName{UbLifecycleTraceOutcome::EGRESS_QUEUE_FULL, "EGRESS_QUEUE_FULL"},
    EnumName{UbLifecycleTraceOutcome::INGRESS_QUEUE_FULL, "INGRESS_QUEUE_FULL"},
    EnumName{UbLifecycleTraceOutcome::FAULT_DROP, "FAULT_DROP"},
    EnumName{UbLifecycleTraceOutcome::CHANNEL_FAILURE, "CHANNEL_FAILURE"},
    EnumName{UbLifecycleTraceOutcome::MALFORMED_PACKET, "MALFORMED_PACKET"},
};

constexpr std::array kReasons = {
    EnumName{UbLifecycleTraceReason::NONE, "NONE"},
    EnumName{UbLifecycleTraceReason::WORK_LEASE, "WORK_LEASE"},
    EnumName{UbLifecycleTraceReason::CHILD_LEASE, "CHILD_LEASE"},
    EnumName{UbLifecycleTraceReason::TRANSPORT_USE_LEASE, "TRANSPORT_USE_LEASE"},
    EnumName{UbLifecycleTraceReason::BINDING_LEASE, "BINDING_LEASE"},
    EnumName{UbLifecycleTraceReason::TRACKED_EVENT_LEASE, "TRACKED_EVENT_LEASE"},
    EnumName{UbLifecycleTraceReason::TRANSPORT_WORK_LEASE, "TRANSPORT_WORK_LEASE"},
    EnumName{UbLifecycleTraceReason::EGRESS_QUEUE_ENQUEUE, "EGRESS_QUEUE_ENQUEUE"},
    EnumName{UbLifecycleTraceReason::EGRESS_QUEUE_DEQUEUE, "EGRESS_QUEUE_DEQUEUE"},
    EnumName{UbLifecycleTraceReason::LOCAL_LINK, "LOCAL_LINK"},
};

constexpr std::array kSites = {
    EnumName{UbLifecycleTraceSite::COORDINATOR_OPEN, "COORDINATOR_OPEN"},
    EnumName{UbLifecycleTraceSite::COORDINATOR_RETIRE, "COORDINATOR_RETIRE"},
    EnumName{UbLifecycleTraceSite::COORDINATOR_ACQUIRE_LEASE, "COORDINATOR_ACQUIRE_LEASE"},
    EnumName{UbLifecycleTraceSite::COORDINATOR_RELEASE_LEASE, "COORDINATOR_RELEASE_LEASE"},
    EnumName{UbLifecycleTraceSite::COORDINATOR_WORK_TERMINAL, "COORDINATOR_WORK_TERMINAL"},
    EnumName{UbLifecycleTraceSite::COORDINATOR_PACKET_BIRTH, "COORDINATOR_PACKET_BIRTH"},
    EnumName{UbLifecycleTraceSite::COORDINATOR_PACKET_TRANSFER, "COORDINATOR_PACKET_TRANSFER"},
    EnumName{UbLifecycleTraceSite::COORDINATOR_PACKET_TERMINAL, "COORDINATOR_PACKET_TERMINAL"},
    EnumName{UbLifecycleTraceSite::COORDINATOR_SUMMARY, "COORDINATOR_SUMMARY"},
};

std::mutex g_mutex;
UbLifecycleTraceLevel g_level = UbLifecycleTraceLevel::OFF;
uint64_t g_detailSampleStride = 1;
uint64_t g_detailSampleOffset = 0;
uint32_t g_rank = 0;
bool g_rankScoped = false;
std::string g_outputRoot;
UbLifecycleTrace::SinkResolver g_resolveSink;
UbLifecycleTrace::ResolvedSink g_metadataSink;
std::map<std::string, UbLifecycleTrace::ResolvedSink> g_recordSinks;
bool g_metadataEmitted = false;
std::set<std::string> g_initializedRecordFiles;

std::string
GetRecordFilename(uint32_t ownerNode)
{
    const std::filesystem::path runlog = std::filesystem::path(g_outputRoot) / "runlog";
    std::string leaf = "LifecycleTrace_node_" + std::to_string(ownerNode);
    if (g_rankScoped)
    {
        leaf += "_rank_" + std::to_string(g_rank);
    }
    return (runlog / (leaf + ".tr")).string();
}

template <typename Enum, std::size_t Size>
void
AppendRegistry(std::ostringstream& out,
               const char* name,
               const std::array<EnumName<Enum>, Size>& registry,
               bool trailingComma = true)
{
    out << "\"" << name << "\":{";
    for (std::size_t i = 0; i < registry.size(); ++i)
    {
        if (i != 0)
        {
            out << ',';
        }
        out << "\"" << static_cast<uint64_t>(registry[i].value) << "\":\""
            << registry[i].name << "\"";
    }
    out << '}';
    if (trailingComma)
    {
        out << ',';
    }
}

std::string
NormalizeLevel(std::string_view value)
{
    std::string normalized(value);
    for (char& c : normalized)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return normalized;
}

} // namespace

UbLifecycleTraceLevel
UbLifecycleTrace::ResolveLevel(std::string_view preset, std::string_view customLevel)
{
    const std::string custom = NormalizeLevel(customLevel);
    if (!custom.empty())
    {
        if (custom == "OFF")
        {
            return UbLifecycleTraceLevel::OFF;
        }
        if (custom == "SUMMARY")
        {
            return UbLifecycleTraceLevel::SUMMARY;
        }
        if (custom == "EVENT")
        {
            return UbLifecycleTraceLevel::EVENT;
        }
        if (custom == "DETAIL")
        {
            return UbLifecycleTraceLevel::DETAIL;
        }
        NS_ABORT_MSG("Unknown lifecycle trace level: " << customLevel);
    }

    const std::string normalizedPreset = NormalizeLevel(preset);
    if (normalizedPreset == "MINIMAL" || normalizedPreset == "BALANCED")
    {
        return UbLifecycleTraceLevel::SUMMARY;
    }
    if (normalizedPreset == "DETAILED")
    {
        return UbLifecycleTraceLevel::EVENT;
    }
    if (normalizedPreset == "CUSTOM")
    {
        NS_ABORT_MSG("custom trace preset requires UB_LIFECYCLE_TRACE_LEVEL");
    }
    NS_ABORT_MSG("Unknown trace preset: " << preset);
}

void
UbLifecycleTrace::Configure(const std::string& outputRoot,
                            UbLifecycleTraceLevel level,
                            uint64_t detailSampleStride,
                            uint64_t detailSampleOffset,
                            uint32_t rank,
                            bool rankScoped,
                            SinkResolver resolveSink)
{
    NS_ABORT_MSG_IF(detailSampleStride == 0, "Lifecycle detail sample stride must be non-zero");
    NS_ABORT_MSG_IF(detailSampleOffset >= detailSampleStride,
                    "Lifecycle detail sample offset must be smaller than the stride");
    NS_ABORT_MSG_IF(level != UbLifecycleTraceLevel::OFF && !resolveSink,
                    "Enabled lifecycle trace requires a sink resolver");

    std::lock_guard<std::mutex> guard(g_mutex);
    g_outputRoot = outputRoot;
    g_level = level;
    g_detailSampleStride = detailSampleStride;
    g_detailSampleOffset = detailSampleOffset;
    g_rank = rank;
    g_rankScoped = rankScoped;
    g_resolveSink = std::move(resolveSink);
    g_metadataSink = {};
    g_recordSinks.clear();
    g_metadataEmitted = false;
    g_initializedRecordFiles.clear();
}

void
UbLifecycleTrace::Reset()
{
    std::lock_guard<std::mutex> guard(g_mutex);
    g_level = UbLifecycleTraceLevel::OFF;
    g_detailSampleStride = 1;
    g_detailSampleOffset = 0;
    g_rank = 0;
    g_rankScoped = false;
    g_outputRoot.clear();
    g_resolveSink = {};
    g_metadataSink = {};
    g_recordSinks.clear();
    g_metadataEmitted = false;
    g_initializedRecordFiles.clear();
}

UbLifecycleTraceLevel
UbLifecycleTrace::GetLevel()
{
    return g_level;
}

bool
UbLifecycleTrace::IsEventEnabled()
{
    return g_level == UbLifecycleTraceLevel::EVENT || g_level == UbLifecycleTraceLevel::DETAIL;
}

bool
UbLifecycleTrace::IsDetailEnabled()
{
    return g_level == UbLifecycleTraceLevel::DETAIL;
}

bool
UbLifecycleTrace::SelectResource(const UbLifecycleResourceIdentity& identity)
{
    return g_detailSampleStride == 1 ||
           (identity.localId + identity.generation - 1) % g_detailSampleStride ==
               g_detailSampleOffset;
}

bool
UbLifecycleTrace::SelectPacket(const UbPacketAttemptIdentity& identity)
{
    return g_detailSampleStride == 1 ||
           (identity.attempt + identity.owner.generation - 1) % g_detailSampleStride ==
               g_detailSampleOffset;
}

void
UbLifecycleTrace::PrepareOwner(uint32_t ownerNode)
{
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_level == UbLifecycleTraceLevel::OFF)
    {
        return;
    }
    const std::string filename = GetRecordFilename(ownerNode);
    if (g_recordSinks.find(filename) == g_recordSinks.end())
    {
        g_recordSinks.emplace(filename, g_resolveSink(filename));
    }
}

std::string
UbLifecycleTrace::BuildMetadataJson(uint32_t rank)
{
    std::ostringstream out;
    out << "{\"schema\":\"ub.lifecycle.v1\",\"timeUnit\":\"ns\",\"rank\":" << rank
        << ',';
    AppendRegistry(out, "recordTypes", kRecordTypes);
    AppendRegistry(out, "resourceKinds", kResourceKinds);
    AppendRegistry(out, "subjectKinds", kSubjectKinds);
    AppendRegistry(out, "events", kEvents);
    AppendRegistry(out, "outcomes", kOutcomes);
    AppendRegistry(out, "reasons", kReasons);
    AppendRegistry(out, "sites", kSites, false);
    out << '}';
    return out.str();
}

void
UbLifecycleTrace::EmitMetadata()
{
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_level == UbLifecycleTraceLevel::OFF || g_metadataEmitted)
    {
        return;
    }
    const std::filesystem::path runlog = std::filesystem::path(g_outputRoot) / "runlog";
    const std::string filename =
        (runlog / (g_rankScoped ? "LifecycleTraceSchema_rank_" + std::to_string(g_rank) + ".json"
                               : "LifecycleTraceSchema.json"))
            .string();
    g_metadataSink = g_resolveSink(filename);
    NS_ABORT_MSG_IF(!g_metadataSink, "Lifecycle metadata sink resolver returned an empty sink");
    g_metadataSink(BuildMetadataJson(g_rank));
    g_metadataEmitted = true;
}

const char*
UbLifecycleTrace::GetRecordHeader()
{
    return "time,ownerNode,ownerSequence,recordType,resourceKind,localId,generation,"
           "subjectKind,subjectId,event,before,delta,after,outcome,reason,siteId";
}

std::string
UbLifecycleTrace::EncodeRecord(const UbLifecycleTraceRecord& record)
{
    std::ostringstream out;
    out << record.timeNs << ',' << record.ownerNode << ',' << record.ownerSequence << ','
        << static_cast<uint64_t>(record.recordType) << ','
        << static_cast<uint64_t>(record.resourceKind) << ',' << record.localId << ','
        << record.generation << ',' << static_cast<uint64_t>(record.subjectKind) << ','
        << record.subjectId << ',' << static_cast<uint64_t>(record.event) << ',' << record.before
        << ',' << record.delta << ',' << record.after << ','
        << static_cast<uint64_t>(record.outcome) << ',' << static_cast<uint64_t>(record.reason)
        << ',' << static_cast<uint64_t>(record.site);
    return out.str();
}

void
UbLifecycleTrace::Emit(const UbLifecycleTraceRecord& record)
{
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_level == UbLifecycleTraceLevel::OFF || !g_resolveSink)
    {
        return;
    }

    const std::string filename = GetRecordFilename(record.ownerNode);
    auto sink = g_recordSinks.find(filename);
    if (sink == g_recordSinks.end())
    {
        sink = g_recordSinks.emplace(filename, g_resolveSink(filename)).first;
    }
    NS_ABORT_MSG_IF(!sink->second, "Lifecycle record sink resolver returned an empty sink");
    if (g_initializedRecordFiles.insert(filename).second)
    {
        sink->second("# schema=ub.lifecycle.v1,timeUnit=ns");
        sink->second(GetRecordHeader());
    }
    sink->second(EncodeRecord(record));
}

} // namespace ns3
