// SPDX-License-Identifier: GPL-2.0-only
#include "ub-utils.h"

#include "ns3/ub-next-hop-index.h"
#include "ns3/ub-rule-routing.h"
#include "ub-ldst-instance.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#ifndef _WIN32
#include <sys/resource.h>
#endif
#ifdef NS3_MPI
#include "ub-remote-link.h"
#include "ns3/mpi-interface.h"
#endif

using namespace std;
using namespace ns3;

namespace
{
const char*
LifecycleResourceKindToString(UbLifecycleResourceKind kind)
{
    switch (kind)
    {
    case UbLifecycleResourceKind::RTP_ENDPOINT:
        return "RTP_ENDPOINT";
    case UbLifecycleResourceKind::WQE:
        return "WQE";
    case UbLifecycleResourceKind::TRANSACTION_OPERATION:
        return "TRANSACTION_OPERATION";
    case UbLifecycleResourceKind::TRANSACTION_SEGMENT:
        return "TRANSACTION_SEGMENT";
    case UbLifecycleResourceKind::JETTY:
        return "JETTY";
    case UbLifecycleResourceKind::RTP_TPG:
        return "RTP_TPG";
    case UbLifecycleResourceKind::RTP_CONNECTION:
        return "RTP_CONNECTION";
    case UbLifecycleResourceKind::CTP_SERVICE:
        return "CTP_SERVICE";
    case UbLifecycleResourceKind::LDST_INSTANCE:
        return "LDST_INSTANCE";
    case UbLifecycleResourceKind::LDST_SEGMENT:
        return "LDST_SEGMENT";
    }
    NS_ABORT_MSG("Unknown lifecycle resource kind");
}

void
AccumulateLifecycleKindStorage(UbLifecycleKindStorageSnapshot& aggregate,
                               const UbLifecycleKindStorageSnapshot& value)
{
    aggregate.resourceCount += value.resourceCount;
    aggregate.compactHistoryRunCount += value.compactHistoryRunCount;
    aggregate.activeStateCount += value.activeStateCount;
    aggregate.retainedSummaryCount += value.retainedSummaryCount;
    aggregate.activeLeaseCount += value.activeLeaseCount;
}

struct LegacyNetworkAttributeKey
{
    const char* legacy;
    const char* replacement;
    const char* note;
};

constexpr std::array<LegacyNetworkAttributeKey, 6> kLegacyNetworkAttributeKeys = {{
    {"ns3::UbQueueManager::ResumeOffset",
     "ns3::UbQueueManager::DynamicPfcResumeGapBytes",
     "DynamicPfcResumeGapBytes is the current dynamic-PFC XON/XOFF resume gap."},
    {"ns3::UbSwitch::EnableCBFC",
     "ns3::UbSwitch::FlowControl \"CBFC\"",
     "FlowControl now selects the CBFC/PFC policy directly."},
    {"ns3::UbSwitch::EnablePFC",
     "ns3::UbSwitch::FlowControl \"PFC_FIXED\" or \"PFC_DYNAMIC\"",
     "FlowControl now selects the CBFC/PFC policy directly."},
    {"ns3::UbApiThread::",
     "ns3::UbLdstThread::",
     "LD/ST thread attributes moved to the UbLdstThread TypeId."},
    {"ns3::UbJetty::UbInflightMax",
     "ns3::UbJetty::UbJettyInflightMax",
     "Jetty inflight control was renamed."},
    {"ns3::UbCtpTransportService::MaxOutstandingTransactions",
     "ns3::UbJetty::UbJettyInflightMax",
     "CTP no longer owns a separate send-admission window; outstanding back-pressure belongs to Jetty."},
}};

struct NetworkAttributeAlias
{
    const char* legacy;
    const char* replacement;
};

constexpr std::array<NetworkAttributeAlias, 2> kNetworkAttributeAliases = {{
    {"ns3::UbTransportChannel::InitialRTO", "ns3::UbTransportChannel::BaseRTO"},
    {"ns3::UbTransportChannel::EnableFastSelectiveRetrans",
     "ns3::UbTransportChannel::EnableFastRetrans"},
}};

struct LegacyRoutingAttributes
{
    const char* typeName;
    const char* packetSpray;
    const char* shortestPaths;
};

constexpr std::array<LegacyRoutingAttributes, 3> kLegacyRoutingAttributes = {{
    {"ns3::UbApp", "ns3::UbApp::UsePacketSpray", "ns3::UbApp::UseShortestPaths"},
    {"ns3::UbTransportChannel",
     "ns3::UbTransportChannel::UsePacketSpray",
     "ns3::UbTransportChannel::UseShortestPaths"},
    {"ns3::UbLdstApi",
     "ns3::UbLdstApi::UsePacketSpray",
     "ns3::UbLdstApi::UseShortestPaths"},
}};

struct ParsedDefaultAttribute
{
    std::string name;
    std::string value;
};

std::optional<ParsedDefaultAttribute>
ParseDefaultAttribute(const std::string& line)
{
    const auto firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#')
    {
        return std::nullopt;
    }

    std::istringstream tokens(line.substr(firstNonSpace));
    std::string directive;
    ParsedDefaultAttribute attribute;
    tokens >> directive >> attribute.name >> attribute.value;
    if (directive != "default" || attribute.name.empty() || attribute.value.empty())
    {
        return std::nullopt;
    }
    if (attribute.value.size() >= 2 && attribute.value.front() == '"' &&
        attribute.value.back() == '"')
    {
        attribute.value = attribute.value.substr(1, attribute.value.size() - 2);
    }
    return attribute;
}

bool
ParseLegacyRoutingBool(const std::string& value,
                       const std::string& filename,
                       uint32_t lineNumber)
{
    if (value == "true" || value == "1")
    {
        return true;
    }
    if (value == "false" || value == "0")
    {
        return false;
    }
    NS_FATAL_ERROR("Invalid legacy routing boolean at "
                   << filename << ":" << lineNumber << ": " << value
                   << ". Use true or false.");
    return false;
}

template <typename T>
T
GetConfiguredDefaultEnum(TypeId typeId, const char* attributeName)
{
    TypeId::AttributeInformation info;
    NS_ABORT_MSG_IF(!typeId.LookupAttributeByName(attributeName, &info),
                    "Missing routing attribute " << typeId.GetName() << "::" << attributeName);
    const auto* value = dynamic_cast<const EnumValue<T>*>(PeekPointer(info.initialValue));
    NS_ABORT_MSG_IF(value == nullptr,
                    "Routing attribute " << typeId.GetName() << "::" << attributeName
                                         << " is not an enum");
    return value->Get();
}

void
ValidateConfiguredRoutingDefaults(const std::string& filename)
{
    const MultipathSelector selector = GetConfiguredDefaultEnum<MultipathSelector>(
        UbRoutingProcess::GetTypeId(),
        "MultipathSelector");
    const std::array<std::pair<TypeId, const char*>, 3> routingOwners = {{
        {UbApp::GetTypeId(), "ns3::UbApp"},
        {UbTransportChannel::GetTypeId(), "ns3::UbTransportChannel"},
        {UbLdstApi::GetTypeId(), "ns3::UbLdstApi"},
    }};

    for (const auto& [typeId, typeName] : routingOwners)
    {
        const RoutingType routingType =
            GetConfiguredDefaultEnum<RoutingType>(typeId, "RoutingType");
        if (MultipathSelectorIsValidForRoutingType(selector, routingType))
        {
            continue;
        }

        const RoutingType compatibleRoutingType =
            MakeRoutingType(selector != MultipathSelector::INGRESS_PORT_STRIPE,
                            RoutingTypeUsesShortestPaths(routingType));
        NS_ABORT_MSG("Invalid routing configuration in "
                     << filename << ": default " << typeName << "::RoutingType \""
                     << RoutingTypeToString(routingType)
                     << "\" cannot be combined with default "
                        "ns3::UbRoutingProcess::MultipathSelector \""
                     << MultipathSelectorToString(selector)
                     << "\". Replace one line with either:\n"
                     << "  default " << typeName << "::RoutingType \""
                     << RoutingTypeToString(compatibleRoutingType) << "\"\n"
                     << "  default ns3::UbRoutingProcess::MultipathSelector \"HASH64\"");
    }
}

std::string
DisplayFilename(const std::string& filename)
{
    return std::filesystem::path(filename).filename().string();
}

uint64_t
MixLinkDelayOffsetHash(uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

uint32_t
ParseUint32Field(const std::string& line, size_t valueStart, size_t valueEnd)
{
    uint32_t value = 0;
    for (size_t pos = valueStart; pos < valueEnd; ++pos)
    {
        const char c = line[pos];
        NS_ABORT_MSG_IF(c < '0' || c > '9',
                        "Invalid uint32 field in traffic.csv; signed values are not supported");
        const uint32_t digit = static_cast<uint32_t>(c - '0');
        NS_ABORT_MSG_IF(value > (std::numeric_limits<uint32_t>::max() - digit) / 10,
                        "traffic.csv uint32 field overflow");
        value = value * 10 + digit;
    }
    return value;
}

uint32_t
ParseUint32Field(std::string_view field)
{
    NS_ABORT_MSG_IF(field.empty(), "Missing uint32 field in traffic.csv");
    uint32_t value = 0;
    for (char c : field)
    {
        NS_ABORT_MSG_IF(c < '0' || c > '9',
                        "Invalid uint32 field in traffic.csv; signed values are not supported");
        const uint32_t digit = static_cast<uint32_t>(c - '0');
        NS_ABORT_MSG_IF(value > (std::numeric_limits<uint32_t>::max() - digit) / 10,
                        "traffic.csv uint32 field overflow");
        value = value * 10 + digit;
    }
    return value;
}

uint8_t
ParseTrafficPriorityField(std::string_view field)
{
    const uint32_t priority = ParseUint32Field(field);
    NS_ABORT_MSG_IF(priority > UB_PRIORITY_MAX,
                    "Invalid priority field in traffic.csv; valid range is 0.."
                        << static_cast<uint32_t>(UB_PRIORITY_MAX));
    return static_cast<uint8_t>(priority);
}

std::string_view
TrimField(std::string_view field)
{
    while (!field.empty() &&
           (field.front() == ' ' || field.front() == '\t' || field.front() == '\r' ||
            field.front() == '\n'))
    {
        field.remove_prefix(1);
    }
    while (!field.empty() &&
           (field.back() == ' ' || field.back() == '\t' || field.back() == '\r' ||
            field.back() == '\n'))
    {
        field.remove_suffix(1);
    }
    return field;
}

std::string
TrimFieldCopy(std::string_view field)
{
    field = TrimField(field);
    return std::string(field.data(), field.size());
}

std::vector<std::string>
SplitCsvRow(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ','))
    {
        fields.push_back(TrimFieldCopy(field));
    }
    if (!line.empty() && line.back() == ',')
    {
        fields.emplace_back();
    }
    return fields;
}

void
ValidateTrafficCsvHeader(const std::vector<std::string>& header)
{
    NS_ABORT_MSG_IF(header.size() != 9 && header.size() != 11,
                    "traffic.csv header must have 9 base fields or 11 fields with "
                        "srcPortId,dstPortId");
    if (header.size() == 11)
    {
        NS_ABORT_MSG_IF(header[9] != "srcPortId" || header[10] != "dstPortId",
                        "traffic.csv 11-field header must end with srcPortId,dstPortId");
    }
}

void
ValidateTrafficCsvRowWidth(const std::string& line, std::size_t expectedFieldCount)
{
    const std::size_t actualFieldCount = SplitCsvRow(line).size();
    NS_ABORT_MSG_IF(actualFieldCount != expectedFieldCount,
                    "traffic.csv row has " << actualFieldCount << " fields, expected "
                                             << expectedFieldCount << " fields from the header");
}

std::map<std::string, std::size_t>
BuildCsvHeaderIndex(const std::vector<std::string>& header)
{
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < header.size(); ++i)
    {
        index.emplace(header[i], i);
    }
    return index;
}

std::string
GetRequiredCsvField(const std::vector<std::string>& fields,
                    const std::map<std::string, std::size_t>& headerIndex,
                    const std::string& column,
                    const std::string& filename)
{
    const auto it = headerIndex.find(column);
    NS_ABORT_MSG_IF(it == headerIndex.end(),
                    DisplayFilename(filename) << " missing required column: " << column);
    NS_ABORT_MSG_IF(it->second >= fields.size(),
                    DisplayFilename(filename) << " row missing required field: " << column);
    return fields[it->second];
}

std::string
GetOptionalCsvField(const std::vector<std::string>& fields,
                    const std::map<std::string, std::size_t>& headerIndex,
                    const std::string& column)
{
    const auto it = headerIndex.find(column);
    if (it == headerIndex.end() || it->second >= fields.size())
    {
        return "";
    }
    return fields[it->second];
}

bool
IsLegacyNodeForwardDelayHeader(const std::vector<std::string>& header)
{
    return header.size() == 4 && header[0] == "nodeId" && header[1] == "nodeType" &&
           header[2] == "portNum" && header[3] == "forwardDelay";
}

uint32_t
ReadStructuredUint32(std::istream& input, const char* field)
{
    uint64_t value = 0;
    NS_ABORT_MSG_IF(!(input >> value), "structured topology missing or invalid " << field);
    NS_ABORT_MSG_IF(value > std::numeric_limits<uint32_t>::max(),
                    "structured topology " << field << " exceeds uint32 range");
    return static_cast<uint32_t>(value);
}

std::string
ReadStructuredToken(std::istream& input, const char* field)
{
    std::string value;
    NS_ABORT_MSG_IF(!(input >> value), "structured topology missing " << field);
    return value;
}

void
ReadStructuredSection(std::istream& input, const char* expected, uint32_t& count)
{
    const std::string section = ReadStructuredToken(input, "section name");
    NS_ABORT_MSG_IF(section != expected,
                    "structured topology expected section " << expected << ", got " << section);
    count = ReadStructuredUint32(input, "section count");
}

std::vector<uint32_t>
ReadStructuredUint32List(std::istream& input, const char* field)
{
    const uint32_t count = ReadStructuredUint32(input, field);
    std::vector<uint32_t> values;
    values.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        values.push_back(ReadStructuredUint32(input, field));
    }
    return values;
}

std::vector<std::string>
ReadStructuredTokenList(std::istream& input, const char* field)
{
    const uint32_t count = ReadStructuredUint32(input, field);
    std::vector<std::string> values;
    values.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        values.push_back(ReadStructuredToken(input, field));
    }
    return values;
}

std::vector<uint16_t>
ReadStructuredPortList(std::istream& input, const char* field)
{
    const std::vector<uint32_t> values = ReadStructuredUint32List(input, field);
    std::vector<uint16_t> ports;
    ports.reserve(values.size());
    for (uint32_t value : values)
    {
        NS_ABORT_MSG_IF(value > std::numeric_limits<uint16_t>::max(),
                        "structured topology " << field << " contains an invalid port");
        ports.push_back(static_cast<uint16_t>(value));
    }
    return ports;
}

void
AppendDependencyPhases(std::string_view field, TrafficRecord& record)
{
    while (!field.empty())
    {
        while (!field.empty() &&
               (field.front() == ' ' || field.front() == '\t' || field.front() == '\r'))
        {
            field.remove_prefix(1);
        }
        if (field.empty())
        {
            break;
        }

        size_t tokenEnd = 0;
        while (tokenEnd < field.size() && field[tokenEnd] != ' ' && field[tokenEnd] != '\t' &&
               field[tokenEnd] != '\r')
        {
            ++tokenEnd;
        }

        record.dependOnPhases.push_back(ParseUint32Field(field.substr(0, tokenEnd)));
        field.remove_prefix(tokenEnd);
    }
}

void
SetTrafficRecordField(int fieldCount, std::string_view rawField, TrafficRecord& record)
{
    const std::string_view field = TrimField(rawField);
    switch (fieldCount)
    {
    case 0:
        record.taskId = static_cast<int>(ParseUint32Field(field));
        break;
    case 1:
        record.sourceNode = static_cast<int>(ParseUint32Field(field));
        break;
    case 2:
        record.destNode = static_cast<int>(ParseUint32Field(field));
        break;
    case 3:
        record.dataSize = static_cast<int>(ParseUint32Field(field));
        break;
    case 4:
        record.opType.assign(field.data(), field.size());
        break;
    case 5:
        record.priority = static_cast<int>(ParseTrafficPriorityField(field));
        break;
    case 6:
        record.delay.assign(field.data(), field.size());
        break;
    case 7:
        record.phaseId = static_cast<int>(ParseUint32Field(field));
        break;
    case 8:
        AppendDependencyPhases(field, record);
        break;
    case 9:
        record.hasSrcPortId = !field.empty();
        record.srcPortId = record.hasSrcPortId ? ParseUint32Field(field) : 0;
        break;
    case 10:
        record.hasDstPortId = !field.empty();
        record.dstPortId = record.hasDstPortId ? ParseUint32Field(field) : 0;
        break;
    }
}

std::pair<uint32_t, uint32_t>
ParseUint32RangeField(const std::string& field)
{
    const size_t dotPos = field.find("..");
    if (dotPos == std::string::npos)
    {
        const uint32_t value = static_cast<uint32_t>(std::stoul(field));
        return {value, value};
    }

    const uint32_t start = static_cast<uint32_t>(std::stoul(field.substr(0, dotPos)));
    const uint32_t end = static_cast<uint32_t>(std::stoul(field.substr(dotPos + 2)));
    NS_ABORT_MSG_IF(start > end, "invalid numeric range: " << field);
    return {start, end};
}

std::string
FormatUint32Range(uint32_t start, uint32_t end)
{
    if (start == end)
    {
        return std::to_string(start);
    }
    return std::to_string(start) + ".." + std::to_string(end);
}

bool
RangesOverlap(uint32_t leftStart, uint32_t leftEnd, uint32_t rightStart, uint32_t rightEnd)
{
    return leftStart <= rightEnd && rightStart <= leftEnd;
}

using RouteValueSignature = std::vector<std::pair<uint16_t, uint32_t>>;

RouteValueSignature
NormalizeRouteValue(const std::vector<uint16_t>& outports,
                    const std::vector<uint32_t>& metrics,
                    uint32_t rowNumber)
{
    NS_ABORT_MSG_IF(outports.size() != metrics.size(),
                    "routing_table.csv row " << rowNumber
                                             << " outports size not equal metrics size");
    NS_ABORT_MSG_IF(outports.empty(),
                    "routing_table.csv row " << rowNumber
                                             << " must contain at least one outPort/metric pair");

    std::map<uint16_t, uint32_t> metricByPort;
    for (auto index = 0u; index < outports.size(); ++index)
    {
        auto [it, inserted] = metricByPort.emplace(outports[index], metrics[index]);
        NS_ABORT_MSG_IF(!inserted && it->second != metrics[index],
                        "routing_table.csv row "
                            << rowNumber << " configures outPort " << outports[index]
                            << " with multiple metrics");
    }

    RouteValueSignature value;
    value.reserve(metricByPort.size());
    for (const auto& [outport, metric] : metricByPort)
    {
        value.emplace_back(outport, metric);
    }
    return value;
}

struct RouteRangeCsvRow
{
    uint32_t rowNumber;
    uint32_t nodeStart;
    uint32_t nodeEnd;
    uint32_t destStart;
    uint32_t destEnd;
    uint32_t destPort;
    RouteValueSignature value;
};

void
ValidateRouteRangeRowsNoConflicts(const std::vector<RouteRangeCsvRow>& rows)
{
    std::map<uint32_t, std::vector<const RouteRangeCsvRow*> > rowsByDestPort;
    for (const auto& row : rows)
    {
        rowsByDestPort[row.destPort].push_back(&row);
    }

    for (auto& [destPort, portRows] : rowsByDestPort)
    {
        std::sort(portRows.begin(),
                  portRows.end(),
                  [](const RouteRangeCsvRow* left, const RouteRangeCsvRow* right) {
                      if (left->nodeStart != right->nodeStart)
                      {
                          return left->nodeStart < right->nodeStart;
                      }
                      if (left->nodeEnd != right->nodeEnd)
                      {
                          return left->nodeEnd < right->nodeEnd;
                      }
                      return left->destStart < right->destStart;
                  });

        std::vector<const RouteRangeCsvRow*> activeRows;
        for (const auto* row : portRows)
        {
            activeRows.erase(std::remove_if(activeRows.begin(),
                                            activeRows.end(),
                                            [row](const RouteRangeCsvRow* active) {
                                                return active->nodeEnd < row->nodeStart;
                                            }),
                             activeRows.end());

            for (const auto* active : activeRows)
            {
                if (active->value == row->value)
                {
                    continue;
                }
                if (!RangesOverlap(active->destStart, active->destEnd, row->destStart, row->destEnd))
                {
                    continue;
                }

                const uint32_t nodeOverlapStart = std::max(active->nodeStart, row->nodeStart);
                const uint32_t nodeOverlapEnd = std::min(active->nodeEnd, row->nodeEnd);
                const uint32_t destOverlapStart = std::max(active->destStart, row->destStart);
                const uint32_t destOverlapEnd = std::min(active->destEnd, row->destEnd);
                NS_ABORT_MSG("routing_table.csv compressed route conflict between rows "
                             << active->rowNumber << " and " << row->rowNumber
                             << " at nodeId="
                             << FormatUint32Range(nodeOverlapStart, nodeOverlapEnd)
                             << ", dstNodeId="
                             << FormatUint32Range(destOverlapStart, destOverlapEnd)
                             << ", dstPortId=" << destPort);
            }
            activeRows.push_back(row);
        }
    }
}

bool
RewriteDefaultAttributeLine(std::string& line, const NetworkAttributeAlias& alias)
{
    const auto firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#')
    {
        return false;
    }

    std::istringstream tokens(line.substr(firstNonSpace));
    std::string directive;
    std::string attribute;
    tokens >> directive >> attribute;
    if (directive != "default" || attribute != alias.legacy)
    {
        return false;
    }

    const auto attributePos = line.find(attribute, firstNonSpace + directive.size());
    NS_ASSERT_MSG(attributePos != std::string::npos, "Parsed network attribute missing from line");
    line.replace(attributePos, attribute.size(), alias.replacement);
    return true;
}

std::string
CreateConfigStoreInput(const std::string& filename, uint32_t& rewrittenLines)
{
    std::ifstream input(filename.c_str());
    NS_ASSERT_MSG(input.good(), "Can not open File: " << filename);

    rewrittenLines = 0;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
    {
        lines.push_back(line);
    }

    struct LegacyValue
    {
        std::string value;
        uint32_t lineNumber;
        std::vector<uint32_t> sourceLines;
    };
    struct RoutingState
    {
        std::optional<LegacyValue> packetSpray;
        std::optional<LegacyValue> shortestPaths;
        std::optional<uint32_t> canonicalLine;
    };

    std::array<RoutingState, kLegacyRoutingAttributes.size()> routingStates;
    std::optional<LegacyValue> legacySelector;
    std::optional<uint32_t> canonicalSelectorLine;
    auto recordLegacyValue = [](std::optional<LegacyValue>& target,
                                const std::string& value,
                                uint32_t lineNumber) {
        if (!target.has_value())
        {
            target = LegacyValue{value, lineNumber, {lineNumber}};
            return;
        }
        target->value = value;
        target->lineNumber = lineNumber;
        target->sourceLines.push_back(lineNumber);
    };

    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const auto attribute = ParseDefaultAttribute(lines[index]);
        if (!attribute.has_value())
        {
            continue;
        }
        const uint32_t lineNumber = static_cast<uint32_t>(index + 1);
        for (std::size_t owner = 0; owner < kLegacyRoutingAttributes.size(); ++owner)
        {
            const auto& legacy = kLegacyRoutingAttributes[owner];
            if (attribute->name == legacy.packetSpray)
            {
                recordLegacyValue(routingStates[owner].packetSpray,
                                  attribute->value,
                                  lineNumber);
            }
            else if (attribute->name == legacy.shortestPaths)
            {
                recordLegacyValue(routingStates[owner].shortestPaths,
                                  attribute->value,
                                  lineNumber);
            }
            else if (attribute->name == std::string(legacy.typeName) + "::RoutingType")
            {
                routingStates[owner].canonicalLine = lineNumber;
            }
        }
        if (attribute->name == "ns3::UbRoutingProcess::RoutingAlgorithm")
        {
            recordLegacyValue(legacySelector, attribute->value, lineNumber);
        }
        else if (attribute->name == "ns3::UbRoutingProcess::MultipathSelector")
        {
            canonicalSelectorLine = lineNumber;
        }
    }

    std::map<uint32_t, std::string> replacements;
    std::set<uint32_t> removedLines;
    std::vector<std::string> warningEntries;
    for (std::size_t owner = 0; owner < kLegacyRoutingAttributes.size(); ++owner)
    {
        const auto& legacy = kLegacyRoutingAttributes[owner];
        const auto& state = routingStates[owner];
        if (!state.packetSpray.has_value() && !state.shortestPaths.has_value())
        {
            continue;
        }
        NS_ABORT_MSG_IF(state.canonicalLine.has_value(),
                        "Conflicting routing configuration in "
                            << filename << ": legacy " << legacy.packetSpray << "/"
                            << legacy.shortestPaths << " cannot be combined with "
                            << legacy.typeName << "::RoutingType at line "
                            << *state.canonicalLine << ". Remove the legacy lines.");

        const bool packetSpray =
            state.packetSpray.has_value()
                ? ParseLegacyRoutingBool(state.packetSpray->value,
                                         filename,
                                         state.packetSpray->lineNumber)
                : false;
        const bool shortestPaths =
            state.shortestPaths.has_value()
                ? ParseLegacyRoutingBool(state.shortestPaths->value,
                                         filename,
                                         state.shortestPaths->lineNumber)
                : true;
        const RoutingType routingType = MakeRoutingType(packetSpray, shortestPaths);
        const std::string replacement = std::string("default ") + legacy.typeName +
                                        "::RoutingType \"" + RoutingTypeToString(routingType) + "\"";

        std::vector<uint32_t> sourceLines;
        if (state.packetSpray.has_value())
        {
            sourceLines.insert(sourceLines.end(),
                               state.packetSpray->sourceLines.begin(),
                               state.packetSpray->sourceLines.end());
            removedLines.insert(state.packetSpray->sourceLines.begin(),
                                state.packetSpray->sourceLines.end());
        }
        if (state.shortestPaths.has_value())
        {
            sourceLines.insert(sourceLines.end(),
                               state.shortestPaths->sourceLines.begin(),
                               state.shortestPaths->sourceLines.end());
            removedLines.insert(state.shortestPaths->sourceLines.begin(),
                                state.shortestPaths->sourceLines.end());
        }
        std::sort(sourceLines.begin(), sourceLines.end());
        replacements[sourceLines.front()] = replacement;

        std::ostringstream entry;
        entry << filename << ":";
        for (std::size_t index = 0; index < sourceLines.size(); ++index)
        {
            if (index > 0)
            {
                entry << ",";
            }
            entry << sourceLines[index];
        }
        entry << " -> " << replacement;
        warningEntries.push_back(entry.str());
    }

    if (legacySelector.has_value())
    {
        NS_ABORT_MSG_IF(canonicalSelectorLine.has_value(),
                        "Conflicting routing configuration in "
                            << filename
                            << ": legacy ns3::UbRoutingProcess::RoutingAlgorithm at line "
                            << legacySelector->lineNumber
                            << " cannot be combined with "
                               "ns3::UbRoutingProcess::MultipathSelector at line "
                            << *canonicalSelectorLine << ". Remove the legacy line.");
        std::string selector;
        if (legacySelector->value == "HASH")
        {
            selector = "HASH64";
        }
        else if (legacySelector->value == "ADAPTIVE")
        {
            selector = "ADAPTIVE";
        }
        else
        {
            NS_FATAL_ERROR("Unsupported legacy routing algorithm at "
                           << filename << ":" << legacySelector->lineNumber << ": "
                           << legacySelector->value << ". Use MultipathSelector instead.");
        }
        const std::string replacement =
            "default ns3::UbRoutingProcess::MultipathSelector \"" + selector + "\"";
        replacements[legacySelector->sourceLines.front()] = replacement;
        removedLines.insert(legacySelector->sourceLines.begin(),
                            legacySelector->sourceLines.end());
        std::ostringstream entry;
        entry << filename << ":";
        for (std::size_t index = 0; index < legacySelector->sourceLines.size(); ++index)
        {
            if (index > 0)
            {
                entry << ",";
            }
            entry << legacySelector->sourceLines[index];
        }
        entry << " -> " << replacement;
        warningEntries.push_back(entry.str());
    }

    std::ostringstream rewritten;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const uint32_t lineNumber = static_cast<uint32_t>(index + 1);
        auto replacement = replacements.find(lineNumber);
        if (replacement != replacements.end())
        {
            rewritten << replacement->second << '\n';
            ++rewrittenLines;
            continue;
        }
        if (removedLines.find(lineNumber) != removedLines.end())
        {
            ++rewrittenLines;
            continue;
        }
        line = lines[index];
        for (const auto& alias : kNetworkAttributeAliases)
        {
            if (RewriteDefaultAttributeLine(line, alias))
            {
                ++rewrittenLines;
                break;
            }
        }
        rewritten << line << '\n';
    }

    if (!warningEntries.empty())
    {
        std::cerr << "WARNING: Legacy routing attributes are deprecated and may stop working in a "
                     "future release. Replace them with:\n";
        for (const auto& entry : warningEntries)
        {
            std::cerr << "  " << entry << '\n';
        }
    }

    if (rewrittenLines == 0)
    {
        return filename;
    }

    const auto uniqueSuffix =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path rewrittenPath =
        std::filesystem::temp_directory_path() /
        ("ub-network-attribute-rewritten-" + uniqueSuffix + ".txt");

    std::ofstream output(rewrittenPath.c_str(), std::ios::trunc);
    NS_ASSERT_MSG(output.good(), "Can not create rewritten config file: " << rewrittenPath);
    output << rewritten.str();
    return rewrittenPath.string();
}

void
SetTrafficRecordViewField(int fieldCount, std::string_view rawField, TrafficRecordView& record)
{
    const std::string_view field = TrimField(rawField);
    switch (fieldCount)
    {
    case 0:
        record.taskId = ParseUint32Field(field);
        break;
    case 1:
        record.sourceNode = ParseUint32Field(field);
        break;
    case 2:
        record.destNode = ParseUint32Field(field);
        break;
    case 3:
        record.dataSize = ParseUint32Field(field);
        break;
    case 4:
        record.opType = field;
        break;
    case 5:
        record.priority = ParseTrafficPriorityField(field);
        break;
    case 6:
        record.delay = field;
        break;
    case 7:
        record.phaseId = ParseUint32Field(field);
        break;
    case 8:
        record.dependOnPhases = field;
        break;
    case 9:
        record.hasSrcPortId = !field.empty();
        record.srcPortId = record.hasSrcPortId ? ParseUint32Field(field) : 0;
        break;
    case 10:
        record.hasDstPortId = !field.empty();
        record.dstPortId = record.hasDstPortId ? ParseUint32Field(field) : 0;
        break;
    }
}

void
ValidateLegacyNetworkAttributeKeys(const std::string& filename)
{
    std::ifstream file(filename.c_str());
    NS_ASSERT_MSG(file.good(), "Can not open File: " << filename);

    std::string line;
    uint32_t lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        const auto firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#')
        {
            continue;
        }

        for (const auto& key : kLegacyNetworkAttributeKeys)
        {
            if (line.find(key.legacy) == std::string::npos)
            {
                continue;
            }

            NS_FATAL_ERROR("Legacy network_attribute.txt key: "
                           << key.legacy << " at " << filename << ":" << lineNumber
                           << ". Use " << key.replacement << " instead. " << key.note);
        }
    }
}

bool
IsNodeOwnedByCurrentRank(Ptr<Node> node)
{
#ifdef NS3_MPI
    if (!MpiInterface::IsEnabled() || MpiInterface::GetSize() <= 1)
    {
        return true;
    }
    return utils::UbUtils::IsSystemOwnedByRank(node->GetSystemId(), MpiInterface::GetSystemId());
#else
    return true;
#endif
}

void
PreloadLocalTpIfOwned(Ptr<Node> node,
                      uint32_t src,
                      uint32_t dest,
                      uint8_t sport,
                      uint8_t dport,
                      UbPriority priority,
                      uint32_t srcTpn,
                      uint32_t dstTpn)
{
    if (!IsNodeOwnedByCurrentRank(node))
    {
        return;
    }

    Ptr<UbController> ctrl = node->GetObject<UbController>();
    NS_ASSERT_MSG(ctrl != nullptr, "Preloaded TP endpoint must have UbController");
    if (ctrl->GetTransportChannelManager()->Contains(srcTpn))
    {
        return;
    }

    auto congestionCtrl = UbCongestionControl::Create(UB_DEVICE);
    ctrl->GetTransportChannelManager()->CreateTp(src, dest, sport, dport, priority, srcTpn, dstTpn, congestionCtrl);
}

Ptr<UbLink>
CreateUbChannelBetween(Ptr<UbPort> p1, Ptr<UbPort> p2, Time delay)
{
    Ptr<UbLink> channel;
#ifdef NS3_MPI
    if (!utils::UbUtils::IsSameMpiRank(p1->GetNode()->GetSystemId(),
                                       p2->GetNode()->GetSystemId()))
    {
        channel = CreateObject<UbRemoteLink>();
        p1->EnableMpiReceive();
        p2->EnableMpiReceive();
    }
    else
#endif
    {
        channel = CreateObject<UbLink>();
    }

    channel->SetAttribute("Delay", TimeValue(delay));
    p1->Attach(channel);
    p2->Attach(channel);
    return channel;
}

} // namespace

namespace utils {

UbUtils::TraceFileState&
UbUtils::GetTraceFile(const std::string& fileName)
{
    ConfigureTraceWriter();
    std::lock_guard<std::mutex> filesGuard(files_mutex);
    auto [it, inserted] = files.try_emplace(fileName);
    if (inserted)
    {
        it->second.fileName = fileName;
    }
    return it->second;
}

UbUtils::TraceSinkHandle
UbUtils::ResolveTraceSink(const std::string& fileName)
{
    while (true)
    {
        const uint64_t epoch = trace_writer_epoch.load(std::memory_order_acquire);
        TraceFileState* state = &GetTraceFile(fileName);
        if (epoch == trace_writer_epoch.load(std::memory_order_acquire))
        {
            TraceSinkHandle handle;
            handle.state = state;
            handle.epoch = epoch;
            return handle;
        }
    }
}

UbUtils::TraceSinkHandle
UbUtils::GetCachedTraceSink(const std::string& fileName)
{
    struct SinkCache
    {
        uint64_t epoch{0};
        std::unordered_map<std::string, TraceSinkHandle> sinks;
    };

    thread_local SinkCache cache;

    const uint64_t epoch = trace_writer_epoch.load(std::memory_order_acquire);
    if (cache.epoch != epoch)
    {
        cache.epoch = epoch;
        cache.sinks.clear();
    }
    auto it = cache.sinks.find(fileName);
    if (it == cache.sinks.end())
    {
        it = cache.sinks.emplace(fileName, ResolveTraceSink(fileName)).first;
    }
    return it->second;
}

void
UbUtils::ConfigureTraceWriter()
{
    std::lock_guard<std::mutex> guard(files_mutex);
    if (trace_writer_configured)
    {
        return;
    }

    UintegerValue chunkBytes;
    UintegerValue highWaterBytes;
    UintegerValue lowWaterBytes;
    UintegerValue maxOpenFiles;
    NS_ABORT_MSG_IF(!GlobalValue::GetValueByNameFailSafe("UB_TRACE_CHUNK_BYTES", chunkBytes),
                    "Missing UB_TRACE_CHUNK_BYTES");
    NS_ABORT_MSG_IF(
        !GlobalValue::GetValueByNameFailSafe("UB_TRACE_OWNER_HIGH_WATER_BYTES", highWaterBytes),
        "Missing UB_TRACE_OWNER_HIGH_WATER_BYTES");
    NS_ABORT_MSG_IF(
        !GlobalValue::GetValueByNameFailSafe("UB_TRACE_OWNER_LOW_WATER_BYTES", lowWaterBytes),
        "Missing UB_TRACE_OWNER_LOW_WATER_BYTES");
    NS_ABORT_MSG_IF(!GlobalValue::GetValueByNameFailSafe("UB_TRACE_MAX_OPEN_FILES", maxOpenFiles),
                    "Missing UB_TRACE_MAX_OPEN_FILES");

    trace_writer_config.chunkBytes = static_cast<std::size_t>(chunkBytes.Get());
    trace_writer_config.highWaterBytes = static_cast<std::size_t>(highWaterBytes.Get());
    trace_writer_config.lowWaterBytes = static_cast<std::size_t>(lowWaterBytes.Get());
    trace_writer_config.maxOpenFiles = static_cast<std::size_t>(maxOpenFiles.Get());
    NS_ABORT_MSG_IF(trace_writer_config.lowWaterBytes > trace_writer_config.highWaterBytes,
                    "Trace owner low-water mark exceeds high-water mark");
    NS_ABORT_MSG_IF(trace_writer_config.chunkBytes > trace_writer_config.highWaterBytes,
                    "Trace chunk size exceeds owner high-water mark");

    std::size_t platformLimit = 256;
#ifndef _WIN32
    constexpr std::size_t kReservedDescriptors = 128;
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0)
    {
        if (limit.rlim_cur == RLIM_INFINITY)
        {
            platformLimit = trace_writer_config.maxOpenFiles;
        }
        else if (limit.rlim_cur > kReservedDescriptors)
        {
            platformLimit = static_cast<std::size_t>(limit.rlim_cur) - kReservedDescriptors;
        }
        else
        {
            platformLimit = 1;
        }
    }
#endif
    trace_writer_config.effectiveMaxOpenFiles =
        std::max<std::size_t>(1, std::min(trace_writer_config.maxOpenFiles, platformLimit));
    trace_writer_configured = true;
}

UbUtils::TraceOwnerState&
UbUtils::GetTraceOwner(uint32_t ownerId)
{
    struct OwnerCache
    {
        uint64_t epoch{0};
        uint32_t ownerId{0};
        TraceOwnerState* owner{nullptr};
    };

    thread_local OwnerCache cache;

    const uint64_t epoch = trace_writer_epoch.load(std::memory_order_acquire);
    if (cache.epoch == epoch && cache.owner != nullptr && cache.ownerId == ownerId)
    {
        return *cache.owner;
    }
    std::lock_guard<std::mutex> guard(files_mutex);
    auto [it, inserted] = trace_owners.try_emplace(ownerId);
    (void)inserted;
    cache = {epoch, ownerId, &it->second};
    return *cache.owner;
}

UbTraceWriterStorageSnapshot
UbUtils::GetTraceWriterStorageSnapshot()
{
    UbTraceWriterStorageSnapshot snapshot;
    std::vector<TraceOwnerState*> owners;
    {
        std::lock_guard<std::mutex> filesGuard(files_mutex);
        snapshot.ownerCount = trace_owners.size();
        snapshot.fileCount = files.size();
        snapshot.openFileCount = trace_open_files;
        snapshot.openCount = trace_writer_stats.openCount;
        snapshot.writeCount = trace_writer_stats.writeCount;
        snapshot.flushCount = trace_writer_stats.flushCount;
        snapshot.closeCount = trace_writer_stats.closeCount;
        snapshot.lruCloseCount = trace_writer_stats.lruCloseCount;
        snapshot.pressureFlushCount = trace_writer_stats.pressureFlushCount;
        snapshot.peakOpenFiles = trace_writer_stats.peakOpenFiles;
        snapshot.peakBufferedBytes = trace_writer_stats.peakBufferedBytes;
        owners.reserve(trace_owners.size());
        for (auto& [ownerId, owner] : trace_owners)
        {
            (void)ownerId;
            owners.push_back(&owner);
        }
    }
    for (TraceOwnerState* owner : owners)
    {
        std::lock_guard<std::mutex> ownerGuard(owner->mutex);
        snapshot.currentBufferedBytes += owner->bufferedBytes;
        snapshot.maxOwnerBufferedBytes =
            std::max<uint64_t>(snapshot.maxOwnerBufferedBytes, owner->bufferedBytes);
    }
    return snapshot;
}

UbRuntimeStorageSnapshot
UbUtils::CaptureRuntimeStorageSnapshot()
{
    UbRuntimeStorageSnapshot snapshot;
    snapshot.nodeCount = NodeList::GetNNodes();
    for (auto node = NodeList::Begin(); node != NodeList::End(); ++node)
    {
        Ptr<UbController> controller = (*node)->GetObject<UbController>();
        if (controller != nullptr)
        {
            Ptr<TpConnectionManager> connections = controller->GetTpConnManager();
            if (connections != nullptr)
            {
                const ConnectionStorageSnapshot connectionStorage =
                    connections->GetConnectionStorageSnapshot();
                snapshot.canonicalConnections += connectionStorage.canonicalRecords;
                snapshot.authoritativeLocalConnectionViews +=
                    connectionStorage.authoritativeLocalViews;
                snapshot.secondaryConnectionIndexEntries += connectionStorage.secondaryIndexEntries;
                snapshot.reservedEndpointSlots += connectionStorage.unmaterializedEndpointSlots;
            }
            Ptr<UbTransportChannelManager> endpoints = controller->GetTransportChannelManager();
            if (endpoints != nullptr)
            {
                snapshot.residentTpEndpoints += endpoints->GetCount();
            }
            Ptr<UbFunction> function = controller->GetUbFunction();
            if (function != nullptr)
            {
                snapshot.residentWqes += function->GetResidentWqeCount();
                const UbJettyStorageSnapshot jettyStorage = function->GetJettyStorageSnapshot();
                snapshot.taskScopedJetties += jettyStorage.taskScopedJetties;
                snapshot.reusableJetties += jettyStorage.reusableJetties;
            }
            Ptr<UbTransaction> transaction = controller->GetUbTransaction();
            if (transaction != nullptr)
            {
                snapshot.residentOperations += transaction->GetResidentOperationCount();
                snapshot.residentSegments += transaction->GetResidentSegmentCount();
            }
        }

        Ptr<UbLdstInstance> ldst = (*node)->GetObject<UbLdstInstance>();
        if (ldst != nullptr)
        {
            snapshot.activeLdstTasks += ldst->GetActiveTaskCount();
            snapshot.activeLdstSegments += ldst->GetActiveSegmentCount();
        }

        Ptr<UbLifecycleCoordinator> coordinator = (*node)->GetObject<UbLifecycleCoordinator>();
        if (coordinator != nullptr)
        {
            const UbLifecycleStorageSnapshot nodeStorage = coordinator->GetStorageSnapshot();
            AccumulateLifecycleKindStorage(snapshot.lifecycle, nodeStorage);
            for (const auto& [kind, kindStorage] : nodeStorage.byKind)
            {
                AccumulateLifecycleKindStorage(snapshot.lifecycle.byKind[kind], kindStorage);
            }
        }
    }

    UbTrafficGen* traffic = UbTrafficGen::Get();
    snapshot.storedTrafficTasks = traffic->GetStoredTaskCount();
    snapshot.completedTrafficTasks = traffic->GetCompletedTaskCount();
    snapshot.trafficDependencyReferences = traffic->GetDependencyReferenceCount();
    snapshot.traceWriter = GetTraceWriterStorageSnapshot();
    snapshot.simulatorEventCount = Simulator::GetEventCount();
    return snapshot;
}

void
UbUtils::WriteRuntimeStorageSnapshot(const std::string& outputPath)
{
    namespace fs = std::filesystem;
    const fs::path path(outputPath);
    NS_ABORT_MSG_IF(path.empty(), "Runtime storage snapshot path must not be empty");
    std::error_code ec;
    if (!path.parent_path().empty())
    {
        fs::create_directories(path.parent_path(), ec);
        NS_ABORT_MSG_IF(ec,
                        "Failed to create runtime storage snapshot directory: "
                            << path.parent_path() << " err=" << ec.message());
    }
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    NS_ABORT_MSG_IF(!output.is_open(), "Cannot open runtime storage snapshot: " << outputPath);

    const UbRuntimeStorageSnapshot snapshot = CaptureRuntimeStorageSnapshot();
    const auto write = [&](std::string_view category,
                           std::string_view metric,
                           std::string_view resourceKind,
                           uint64_t value) {
        output << UbRuntimeStorageSnapshot::SCHEMA_VERSION << ',' << category << ',' << metric
               << ',' << resourceKind << ',' << value << '\n';
    };
    output << "schema_version,category,metric,resource_kind,value\n";
    write("runtime", "node_count", "", snapshot.nodeCount);
    write("runtime", "canonical_connections", "", snapshot.canonicalConnections);
    write("runtime",
          "authoritative_local_connection_views",
          "",
          snapshot.authoritativeLocalConnectionViews);
    write("runtime",
          "secondary_connection_index_entries",
          "",
          snapshot.secondaryConnectionIndexEntries);
    write("runtime", "resident_tp_endpoints", "", snapshot.residentTpEndpoints);
    write("runtime", "reserved_endpoint_slots", "", snapshot.reservedEndpointSlots);
    write("runtime", "task_scoped_jetties", "", snapshot.taskScopedJetties);
    write("runtime", "reusable_jetties", "", snapshot.reusableJetties);
    write("runtime", "simulator_event_count", "", snapshot.simulatorEventCount);
    write("runtime", "resident_wqes", "", snapshot.residentWqes);
    write("runtime", "resident_operations", "", snapshot.residentOperations);
    write("runtime", "resident_segments", "", snapshot.residentSegments);
    write("runtime", "active_ldst_tasks", "", snapshot.activeLdstTasks);
    write("runtime", "active_ldst_segments", "", snapshot.activeLdstSegments);
    write("traffic", "stored_tasks", "", snapshot.storedTrafficTasks);
    write("traffic", "completed_tasks", "", snapshot.completedTrafficTasks);
    write("traffic", "dependency_references", "", snapshot.trafficDependencyReferences);

    const auto writeLifecycle = [&](std::string_view kind,
                                    const UbLifecycleKindStorageSnapshot& storage) {
        write("lifecycle", "resource_count", kind, storage.resourceCount);
        write("lifecycle", "compact_history_run_count", kind, storage.compactHistoryRunCount);
        write("lifecycle", "active_state_count", kind, storage.activeStateCount);
        write("lifecycle", "retained_summary_count", kind, storage.retainedSummaryCount);
        write("lifecycle", "active_lease_count", kind, storage.activeLeaseCount);
    };
    writeLifecycle("", snapshot.lifecycle);
    for (const auto& [kind, storage] : snapshot.lifecycle.byKind)
    {
        writeLifecycle(LifecycleResourceKindToString(kind), storage);
    }

    write("trace_writer", "owner_count", "", snapshot.traceWriter.ownerCount);
    write("trace_writer", "file_count", "", snapshot.traceWriter.fileCount);
    write("trace_writer", "open_file_count", "", snapshot.traceWriter.openFileCount);
    write("trace_writer", "current_buffered_bytes", "", snapshot.traceWriter.currentBufferedBytes);
    write("trace_writer", "max_owner_buffered_bytes", "", snapshot.traceWriter.maxOwnerBufferedBytes);
    write("trace_writer", "open_count", "", snapshot.traceWriter.openCount);
    write("trace_writer", "write_count", "", snapshot.traceWriter.writeCount);
    write("trace_writer", "flush_count", "", snapshot.traceWriter.flushCount);
    write("trace_writer", "close_count", "", snapshot.traceWriter.closeCount);
    write("trace_writer", "lru_close_count", "", snapshot.traceWriter.lruCloseCount);
    write("trace_writer", "pressure_flush_count", "", snapshot.traceWriter.pressureFlushCount);
    write("trace_writer", "peak_open_files", "", snapshot.traceWriter.peakOpenFiles);
    write("trace_writer", "peak_buffered_bytes", "", snapshot.traceWriter.peakBufferedBytes);
    output.flush();
    NS_ABORT_MSG_IF(output.fail(), "Failed to write runtime storage snapshot: " << outputPath);
}

void
UbUtils::CloseTraceFile(TraceFileState& fileState, bool lruClose)
{
    if (!fileState.open)
    {
        return;
    }
    NS_ABORT_MSG_IF(fileState.activeWrites != 0,
                    "Cannot close active trace file: " << fileState.fileName);
    fileState.stream.close();
    NS_ABORT_MSG_IF(fileState.stream.fail(), "Failed to close trace file: " << fileState.fileName);
    fileState.open = false;
    --trace_open_files;
    ++trace_writer_stats.closeCount;
    if (lruClose)
    {
        ++trace_writer_stats.lruCloseCount;
    }
}

void
UbUtils::CommitTraceChunk(TraceFileState& fileState, std::string& chunk)
{
    if (chunk.empty())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> filesGuard(files_mutex);
        if (!fileState.open)
        {
            while (trace_open_files >= trace_writer_config.effectiveMaxOpenFiles)
            {
                TraceFileState* candidate = nullptr;
                for (auto& [name, state] : files)
                {
                    (void)name;
                    if (state.open && state.activeWrites == 0 && &state != &fileState &&
                        (candidate == nullptr || state.lastUse < candidate->lastUse))
                    {
                        candidate = &state;
                    }
                }
                NS_ABORT_MSG_IF(candidate == nullptr,
                                "Trace file descriptor limit reached with no closable sink");
                CloseTraceFile(*candidate, true);
            }

            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path parent = fs::path(fileState.fileName).parent_path();
            if (!parent.empty())
            {
                fs::create_directories(parent, ec);
                NS_ABORT_MSG_IF(ec,
                                "Failed to create trace parent dir: " << parent
                                                                      << " err=" << ec.message());
            }
            fileState.stream.clear();
            fileState.stream.open(fileState.fileName.c_str(), std::ios::out | std::ios::app);
            NS_ABORT_MSG_IF(!fileState.stream.is_open(),
                            "Cannot open trace file: " << fileState.fileName);
            fileState.open = true;
            ++trace_open_files;
            ++trace_writer_stats.openCount;
            trace_writer_stats.peakOpenFiles =
                std::max(trace_writer_stats.peakOpenFiles, trace_open_files);
        }
        ++fileState.activeWrites;
        fileState.lastUse = ++trace_lru_clock;
    }

    {
        std::lock_guard<std::mutex> fileGuard(fileState.mutex);
        fileState.stream.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        NS_ABORT_MSG_IF(fileState.stream.fail(),
                        "Failed to write trace file: " << fileState.fileName);
        fileState.stream.flush();
        NS_ABORT_MSG_IF(fileState.stream.fail(),
                        "Failed to flush trace file: " << fileState.fileName);
    }

    {
        std::lock_guard<std::mutex> filesGuard(files_mutex);
        --fileState.activeWrites;
        fileState.lastUse = ++trace_lru_clock;
        ++trace_writer_stats.writeCount;
        ++trace_writer_stats.flushCount;
    }
    std::string empty;
    chunk.swap(empty);
}

void
UbUtils::AppendTraceLine(const TraceSinkHandle& sink, std::string line)
{
    const uint64_t epoch = trace_writer_epoch.load(std::memory_order_acquire);
    NS_ABORT_MSG_IF(!sink.IsValid() || sink.epoch != epoch, "Stale or invalid trace sink handle");
    auto& fileState = *static_cast<TraceFileState*>(sink.state);
    TraceOwnerState& owner = GetTraceOwner(Simulator::GetSystemId());
    std::lock_guard<std::mutex> ownerGuard(owner.mutex);

    line.push_back('\n');
    const auto pressureFlush = [&](std::size_t incomingBytes) {
        std::vector<std::pair<TraceFileState*, std::string*>> pending;
        pending.reserve(owner.chunks.size());
        for (auto& [state, buffered] : owner.chunks)
        {
            if (!buffered.empty())
            {
                pending.emplace_back(state, &buffered);
            }
        }
        std::sort(pending.begin(), pending.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second->size() > rhs.second->size();
        });
        const std::size_t target = trace_writer_config.lowWaterBytes > incomingBytes
                                       ? trace_writer_config.lowWaterBytes - incomingBytes
                                       : 0;
        for (auto& [state, buffered] : pending)
        {
            if (owner.bufferedBytes <= target)
            {
                break;
            }
            const std::size_t bytes = buffered->size();
            CommitTraceChunk(*state, *buffered);
            owner.bufferedBytes -= bytes;
        }
        std::lock_guard<std::mutex> filesGuard(files_mutex);
        ++trace_writer_stats.pressureFlushCount;
    };

    if (owner.bufferedBytes + line.size() >= trace_writer_config.highWaterBytes)
    {
        pressureFlush(line.size());
    }

    std::string& chunk = owner.chunks[&fileState];
    if (!chunk.empty() && chunk.size() + line.size() > trace_writer_config.chunkBytes)
    {
        const std::size_t bytes = chunk.size();
        CommitTraceChunk(fileState, chunk);
        owner.bufferedBytes -= bytes;
    }
    if (line.size() >= trace_writer_config.chunkBytes)
    {
        CommitTraceChunk(fileState, line);
        return;
    }

    chunk += line;
    owner.bufferedBytes += line.size();
    {
        std::lock_guard<std::mutex> filesGuard(files_mutex);
        trace_writer_stats.peakBufferedBytes =
            std::max(trace_writer_stats.peakBufferedBytes, owner.bufferedBytes);
    }
    if (chunk.size() >= trace_writer_config.chunkBytes)
    {
        const std::size_t bytes = chunk.size();
        CommitTraceChunk(fileState, chunk);
        owner.bufferedBytes -= bytes;
    }
}

void
UbUtils::AppendTraceInfoNoTs(const TraceSinkHandle& sink, const std::string& info)
{
    AppendTraceLine(sink, info);
}

void
UbUtils::FlushTraceFile(TraceFileState& fileState)
{
    std::vector<TraceOwnerState*> owners;
    {
        std::lock_guard<std::mutex> filesGuard(files_mutex);
        owners.reserve(trace_owners.size());
        for (auto& [ownerId, owner] : trace_owners)
        {
            (void)ownerId;
            owners.push_back(&owner);
        }
    }
    for (TraceOwnerState* owner : owners)
    {
        std::lock_guard<std::mutex> ownerGuard(owner->mutex);
        auto it = owner->chunks.find(&fileState);
        if (it != owner->chunks.end() && !it->second.empty())
        {
            const std::size_t bytes = it->second.size();
            CommitTraceChunk(fileState, it->second);
            owner->bufferedBytes -= bytes;
        }
    }
}

void
UbUtils::FlushTraceOwner(TraceOwnerState& owner)
{
    std::lock_guard<std::mutex> ownerGuard(owner.mutex);
    for (auto& [fileState, chunk] : owner.chunks)
    {
        if (!chunk.empty())
        {
            CommitTraceChunk(*fileState, chunk);
        }
    }
    owner.bufferedBytes = 0;
}

bool
UbUtils::IsTraceEnabled()
{
    BooleanValue enabled;
    if (!GlobalValue::GetValueByNameFailSafe("UB_TRACE_ENABLE", enabled))
    {
        return false;
    }
    return enabled.Get();
}

bool
UbUtils::IsFlowControlTraceEnabled()
{
    if (!IsTraceEnabled())
    {
        return false;
    }

    BooleanValue enabled;
    if (!GlobalValue::GetValueByNameFailSafe("UB_FLOW_CONTROL_TRACE_ENABLE", enabled))
    {
        return false;
    }
    return enabled.Get();
}

bool
UbUtils::IsCongestionControlTraceEnabled()
{
    if (!IsTraceEnabled())
    {
        return false;
    }

    BooleanValue enabled;
    if (!GlobalValue::GetValueByNameFailSafe("UB_CONGESTION_CONTROL_TRACE_ENABLE", enabled))
    {
        return false;
    }
    return enabled.Get();
}

bool
UbUtils::IsQueueTraceEnabled()
{
    if (!IsTraceEnabled())
    {
        return false;
    }

    BooleanValue enabled;
    if (!GlobalValue::GetValueByNameFailSafe("UB_QUEUE_TRACE_ENABLE", enabled))
    {
        return false;
    }
    return enabled.Get();
}

uint32_t
UbUtils::ExtractMpiRank(uint32_t systemId)
{
#ifdef NS3_MTP
    return systemId & 0xFFFF;
#else
    return systemId;
#endif
}

bool
UbUtils::IsSameMpiRank(uint32_t lhsSystemId, uint32_t rhsSystemId)
{
    return ExtractMpiRank(lhsSystemId) == ExtractMpiRank(rhsSystemId);
}

bool
UbUtils::IsSystemOwnedByRank(uint32_t systemId, uint32_t currentRank)
{
    return ExtractMpiRank(systemId) == currentRank;
}

bool
UbUtils::IsFaultEnabled() const
{
    BooleanValue faultEnabled;
    g_fault_enable.GetValue(faultEnabled);
    return faultEnabled.Get();
}

void
UbUtils::ResetRuntimeDropDiagnostics()
{
    std::lock_guard<std::mutex> guard(runtime_drop_mutex);
    runtime_packet_drop_count = 0;
    runtime_packet_drop_reason.clear();
}

void
UbUtils::RecordRuntimePacketDrop(const std::string& reason)
{
    std::lock_guard<std::mutex> guard(runtime_drop_mutex);
    ++runtime_packet_drop_count;
    if (runtime_packet_drop_reason.empty())
    {
        runtime_packet_drop_reason = reason;
    }
}

uint64_t
UbUtils::GetRuntimePacketDropCount()
{
    std::lock_guard<std::mutex> guard(runtime_drop_mutex);
    return runtime_packet_drop_count;
}

std::string
UbUtils::GetRuntimePacketDropReason()
{
    std::lock_guard<std::mutex> guard(runtime_drop_mutex);
    return runtime_packet_drop_reason;
}

void UbUtils::PrintTimestamp(const std::string &message)
{
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime = *std::localtime(&nowTime);

    std::cout << "[" << std::put_time(&localTime, "%H:%M:%S") << "] " << message << std::endl;
}

void UbUtils::ParseTrace(bool isTest)
{
    BooleanValue val;
    g_parse_enable.GetValue(val);
    bool ParseEnable = val.Get();
    if (ParseEnable) {
        PrintTimestamp("[trace] Parse runlog into output artifacts.");

        // 从GlobalValue获取路径
        StringValue scriptPathValue;
        g_python_script_path.GetValue(scriptPathValue);
        string python_script_path = scriptPathValue.Get();

        string cmd = "python3 " + python_script_path + " " + trace_path;
        if (isTest) {
            cmd += " true";
        } else {
            cmd += " false";
        }
        int ret = system(cmd.c_str());
        if (ret == -1) {
            NS_ASSERT_MSG(0, "parse trace failed :" << cmd);
        }
    }
}

void UbUtils::Destroy()
{
    for (auto& event : queue_sampler_events)
    {
        event.second.Cancel();
    }
    queue_sampler_events.clear();
    queue_sampler_started = false;

    if (UbLifecycleTrace::GetLevel() != UbLifecycleTraceLevel::OFF)
    {
        for (auto node = NodeList::Begin(); node != NodeList::End(); ++node)
        {
            Ptr<UbLifecycleCoordinator> coordinator = (*node)->GetObject<UbLifecycleCoordinator>();
            if (coordinator != nullptr)
            {
                coordinator->EmitLifecycleSummary();
            }
        }
    }

    for (auto& [ownerId, owner] : trace_owners)
    {
        (void)ownerId;
        FlushTraceOwner(owner);
    }

    {
        std::lock_guard<std::mutex> filesGuard(files_mutex);
        for (auto& [fileName, fileState] : files)
        {
            (void)fileName;
            CloseTraceFile(fileState, false);
        }
        const char* report = std::getenv("UB_TRACE_WRITER_REPORT");
        if (report != nullptr && std::string_view(report) == "1")
        {
            std::cerr << "TRACE_WRITER_METRICS"
                      << " chunkBytes=" << trace_writer_config.chunkBytes
                      << " highWaterBytes=" << trace_writer_config.highWaterBytes
                      << " lowWaterBytes=" << trace_writer_config.lowWaterBytes
                      << " effectiveMaxOpenFiles=" << trace_writer_config.effectiveMaxOpenFiles
                      << " opens=" << trace_writer_stats.openCount
                      << " writes=" << trace_writer_stats.writeCount
                      << " flushes=" << trace_writer_stats.flushCount
                      << " closes=" << trace_writer_stats.closeCount
                      << " lruCloses=" << trace_writer_stats.lruCloseCount
                      << " pressureFlushes=" << trace_writer_stats.pressureFlushCount
                      << " peakOpenFiles=" << trace_writer_stats.peakOpenFiles
                      << " peakBufferedBytes=" << trace_writer_stats.peakBufferedBytes << '\n';
        }
        files.clear();
        trace_owners.clear();
        trace_writer_config = {};
        trace_writer_stats = {};
        trace_writer_configured = false;
        trace_open_files = 0;
        trace_lru_clock = 0;
        trace_writer_epoch.fetch_add(1, std::memory_order_release);
    }
    UbLifecycleTrace::Reset();

}

std::string UbUtils::PrepareTraceDir(const std::string &configPath)
{
    namespace fs = std::filesystem;

    fs::path caseDir = fs::path(configPath).parent_path();
    if (caseDir.empty()) {
        caseDir = fs::current_path();
    }

    std::string dirPath = caseDir.string();
    if (dirPath.empty()) {
        NS_ASSERT_MSG(0, "Not find testcase dir");
    }
    if (dirPath.back() != fs::path::preferred_separator) {
        dirPath.push_back(fs::path::preferred_separator);
    }

    fs::path runlog = caseDir / "runlog";
    std::error_code ec;
    if (fs::exists(runlog, ec)) {
        NS_ASSERT_MSG(!ec, "Failed to query runlog dir: " << ec.message());
        ec.clear();
        fs::remove_all(runlog, ec);
        NS_ASSERT_MSG(!ec, "Failed to remove runlog dir: " << ec.message());
        ec.clear();
    } else {
        NS_ASSERT_MSG(!ec, "Failed to query runlog dir: " << ec.message());
    }

    fs::create_directories(runlog, ec);
    NS_ASSERT_MSG(!ec, "Failed to recreate runlog dir: " << ec.message());
    return dirPath;
}

void
UbUtils::SetTracePathForTest(const std::string& tracePath)
{
    trace_path = tracePath;
    if (!trace_path.empty() && trace_path.back() != std::filesystem::path::preferred_separator)
    {
        trace_path.push_back(std::filesystem::path::preferred_separator);
    }
}

void UbUtils::CreateTraceDir()
{
    ResetRuntimeDropDiagnostics();
    trace_path = PrepareTraceDir(g_config_path);
    queue_sampler_started = false;
    queue_sampler_events.clear();

    BooleanValue traceEnabled;
    g_trace_enable.GetValue(traceEnabled);

    UbLifecycleTraceLevel resolvedLevel = UbLifecycleTraceLevel::OFF;
    if (traceEnabled.Get())
    {
        StringValue preset;
        StringValue lifecycleLevel;
        g_trace_preset.GetValue(preset);
        g_lifecycle_trace_level.GetValue(lifecycleLevel);
        resolvedLevel = UbLifecycleTrace::ResolveLevel(preset.Get(), lifecycleLevel.Get());
    }

    if (resolvedLevel == UbLifecycleTraceLevel::OFF)
    {
        UbLifecycleTrace::Configure(trace_path, resolvedLevel, 1, 0, 0, false, {});
        PrintTimestamp("[setup] Prepare runlog directory: " + trace_path + "runlog");
        return;
    }

    UintegerValue detailStride;
    UintegerValue detailOffset;
    g_lifecycle_detail_sample_stride.GetValue(detailStride);
    g_lifecycle_detail_sample_offset.GetValue(detailOffset);
    uint32_t rank = 0;
    bool rankScoped = false;
#ifdef NS3_MPI
    rankScoped = MpiInterface::IsEnabled();
    if (rankScoped)
    {
        rank = MpiInterface::GetSystemId();
    }
#endif
    UbLifecycleTrace::Configure(trace_path,
                                resolvedLevel,
                                detailStride.Get(),
                                detailOffset.Get(),
                                rank,
                                rankScoped,
                                [](const std::string& filename) {
                                    const auto sink = UbUtils::ResolveTraceSink(filename);
                                    return [sink](const std::string& line) {
                                        UbUtils::AppendTraceInfoNoTs(sink, line);
                                    };
                                });
    UbLifecycleTrace::EmitMetadata();
    PrintTimestamp("[setup] Prepare runlog directory: " + trace_path + "runlog");
}

void
UbUtils::QueueSampleNotify(uint32_t nodeId, uint32_t portId)
{
    if (trace_path.empty() || !IsQueueTraceEnabled())
    {
        return;
    }

    Ptr<Node> node = NodeList::GetNode(nodeId);
    if (node == nullptr)
    {
        return;
    }

    Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(portId));
    Ptr<UbSwitch> sw = node->GetObject<UbSwitch>();
    if (port == nullptr || sw == nullptr)
    {
        return;
    }

    const uint64_t voqBytes = sw->GetQueueManager()->GetTotalOutPortBufferUsed(portId);
    const uint64_t egressBytes = port->GetUbQueue()->GetCurrentBytes();
    const uint64_t totalBytes = voqBytes + egressBytes;

    std::ostringstream oss;
    oss << "Queue Update, source: SAMPLE"
        << " voqBytes: " << voqBytes
        << " egressBytes: " << egressBytes
        << " totalBytes: " << totalBytes;
    const std::string fileName =
        trace_path + "runlog/QueueTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, oss.str());
}

void
UbUtils::QueueSampleTick(uint32_t nodeId, uint32_t portId, uint32_t intervalNs)
{
    QueueSampleNotify(nodeId, portId);
    queue_sampler_events[{nodeId, portId}] =
        Simulator::Schedule(NanoSeconds(intervalNs),
                            &UbUtils::QueueSampleTick,
                            nodeId,
                            portId,
                            intervalNs);
}

void
UbUtils::StartQueueSampler()
{
    if (!IsQueueTraceEnabled())
    {
        return;
    }

    if (queue_sampler_started)
    {
        return;
    }

    UintegerValue intervalValue;
    g_queue_sample_interval.GetValue(intervalValue);
    const uint32_t intervalNs = intervalValue.Get();
    if (intervalNs == 0)
    {
        return;
    }

    const Time interval = NanoSeconds(intervalNs);
    for (uint32_t nodeId = 0; nodeId < NodeList::GetNNodes(); ++nodeId)
    {
        Ptr<Node> node = NodeList::GetNode(nodeId);
        if (node == nullptr)
        {
            continue;
        }
        Ptr<UbSwitch> sw = node->GetObject<UbSwitch>();
        if (sw == nullptr)
        {
            continue;
        }
        const uint32_t devicesNum = node->GetNDevices();
        for (uint32_t portId = 0; portId < devicesNum; ++portId)
        {
            Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(portId));
            if (port == nullptr)
            {
                continue;
            }
            queue_sampler_events[{nodeId, portId}] =
                Simulator::Schedule(interval,
                                    &UbUtils::QueueSampleTick,
                                    nodeId,
                                    portId,
                                    intervalNs);
        }
    }

    queue_sampler_started = true;
}

void UbUtils::PrintTraceInfo(const string& fileName, const string& info)
{
    std::string line = "[";
    line += std::to_string(Simulator::Now().GetSeconds() * 1e6);
    line += "us] ";
    line += info;
    AppendTraceLine(GetCachedTraceSink(fileName), std::move(line));
}

void UbUtils::PrintTraceInfoNoTs(const string& fileName, const string& info)
{
    AppendTraceInfoNoTs(GetCachedTraceSink(fileName), info);
}

void
UbUtils::TpgPhaseCreateNotify(uint32_t srcNode,
                              uint32_t dstNode,
                              uint32_t taskId,
                              uint32_t memberCount,
                              uint32_t initialIndex,
                              uint16_t initialPort,
                              const std::vector<uint16_t>& memberPorts,
                              const std::string& strategy)
{
    BooleanValue enabled;
    if (!GlobalValue::GetValueByNameFailSafe("UB_TRACE_ENABLE", enabled) || !enabled.Get())
    {
        return;
    }
    if (!GlobalValue::GetValueByNameFailSafe("UB_TPG_PHASE_TRACE_ENABLE", enabled) || !enabled.Get())
    {
        return;
    }
    std::ostringstream oss;
    oss << "TPG_CREATE: src=" << srcNode << " dst=" << dstNode << " taskId=" << taskId
        << " memberCount=" << memberCount << " initialIndex=" << initialIndex
        << " initialPort=" << initialPort << " strategy=" << strategy << " memberPorts=";
    for (size_t i = 0; i < memberPorts.size(); ++i)
    {
        if (i != 0)
        {
            oss << ',';
        }
        oss << memberPorts[i];
    }
    PrintTraceInfoNoTs(trace_path + "runlog/TPGPhaseTrace.tr", oss.str());
}

inline void UbUtils::TpFirstPacketSendsNotify(
    uint32_t nodeId, uint32_t taskId, uint32_t tpn, uint32_t dstTpn, uint32_t tpMsn, uint32_t psnSndNxt, uint32_t sPort)
{
    // 使用 std::ostringstream 来减少字符串构造的次数
    std::ostringstream oss;
    oss << "First Packet Sends, taskId: " << taskId << " srcTpn: " << tpn << " destTpn: " << dstTpn
        << " tpMsn: " << tpMsn << " psn: " << psnSndNxt << " portId: " << sPort << " lastPacket: 0";
    string info = oss.str();
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::TpLastPacketSendsNotify(
    uint32_t nodeId, uint32_t taskId, uint32_t tpn, uint32_t dstTpn, uint32_t tpMsn, uint32_t psnSndNxt, uint32_t sPort)
{
    std::ostringstream oss;
    oss << "Last Packet Sends,taskId: " << taskId << " srcTpn: " << tpn << " destTpn: " << dstTpn << " tpMsn: " << tpMsn
        << " psn: " << psnSndNxt << " portId: " << sPort << " lastPacket: 1";
    string info = oss.str();
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::TpLastPacketACKsNotify(
    uint32_t nodeId, uint32_t taskId, uint32_t tpn, uint32_t dstTpn, uint32_t tpMsn, uint32_t psn, uint32_t sPort)
{
    std::ostringstream oss;
    oss << "Last Packet ACKs,taskId: " << taskId << " srcTpn: " << tpn << " destTpn: " << dstTpn << " tpMsn: " << tpMsn
        << " psn: " << psn << " portId: " << sPort << " lastPacket: 1";
    string info = oss.str();
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::TpLastPacketReceivesNotify(
    uint32_t nodeId, uint32_t srcTpn, uint32_t dstTpn, uint32_t tpMsn, uint32_t psn, uint32_t dPort)
{
    std::ostringstream oss;
    oss << "Last Packet Receives,srcTpn: " << srcTpn << " destTpn: " << dstTpn << " tpMsn: " << tpMsn
        << " psn: " << psn << " inportId: " << dPort << " lastPacket: 1";
    string info = oss.str();
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void
UbUtils::CtpFirstPacketSendsNotify(uint32_t nodeId,
                                   uint32_t taskId,
                                   uint32_t srcNodeId,
                                   uint32_t dstNodeId,
                                   uint32_t srcEntityId,
                                   uint32_t dstEntityId,
                                   uint32_t vl,
                                   uint32_t taSsn,
                                   uint32_t outPort,
                                   uint32_t payloadBytes,
                                   uint32_t opcode)
{
    std::ostringstream oss;
    oss << "First Packet Sends, taskId: " << taskId << " transport: CTP"
        << " srcNode: " << srcNodeId << " dstNode: " << dstNodeId
        << " srcEntity: " << srcEntityId << " dstEntity: " << dstEntityId
        << " vl: " << vl << " taSsn: " << taSsn << " outPort: " << outPort
        << " payloadBytes: " << payloadBytes << " taOpcode: " << opcode << " lastPacket: 0";
    string info = oss.str();
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void
UbUtils::CtpLastPacketACKsNotify(uint32_t nodeId,
                                 uint32_t taskId,
                                 uint32_t srcNodeId,
                                 uint32_t dstNodeId,
                                 uint32_t srcEntityId,
                                 uint32_t dstEntityId,
                                 uint32_t vl,
                                 uint32_t taSsn,
                                 uint32_t outPort,
                                 uint32_t payloadBytes,
                                 uint32_t opcode)
{
    std::ostringstream oss;
    oss << "Last Packet ACKs, taskId: " << taskId << " transport: CTP"
        << " srcNode: " << srcNodeId << " dstNode: " << dstNodeId
        << " srcEntity: " << srcEntityId << " dstEntity: " << dstEntityId
        << " vl: " << vl << " taSsn: " << taSsn;
    if (outPort != UINT32_MAX)
    {
        oss << " outPort: " << outPort;
    }
    oss << " payloadBytes: " << payloadBytes << " taOpcode: " << opcode << " lastPacket: 1";
    string info = oss.str();
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::TpWqeSegmentSendsNotify(uint32_t nodeId, uint32_t taskId, uint32_t taSsn)
{
    std::ostringstream oss;
    oss << "WQE Segment Sends,taskId: " << taskId << " TASSN: " << taSsn;
    string info = oss.str();
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::TpWqeSegmentCompletesNotify(uint32_t nodeId, uint32_t taskId, uint32_t taSsn)
{
    std::ostringstream oss;
    oss << "WQE Segment Completes,taskId: " << taskId << " TASSN: " << taSsn;
    string info = oss.str();
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline string UbUtils::Among(string s, string ts)
{
    string res = s;
    // 添加空格使字符串和时间戳对齐
    if (s.size() >= ts.size()) {
        res.insert(0, 1, ' ');
        res.insert(res.end(), 1, ' ');
    } else {
        res.insert(0, (ts.size() - s.size()) / 2 + 1, ' ');
        res.insert(res.end(), ts.size() - s.size() - (ts.size() - s.size()) / 2 + 1, ' ');
    }
    return res;
}

void UbUtils::TpRecvNotify(uint32_t packetUid, uint32_t psn, uint32_t src, uint32_t dst, uint32_t srcTpn,
                           uint32_t dstTpn, PacketType type, uint32_t size, uint32_t taskId,
                           std::string ackInfo, UbPacketTraceTag traceTag)
{
    const char* pktType = "CONTROL";
    switch (type) {
    case PacketType::PACKET:
        pktType = "PKT";
        break;
    case PacketType::ACK:
        pktType = "ACK";
        break;
    case PacketType::NAK:
        pktType = "NAK";
        break;
    case PacketType::SACK:
        pktType = "SACK";
        break;
    case PacketType::CONTROL_FRAME:
        break;
    }

    std::ostringstream oss;
    oss << "Uid:" << packetUid << " Psn:" << psn << " Src:" << src << " Dst:" << dst << " SrcTpn:" << srcTpn
        << " DstTpn:" << dstTpn << " Type:" << pktType;
    if (!ackInfo.empty()) {
        oss << " AckInfo:" << ackInfo;
    }
    oss << " Size:" << size << " TaskId:" << taskId << '\n';
    for (uint32_t i = 0; i < traceTag.GetTraceLenth(); i++) {
        uint32_t node = traceTag.GetNodeTrace(i);
        PortTrace trace = traceTag.GetPortTrace(node);
        if (i == 0) {
            oss << "[" << node << "][" << Among(std::to_string(trace.sendPort), std::to_string(trace.sendTime)) << "]"
                << "--->";
        } else if (i == traceTag.GetTraceLenth() - 1) {
            oss << "[" << Among(std::to_string(trace.recvPort), std::to_string(trace.recvTime)) << "][" << node << "]"
                << '\n';
        } else {
            oss << "[" << Among(std::to_string(trace.recvPort), std::to_string(trace.recvTime)) << "]"
                << "[" << node << "]"
                << "[" << Among(std::to_string(trace.sendPort), std::to_string(trace.sendTime)) << "]"
                << "--->";
        }
    }
    for (uint32_t i = 0; i < traceTag.GetTraceLenth(); i++) {
        uint32_t node = traceTag.GetNodeTrace(i);
        PortTrace trace = traceTag.GetPortTrace(node);
        if (i == 0) {
            oss << std::string(std::to_string(node).size() + 2, ' ') << "["
                << Among(std::to_string(trace.sendTime), std::to_string(trace.sendTime)) << "]" << std::string(4, ' ');
        } else if (i == traceTag.GetTraceLenth() - 1) {
            oss << "[" << Among(std::to_string(trace.recvTime), std::to_string(trace.recvTime)) << "]" << '\n';
        } else {
            oss << "[" << Among(std::to_string(trace.recvTime), std::to_string(trace.recvTime)) << "]"
                << std::string(std::to_string(node).size() + 2, ' ') << "["
                << Among(std::to_string(trace.sendTime), std::to_string(trace.sendTime)) << "]" << std::string(4, ' ');
        }
    }
    string info = oss.str();
    const char* fileType = (type == PacketType::NAK || type == PacketType::SACK) ? "ACK" : pktType;
    string fileName = trace_path + "runlog/AllPacketTrace_" + fileType + "_node_" + to_string(src) + ".tr";
    PrintTraceInfoNoTs(fileName, info);
}

void UbUtils::LdstRecvNotify(uint32_t packetUid, uint32_t src, uint32_t dst, PacketType type,
                             uint32_t size, uint32_t taskId, UbPacketTraceTag traceTag)
{
    const char* pktType = "CONTROL";
    switch (type) {
    case PacketType::PACKET:
        pktType = "PKT";
        break;
    case PacketType::ACK:
        pktType = "ACK";
        break;
    case PacketType::NAK:
        pktType = "NAK";
        break;
    case PacketType::SACK:
        pktType = "SACK";
        break;
    case PacketType::CONTROL_FRAME:
        break;
    }

    std::ostringstream oss;
    oss << "Uid:" << packetUid << " Src:" << src << " Dst:" << dst
        << " Type:" << pktType << " Size:" << size << " TaskId:" << taskId << '\n';
    for (uint32_t i = 0; i < traceTag.GetTraceLenth(); i++) {
        uint32_t node = traceTag.GetNodeTrace(i);
        PortTrace trace = traceTag.GetPortTrace(node);
        if (i == 0) {
            oss << "[" << node << "][" << Among(std::to_string(trace.sendPort), std::to_string(trace.sendTime)) << "]"
                << "--->";
        } else if (i == traceTag.GetTraceLenth() - 1) {
            oss << "[" << Among(std::to_string(trace.recvPort), std::to_string(trace.recvTime)) << "][" << node << "]"
                << '\n';
        } else {
            oss << "[" << Among(std::to_string(trace.recvPort), std::to_string(trace.recvTime)) << "]"
                << "[" << node << "]"
                << "[" << Among(std::to_string(trace.sendPort), std::to_string(trace.sendTime)) << "]"
                << "--->";
        }
    }
    for (uint32_t i = 0; i < traceTag.GetTraceLenth(); i++) {
        uint32_t node = traceTag.GetNodeTrace(i);
        PortTrace trace = traceTag.GetPortTrace(node);
        if (i == 0) {
            oss << std::string(std::to_string(node).size() + 2, ' ') << "["
                << Among(std::to_string(trace.sendTime), std::to_string(trace.sendTime)) << "]" << std::string(4, ' ');
        } else if (i == traceTag.GetTraceLenth() - 1) {
            oss << "[" << Among(std::to_string(trace.recvTime), std::to_string(trace.recvTime)) << "]" << '\n';
        } else {
            oss << "[" << Among(std::to_string(trace.recvTime), std::to_string(trace.recvTime)) << "]"
                << std::string(std::to_string(node).size() + 2, ' ') << "["
                << Among(std::to_string(trace.sendTime), std::to_string(trace.sendTime)) << "]" << std::string(4, ' ');
        }
    }
    string info = oss.str();
    const char* fileType = (type == PacketType::NAK || type == PacketType::SACK) ? "ACK" : pktType;
    string fileName = trace_path + "runlog/AllPacketTrace_" + fileType + "_node_" + to_string(src) + ".tr";
    PrintTraceInfoNoTs(fileName, info);
}

inline void UbUtils::LdstFirstPacketSendsNotify(uint32_t nodeId, uint32_t taskId)
{
    string info = "First Packet Sends,taskId: " + std::to_string(taskId);
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::DagMemTaskStartsNotify(uint32_t nodeId, uint32_t taskId)
{
    string info = "MEM Task Starts, taskId: " + std::to_string(taskId);
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::DagMemTaskCompletesNotify(uint32_t nodeId, uint32_t taskId)
{
    string info = "MEM Task Completes, taskId: " + std::to_string(taskId);
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::DagComputeTaskQueuedNotify(uint32_t nodeId, uint32_t taskId)
{
    string info = "COMPUTE Task Queued, taskId: " + std::to_string(taskId);
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::DagComputeTaskStartsNotify(uint32_t nodeId, uint32_t taskId)
{
    string info = "COMPUTE Task Starts, taskId: " + std::to_string(taskId);
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::DagComputeTaskCompletesNotify(uint32_t nodeId, uint32_t taskId)
{
    string info = "COMPUTE Task Completes, taskId: " + std::to_string(taskId);
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::DagWqeTaskStartsNotify(uint32_t nodeId, uint32_t jettyNum, uint32_t taskId)
{
    std::ostringstream oss;
    oss << "WQE Starts, jettyNum: " << jettyNum << " taskId: " << taskId;
    string info = oss.str();
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::DagWqeTaskCompletesNotify(uint32_t nodeId, uint32_t jettyNum, uint32_t taskId)
{
    std::ostringstream oss;
    oss << "WQE Completes, jettyNum: " << jettyNum << " taskId: " << taskId;
    string info = oss.str();
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::PortTxNotify(uint32_t nodeId, uint32_t portId, uint32_t size)
{
    std::ostringstream oss;
    oss << "Port Tx, port ID: " << portId << " PacketSize: " << size;
    string info = oss.str();
    string fileName = trace_path + "runlog/PortTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::PortRxNotify(uint32_t nodeId, uint32_t portId, uint32_t size)
{
    std::ostringstream oss;
    oss << "Port Rx, port ID: " << portId << " PacketSize: " << size;
    string info = oss.str();
    string fileName = trace_path + "runlog/PortTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::PfcStateNotify(uint32_t nodeId,
                             uint32_t portId,
                             const std::string& action,
                             uint32_t priority,
                             uint64_t ingressBytes)
{
    if (trace_path.empty() || !IsFlowControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << "PFC " << action
        << ", priority: " << priority
        << " ingressBytes: " << ingressBytes;
    string info = oss.str();
    string fileName = trace_path + "runlog/PfcTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::PfcDynamicStateNotify(uint32_t nodeId,
                                    uint32_t portId,
                                    uint32_t priority,
                                    uint64_t ingressTotalBytes,
                                    uint64_t sharedUsedBytes,
                                    uint64_t headroomUsedBytes,
                                    uint64_t xoffBytes,
                                    uint64_t xonBytes,
                                    bool pause)
{
    if (trace_path.empty() || !IsFlowControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << "PFC_DYNAMIC " << (pause ? "PAUSE" : "RESUME")
        << ", priority: " << priority
        << " ingressTotalBytes: " << ingressTotalBytes
        << " sharedUsedBytes: " << sharedUsedBytes
        << " headroomUsedBytes: " << headroomUsedBytes
        << " xoffBytes: " << xoffBytes
        << " xonBytes: " << xonBytes;
    string info = oss.str();
    string fileName =
        trace_path + "runlog/PfcDynamicTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::CbfcStateNotify(uint32_t nodeId,
                              uint32_t portId,
                              const std::string& action,
                              uint32_t priority,
                              int32_t availableCredits,
                              uint32_t nextPacketBytes)
{
    if (trace_path.empty() || !IsFlowControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << "CBFC " << action
        << ", priority: " << priority
        << " availableCredits: " << availableCredits
        << " nextPacketBytes: " << nextPacketBytes;
    string info = oss.str();
    string fileName = trace_path + "runlog/CbfcTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::CbfcCreditRestoreTraceNotify(uint32_t nodeId,
                                           uint32_t portId,
                                           const std::vector<uint8_t>& credits)
{
    if (trace_path.empty() || !IsFlowControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << "CBFC CREDIT_RESTORE";
    for (size_t i = 0; i < credits.size(); ++i)
    {
        oss << (i == 0 ? ", credits:" : " ") << static_cast<uint32_t>(credits[i]);
    }
    string info = oss.str();
    string fileName = trace_path + "runlog/CbfcTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::CbfcCreditLevelNotify(uint32_t nodeId,
                                    uint32_t portId,
                                    const std::string& reason,
                                    uint32_t priority,
                                    int32_t availableCredits,
                                    int32_t deltaCredits)
{
    if (trace_path.empty() || !IsFlowControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << "CBFC CREDIT_LEVEL"
        << ", reason: " << reason
        << " priority: " << priority
        << " availableCredits: " << availableCredits
        << " deltaCredits: " << deltaCredits;
    string info = oss.str();
    string fileName = trace_path + "runlog/CbfcTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::CbfcControlSendNotify(uint32_t nodeId,
                                    uint32_t portId,
                                    const std::string& reason,
                                    int32_t triggerThresholdCells,
                                    int32_t emitMinPendingCells,
                                    const std::vector<int32_t>& pendingCredits,
                                    const std::vector<uint8_t>& sendCredits)
{
    if (trace_path.empty() || !IsFlowControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << "CBFC CONTROL_SEND"
        << ", reason: " << reason
        << " triggerThresholdCells: " << triggerThresholdCells
        << " emitMinPendingCells: " << emitMinPendingCells
        << " pendingCredits:";
    for (size_t i = 0; i < pendingCredits.size(); ++i)
    {
        oss << (i == 0 ? "" : " ") << pendingCredits[i];
    }
    oss << " sendCredits:";
    for (size_t i = 0; i < sendCredits.size(); ++i)
    {
        oss << (i == 0 ? "" : " ") << static_cast<uint32_t>(sendCredits[i]);
    }
    string info = oss.str();
    string fileName = trace_path + "runlog/CbfcTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::DcqcnMarkNotify(uint32_t nodeId,
                              uint32_t outPort,
                              uint64_t totalQueueBytes,
                              double markProbability)
{
    if (trace_path.empty() || !IsCongestionControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6)
        << "DCQCN MARK, outPort: " << outPort
        << " totalQueueBytes: " << totalQueueBytes
        << " markProbability: " << markProbability;
    string info = oss.str();
    string fileName =
        trace_path + "runlog/DcqcnMarkTrace_node_" + to_string(nodeId) + "_port_" + to_string(outPort) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::DcqcnCnpNotify(uint32_t nodeId,
                             uint32_t tpn,
                             const std::string& action,
                             uint8_t ecn,
                             bool location)
{
    if (trace_path.empty() || !IsCongestionControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << "DCQCN CNP " << action
        << ", ecn: " << static_cast<uint32_t>(ecn)
        << " location: " << static_cast<uint32_t>(location);
    string info = oss.str();
    string fileName = trace_path + "runlog/DcqcnCnpTrace_node_" + to_string(nodeId) + "_tpn_" + to_string(tpn) + ".tr";
    PrintTraceInfo(fileName, info);
}

void UbUtils::DcqcnSenderStateNotify(uint32_t nodeId,
                                     uint32_t tpn,
                                     const std::string& reason,
                                     double alpha,
                                     uint64_t currentRateBps,
                                     uint64_t targetRateBps,
                                     uint64_t bytesSinceLastIncrease,
                                     uint32_t timeRecoveryEvents,
                                     uint32_t byteRecoveryEvents)
{
    if (trace_path.empty() || !IsCongestionControlTraceEnabled())
    {
        return;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6)
        << "DCQCN SENDER " << reason
        << ", alpha: " << alpha
        << " currentRateBps: " << currentRateBps
        << " targetRateBps: " << targetRateBps
        << " bytesSinceLastIncrease: " << bytesSinceLastIncrease
        << " timeRecoveryEvents: " << timeRecoveryEvents
        << " byteRecoveryEvents: " << byteRecoveryEvents;
    string info = oss.str();
    string fileName =
        trace_path + "runlog/DcqcnSenderTrace_node_" + to_string(nodeId) + "_tpn_" + to_string(tpn) + ".tr";
    PrintTraceInfo(fileName, info);
}

void
UbUtils::CaqmAckNotify(uint32_t nodeId,
                       uint32_t tpn,
                       uint32_t psnStart,
                       uint32_t psnEnd,
                       uint8_t cE,
                       uint8_t iE,
                       uint16_t hintE)
{
    if (trace_path.empty() || !IsCongestionControlTraceEnabled())
    {
        return;
    }
    if (cE == 0 && iE == 0 && hintE == 0)
    {
        return;
    }
    std::ostringstream oss;
    oss << "CAQM ACK"
        << ", psnStart: " << psnStart
        << " psnEnd: " << psnEnd
        << " cE: " << static_cast<uint32_t>(cE)
        << " iE: " << static_cast<uint32_t>(iE)
        << " hintE: " << hintE;
    string info = oss.str();
    string fileName = trace_path + "runlog/CaqmAckTrace_node_" + to_string(nodeId) + "_tpn_" + to_string(tpn) + ".tr";
    PrintTraceInfo(fileName, info);
}

void
UbUtils::CaqmSenderStateNotify(uint32_t nodeId,
                               uint32_t tpn,
                               uint32_t psn,
                               uint32_t sequence,
                               uint32_t inFlight,
                               uint32_t cwnd,
                               uint8_t cE,
                               bool iE,
                               uint16_t hint)
{
    if (trace_path.empty() || !IsCongestionControlTraceEnabled())
    {
        return;
    }
    if (cE == 0 && iE && hint == 0)
    {
        return;
    }
    std::ostringstream oss;
    oss << "CAQM SENDER"
        << ", psn: " << psn
        << " sequence: " << sequence
        << " inFlight: " << inFlight
        << " cwnd: " << cwnd
        << " cE: " << static_cast<uint32_t>(cE)
        << " iE: " << static_cast<uint32_t>(iE)
        << " hint: " << hint;
    string info = oss.str();
    string fileName =
        trace_path + "runlog/CaqmSenderTrace_node_" + to_string(nodeId) + "_tpn_" + to_string(tpn) + ".tr";
    PrintTraceInfo(fileName, info);
}

bool UbUtils::IsTpDebugEnabledFor(uint32_t nodeId, uint32_t tpn)
{
    BooleanValue enabledValue;
    if (!GlobalValue::GetValueByNameFailSafe("UB_TP_DEBUG_ENABLE", enabledValue) || !enabledValue.Get())
    {
        return false;
    }

    UintegerValue debugNodeIdValue;
    UintegerValue debugTpnValue;
    UintegerValue debugStartNsValue;
    UintegerValue debugEndNsValue;
    GlobalValue::GetValueByName("UB_TP_DEBUG_NODE_ID", debugNodeIdValue);
    GlobalValue::GetValueByName("UB_TP_DEBUG_TPN", debugTpnValue);
    GlobalValue::GetValueByName("UB_TP_DEBUG_START_NS", debugStartNsValue);
    GlobalValue::GetValueByName("UB_TP_DEBUG_END_NS", debugEndNsValue);

    if (nodeId != debugNodeIdValue.Get() || tpn != debugTpnValue.Get())
    {
        return false;
    }

    const uint64_t nowNs = static_cast<uint64_t>(Simulator::Now().GetNanoSeconds());
    if (nowNs < debugStartNsValue.Get())
    {
        return false;
    }
    const uint32_t endNs = debugEndNsValue.Get();
    if (endNs != 0 && nowNs > endNs)
    {
        return false;
    }
    return !trace_path.empty();
}

void UbUtils::TpDebugStateNotify(uint32_t nodeId,
                                 uint32_t tpn,
                                 const std::string& reason,
                                 uint64_t psnSndNxt,
                                 uint64_t psnSndUna,
                                 uint64_t inflightPackets,
                                 uint64_t maxInflightPackets,
                                 bool inflightLimited,
                                 bool ccLimited,
                                 bool sendWindowLimited,
                                 uint32_t activeSegments,
                                 uint32_t totalSegments,
                                 uint32_t ackQueueLen,
                                 uint32_t cnpQueueLen)
{
    if (!IsTpDebugEnabledFor(nodeId, tpn))
    {
        return;
    }

    std::ostringstream oss;
    oss << "TP DEBUG " << reason
        << " psnSndNxt: " << psnSndNxt
        << " psnSndUna: " << psnSndUna
        << " inflightPackets: " << inflightPackets
        << " maxInflightPackets: " << maxInflightPackets
        << " inflightLimited: " << static_cast<uint32_t>(inflightLimited)
        << " ccLimited: " << static_cast<uint32_t>(ccLimited)
        << " sendWindowLimited: " << static_cast<uint32_t>(sendWindowLimited)
        << " activeSegments: " << activeSegments
        << " totalSegments: " << totalSegments
        << " ackQueueLen: " << ackQueueLen
        << " cnpQueueLen: " << cnpQueueLen;
    const string info = oss.str();
    const string fileName =
        trace_path + "runlog/TpDebugTrace_node_" + to_string(nodeId) + "_tpn_" + to_string(tpn) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::QueueVoqNotify(uint32_t nodeId, uint32_t portId, uint64_t voqBytes)
{
    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<UbPort> port = node != nullptr ? DynamicCast<UbPort>(node->GetDevice(portId)) : nullptr;
    const uint64_t egressBytes = port != nullptr ? port->GetUbQueue()->GetCurrentBytes() : 0;
    const uint64_t totalBytes = voqBytes + egressBytes;

    std::ostringstream oss;
    oss << "Queue Update, source: VOQ"
        << " voqBytes: " << voqBytes
        << " egressBytes: " << egressBytes
        << " totalBytes: " << totalBytes;
    string info = oss.str();
    string fileName = trace_path + "runlog/QueueTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void
UbUtils::QueueIngressOccupancyNotify(uint32_t nodeId,
                                     uint32_t inPort,
                                     uint32_t priority,
                                     uint64_t bytes)
{
    std::ostringstream oss;
    oss << "Queue Update, source: ingress"
        << " inPort: " << inPort
        << " priority: " << priority
        << " bytes: " << bytes;
    string info = oss.str();
    string fileName =
        trace_path + "runlog/QueueTrace_node_" + to_string(nodeId) + "_port_" + to_string(inPort) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::QueueEgressEnqueueNotify(uint32_t nodeId,
                                              uint32_t portId,
                                              Ptr<const Packet> packet,
                                              uint32_t egressBytes)
{
    (void)packet;
    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<UbSwitch> sw = node != nullptr ? node->GetObject<UbSwitch>() : nullptr;
    const uint64_t voqBytes = sw != nullptr ? sw->GetQueueManager()->GetTotalOutPortBufferUsed(portId) : 0;
    const uint64_t totalBytes = voqBytes + egressBytes;

    std::ostringstream oss;
    oss << "Queue Update, source: EGRESS_ENQUEUE"
        << " voqBytes: " << voqBytes
        << " egressBytes: " << egressBytes
        << " totalBytes: " << totalBytes;
    string info = oss.str();
    string fileName = trace_path + "runlog/QueueTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::QueueEgressDequeueNotify(uint32_t nodeId,
                                              uint32_t portId,
                                              Ptr<const Packet> packet,
                                              uint32_t egressBytes)
{
    (void)packet;
    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<UbSwitch> sw = node != nullptr ? node->GetObject<UbSwitch>() : nullptr;
    const uint64_t voqBytes = sw != nullptr ? sw->GetQueueManager()->GetTotalOutPortBufferUsed(portId) : 0;
    const uint64_t totalBytes = voqBytes + egressBytes;

    std::ostringstream oss;
    oss << "Queue Update, source: EGRESS_DEQUEUE"
        << " voqBytes: " << voqBytes
        << " egressBytes: " << egressBytes
        << " totalBytes: " << totalBytes;
    string info = oss.str();
    string fileName = trace_path + "runlog/QueueTrace_node_" + to_string(nodeId) + "_port_" + to_string(portId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::LdstThreadMemTaskStartsNotify(uint32_t nodeId, uint32_t memTaskId)
{
    string info = "Mem Task Starts,taskId: " + std::to_string(memTaskId);
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::LdstMemTaskCompletesNotify(uint32_t nodeId, uint32_t taskId)
{
    string info = "Mem Task Completes,taskId: " + std::to_string(taskId);
    string fileName = trace_path + "runlog/TaskTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::LdstThreadFirstPacketSendsNotify(uint32_t nodeId, uint32_t memTaskId)
{
    string info = "First Packet Sends, taskId: " + std::to_string(memTaskId);
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::LdstThreadLastPacketSendsNotify(uint32_t nodeId, uint32_t memTaskId)
{
    string info = "Last Packet Sends, taskId: " + std::to_string(memTaskId);
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::LdstLastPacketACKsNotify(uint32_t nodeId, uint32_t taskId)
{
    string info = "Last Packet ACKs,taskId: " + std::to_string(taskId);
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::LdstPeerSendFirstPacketACKsNotify(uint32_t nodeId, uint32_t taskId, uint32_t type)
{
    std::ostringstream oss;
    oss << "Peer Send First Packet ACKs, taskId: " << taskId << " type: " << type;
    string info = oss.str();
    string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
    PrintTraceInfo(fileName, info);
}

inline void UbUtils::SwitchLastPacketTraversesNotify(uint32_t nodeId, UbTransportHeader ubTpHeader)
{
    if (ubTpHeader.GetLastPacket()) {
        std::ostringstream oss;
        oss << "Last Packet Traverses ,NodeId: " << nodeId << " srcTpn: " << ubTpHeader.GetSrcTpn()
            << " destTpn: " << ubTpHeader.GetDestTpn() << " tpMsn: " << ubTpHeader.GetTpMsn()
            << " psn:" << ubTpHeader.GetPsn();
        string info = oss.str();
        string fileName = trace_path + "runlog/PacketTrace_node_" + to_string(nodeId) + ".tr";
        PrintTraceInfo(fileName, info);
    }
}

Time
UbUtils::ResolveLinkDelayWithOffset(Time baseDelay,
                                    Time offsetWindow,
                                    uint32_t offsetSeed,
                                    uint32_t node1,
                                    uint32_t port1,
                                    uint32_t node2,
                                    uint32_t port2)
{
    NS_ABORT_MSG_IF(baseDelay.IsStrictlyNegative(), "link delay must be non-negative");
    NS_ABORT_MSG_IF(offsetWindow.IsStrictlyNegative(),
                    "link delay offset window must be non-negative");
    if (baseDelay.IsZero() || offsetWindow.IsZero())
    {
        return baseDelay;
    }

    std::pair<uint32_t, uint32_t> endpoint1{node1, port1};
    std::pair<uint32_t, uint32_t> endpoint2{node2, port2};
    if (endpoint2 < endpoint1)
    {
        std::swap(endpoint1, endpoint2);
    }

    uint64_t hash =
        MixLinkDelayOffsetHash(static_cast<uint64_t>(offsetSeed) ^ 0x6c696e6b2d646c79ULL);
    hash = MixLinkDelayOffsetHash(hash ^ endpoint1.first);
    hash = MixLinkDelayOffsetHash(hash ^ endpoint1.second);
    hash = MixLinkDelayOffsetHash(hash ^ endpoint2.first);
    hash = MixLinkDelayOffsetHash(hash ^ endpoint2.second);

    const auto slotCount = static_cast<uint64_t>(offsetWindow.GetTimeStep());
    return baseDelay + TimeStep(static_cast<int64_t>(hash % slotCount));
}

UbStructuredTopologyConfig
UbUtils::ReadStructuredTopologyConfig(std::istream& input)
{
    const std::string magic = ReadStructuredToken(input, "protocol header");
    NS_ABORT_MSG_IF(magic != "NS3_UB_STRUCTURED",
                    "invalid structured topology protocol header: " << magic);
    const uint32_t version = ReadStructuredUint32(input, "protocol version");
    NS_ABORT_MSG_IF(version != 1 && version != 2 && version != 3 && version != 4,
                    "unsupported structured topology protocol version: " << version);

    UbStructuredTopologyConfig config;
    uint32_t count = 0;
    ReadStructuredSection(input, "NODES", count);
    config.nodes.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        config.nodes.push_back(UbStructuredNodeConfig{
            ReadStructuredUint32(input, "node ID"),
            ReadStructuredToken(input, "node type"),
            ReadStructuredUint32(input, "port count"),
            ReadStructuredToken(input, "allocation delay"),
            ReadStructuredToken(input, "forward delay"),
            ReadStructuredUint32(input, "system ID"),
        });
    }

    // Normalize prepared files before table/index materialization. Node IDs remain the
    // canonical identity; only an unambiguous record-order variation is repaired here.
    std::sort(config.nodes.begin(), config.nodes.end(), [](const auto& left, const auto& right) {
        return left.nodeId < right.nodeId;
    });
    for (uint32_t index = 0; index < config.nodes.size(); ++index)
    {
        NS_ABORT_MSG_IF(config.nodes[index].nodeId != index,
                        "structured topology node IDs must be unique and contiguous from zero");
    }

    ReadStructuredSection(input, "LINKS", count);
    config.links.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        config.links.push_back(UbStructuredLinkConfig{
            ReadStructuredUint32(input, "first link node"),
            ReadStructuredUint32(input, "first link port"),
            ReadStructuredUint32(input, "second link node"),
            ReadStructuredUint32(input, "second link port"),
            ReadStructuredToken(input, "link bandwidth"),
            ReadStructuredToken(input, "link delay"),
        });
    }

    if (version >= 4)
    {
        const std::string section = ReadStructuredToken(input, "routing fingerprint section");
        NS_ABORT_MSG_IF(section != "ROUTING_FINGERPRINT",
                        "structured topology expected ROUTING_FINGERPRINT, got " << section);
        config.routingFingerprint = ReadStructuredToken(input, "routing fingerprint");
        NS_ABORT_MSG_IF(
            config.routingFingerprint.size() != 64 ||
                !std::all_of(config.routingFingerprint.begin(),
                             config.routingFingerprint.end(),
                             [](unsigned char character) { return std::isxdigit(character); }),
            "structured topology routing fingerprint must be a 64-character hexadecimal digest");
    }

    ReadStructuredSection(input, "TABLE", count);
    config.tableRouteCount = count;
    std::unique_ptr<UbNextHopIndexBuilder> tableBuilder;
    if (count != 0)
    {
        tableBuilder = std::make_unique<UbNextHopIndexBuilder>(config.nodes.size(), count);
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        UbStructuredTableRoute route{
            ReadStructuredUint32(input, "table current node"),
            ReadStructuredUint32(input, "table destination node"),
            ReadStructuredUint32(input, "table destination port"),
            ReadStructuredPortList(input, "shortest table ports"),
            ReadStructuredPortList(input, "non-shortest table ports"),
        };
        NS_ABORT_MSG_IF(route.currentNodeId >= config.nodes.size() ||
                            route.destinationNodeId >= config.nodes.size(),
                        "structured table route references an unknown node");
        const auto& current = config.nodes[route.currentNodeId];
        const auto& destination = config.nodes[route.destinationNodeId];
        NS_ABORT_MSG_IF(route.destinationPortId >= destination.portCount,
                        "structured table route destination port is outside the node port count");
        auto validatePorts = [&](const std::vector<uint16_t>& ports) {
            for (uint16_t port : ports)
            {
                NS_ABORT_MSG_IF(
                    port >= current.portCount,
                    "structured table route output port is outside the node port count");
            }
        };
        validatePorts(route.shortestPorts);
        validatePorts(route.nonShortestPorts);
        tableBuilder->AddRoute(route);
    }
    if (tableBuilder != nullptr)
    {
        config.tableNextHopIndex = tableBuilder->Build();
    }

    ReadStructuredSection(input, "PORT_GROUPS", count);
    config.portGroups.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        config.portGroups.push_back(UbPortGroup{
            ReadStructuredUint32(input, "port-group node"),
            ReadStructuredUint32(input, "port-group ID"),
            ReadStructuredPortList(input, "port-group ports"),
        });
    }

    ReadStructuredSection(input, "RULES", count);
    config.routingRules.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        config.routingRules.push_back(UbRoutingRule{
            ReadStructuredUint32(input, "rule current node"),
            UbRuleDestination{
                ReadStructuredUint32(input, "rule destination node"),
                ReadStructuredUint32(input, "rule destination port"),
            },
            ReadStructuredUint32List(input, "shortest rule groups"),
            ReadStructuredUint32List(input, "non-shortest rule groups"),
        });
    }

    if (version >= 2)
    {
        ReadStructuredSection(input, "RULE_RANGES", count);
        config.routingRangeRules.reserve(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            config.routingRangeRules.push_back(UbRoutingRangeRule{
                ReadStructuredUint32(input, "range-rule current node"),
                ReadStructuredUint32(input, "range-rule destination start node"),
                ReadStructuredUint32(input, "range-rule destination end node"),
                ReadStructuredUint32(input, "range-rule destination port"),
                ReadStructuredUint32List(input, "shortest range-rule groups"),
                ReadStructuredUint32List(input, "non-shortest range-rule groups"),
            });
        }
    }

    if (version >= 3)
    {
        auto parseRuleType = [](const std::string& value) {
            if (value == "dimension")
            {
                return UbTopologyRuleType::DIMENSION;
            }
            if (value == "direct-switch")
            {
                return UbTopologyRuleType::DIRECT_SWITCH;
            }
            if (value == "two-level-switch")
            {
                return UbTopologyRuleType::TWO_LEVEL_SWITCH;
            }
            if (value == "dragonfly")
            {
                return UbTopologyRuleType::DRAGONFLY;
            }
            NS_ABORT_MSG("unknown topology rule type: " << value);
            return UbTopologyRuleType::DIMENSION;
        };
        auto parseConnection = [](const std::string& value) {
            if (value == "line")
            {
                return UbDimensionConnection::LINE;
            }
            if (value == "ring")
            {
                return UbDimensionConnection::RING;
            }
            if (value == "fullMesh")
            {
                return UbDimensionConnection::FULL_MESH;
            }
            NS_ABORT_MSG("unknown dimension connection: " << value);
            return UbDimensionConnection::LINE;
        };

        ReadStructuredSection(input, "TOPOLOGY_RULES", count);
        config.topologyRules.reserve(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            const UbTopologyRuleType type =
                parseRuleType(ReadStructuredToken(input, "topology rule type"));
            std::vector<uint32_t> shape = ReadStructuredUint32List(input, "topology rule shape");
            const auto connectionTokens =
                ReadStructuredTokenList(input, "topology rule connections");
            std::vector<UbDimensionConnection> connections;
            connections.reserve(connectionTokens.size());
            std::transform(connectionTokens.begin(),
                           connectionTokens.end(),
                           std::back_inserter(connections),
                           parseConnection);
            const uint32_t nodeCount = ReadStructuredUint32(input, "topology rule node count");
            std::vector<UbTopologyRuleNode> nodes;
            nodes.reserve(nodeCount);
            for (uint32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const uint32_t nodeId = ReadStructuredUint32(input, "topology rule node ID");
                const std::string nodeType = ReadStructuredToken(input, "topology rule node type");
                NS_ABORT_MSG_IF(nodeType != "HOST" && nodeType != "SWITCH",
                                "unknown topology rule node type: " << nodeType);
                nodes.push_back(UbTopologyRuleNode{
                    nodeId,
                    nodeType == "HOST",
                    ReadStructuredUint32List(input, "topology rule coordinates"),
                });
            }
            config.topologyRules.push_back(UbTopologyRule{
                type,
                std::move(shape),
                std::move(connections),
                std::move(nodes),
            });
        }
    }

    const std::string end = ReadStructuredToken(input, "protocol terminator");
    NS_ABORT_MSG_IF(end != "END", "structured topology expected END, got " << end);
    std::string trailing;
    NS_ABORT_MSG_IF(input >> trailing, "structured topology contains data after END: " << trailing);
    return config;
}

// 读取拓扑文件
void
UbUtils::CreateTopo(const string& filename)
{
    (void)CreateTopo(filename, Time(0), 1);
}

UbUtils::LinkDelayOffsetStats
UbUtils::CreateTopo(const string& filename, Time offsetWindow, uint32_t offsetSeed)
{
    PrintTimestamp("[setup] Load " + DisplayFilename(filename));
    ifstream file(filename);
    if (!file.is_open())
        NS_ASSERT_MSG(0, "Can not open File: " << filename);
    string line;
    getline(file, line);
    std::vector<UbStructuredLinkConfig> links;
    // node1,port1,node2,port2,bandwidth,delay 0,0,2,0,400Gbps,10ns
    while (getline(file, line))
    {
        stringstream ss(line);
        // 跳过空行、#开头行、纯空格行
        if (line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == string::npos) {
            continue;
        }
        string cell;
        uint32_t node1;
        uint32_t port1;
        uint32_t node2;
        uint32_t port2;
        string delay;
        string bandwidth;
        getline(ss, cell, ',');
        node1 = static_cast<uint32_t>(stoi(TrimFieldCopy(cell)));
        getline(ss, cell, ',');
        port1 = static_cast<uint32_t>(stoi(TrimFieldCopy(cell)));
        getline(ss, cell, ',');
        node2 = static_cast<uint32_t>(stoi(TrimFieldCopy(cell)));
        getline(ss, cell, ',');
        port2 = static_cast<uint32_t>(stoi(TrimFieldCopy(cell)));
        getline(ss, cell, ',');
        bandwidth = TrimFieldCopy(cell);
        getline(ss, cell, ',');
        delay = TrimFieldCopy(cell);
        links.push_back(UbStructuredLinkConfig{node1, port1, node2, port2, bandwidth, delay});
    }
    file.close();
    return CreateTopo(links, offsetWindow, offsetSeed);
}

UbUtils::LinkDelayOffsetStats
UbUtils::CreateTopo(const std::vector<UbStructuredLinkConfig>& links,
                    Time offsetWindow,
                    uint32_t offsetSeed)
{
    NS_ABORT_MSG_IF(offsetWindow.IsStrictlyNegative(),
                    "link delay offset window must be non-negative");
    LinkDelayOffsetStats offsetStats;
    std::unordered_map<int64_t, uint64_t> slotOccupancy;
    std::set<std::pair<uint32_t, uint32_t>> usedEndpoints;

    for (const auto& link : links)
    {
        NS_ABORT_MSG_IF(link.node1 >= NodeList::GetNNodes() || link.node2 >= NodeList::GetNNodes(),
                        "structured topology link references an unknown node");
        Ptr<Node> n1 = NodeList::GetNode(link.node1);
        Ptr<Node> n2 = NodeList::GetNode(link.node2);
        NS_ABORT_MSG_IF(link.port1 >= n1->GetNDevices() || link.port2 >= n2->GetNDevices(),
                        "structured topology link references a port outside the node port count");
        NS_ABORT_MSG_IF(!usedEndpoints.emplace(link.node1, link.port1).second ||
                            !usedEndpoints.emplace(link.node2, link.port2).second,
                        "structured topology physical port is connected more than once");

        Ptr<UbPort> p1 = DynamicCast<UbPort>(n1->GetDevice(link.port1));
        Ptr<UbPort> p2 = DynamicCast<UbPort>(n2->GetDevice(link.port2));
        NS_ABORT_MSG_IF(p1 == nullptr || p2 == nullptr,
                        "structured topology link endpoint is not a unified-bus port");
        p1->SetDataRate(DataRate(link.bandwidth));
        p2->SetDataRate(DataRate(link.bandwidth));
        const Time baseDelay(link.delay);
        const Time effectiveDelay = ResolveLinkDelayWithOffset(baseDelay,
                                                               offsetWindow,
                                                               offsetSeed,
                                                               link.node1,
                                                               link.port1,
                                                               link.node2,
                                                               link.port2);
        if (baseDelay.IsZero())
        {
            ++offsetStats.zeroDelayLinkCount;
        }
        else
        {
            ++offsetStats.positiveLinkCount;
            if (offsetWindow.IsStrictlyPositive())
            {
                const int64_t slot = (effectiveDelay - baseDelay).GetTimeStep();
                offsetStats.maxLinksPerOffset =
                    std::max(offsetStats.maxLinksPerOffset, ++slotOccupancy[slot]);
            }
        }
        CreateUbChannelBetween(p1, p2, effectiveDelay);
    }

    for (auto it = NodeList::Begin(); it != NodeList::End(); ++it) {
        Ptr<Node> node = *it;
        Ptr<UbCongestionControl> congestionCtrl = node->GetObject<ns3::UbSwitch>()->GetCongestionCtrl();
        if (congestionCtrl->GetCongestionAlgo() == CAQM) {
            Ptr<UbSwitchCaqm> swCaqm = DynamicCast<UbSwitchCaqm>(congestionCtrl);
            swCaqm->ResetLocalCc();
        }
    }
    if (offsetWindow.IsStrictlyPositive())
    {
        offsetStats.distinctOffsetCount = slotOccupancy.size();
        offsetStats.offsetReuseCount =
            offsetStats.positiveLinkCount - offsetStats.distinctOffsetCount;
    }
    return offsetStats;
}

// 解析节点范围（如 "1..4"）
inline void UbUtils::ParseNodeRange(const string &rangeStr, NodeEle nodeEle)
{
    size_t dotPos = rangeStr.find("..");
    if (dotPos != string::npos) {
        // 处理范围格式
        uint32_t start = stoi(rangeStr.substr(0, dotPos));
        uint32_t end = stoi(rangeStr.substr(dotPos + 2));
        for (auto i = start; i <= end; ++i) {
            nodeEle_map[i]=nodeEle;
        }
    } else {
        // 处理单个节点
        nodeEle_map[stoi(rangeStr)] = nodeEle;
    }
}

// 创建node
void UbUtils::CreateNode(const string &filename)
{
    PrintTimestamp("[setup] Load " + DisplayFilename(filename));
    nodeEle_map.clear();
    ifstream file(filename);
    if (!file.is_open()) {
        NS_ASSERT_MSG(0, "Can not open File: " << filename);
    }
    string line;
    NS_ABORT_MSG_IF(!getline(file, line), "node.csv must include a header");
    const std::vector<std::string> header = SplitCsvRow(line);
    const std::map<std::string, std::size_t> headerIndex = BuildCsvHeaderIndex(header);
    const bool legacyForwardDelayIsAllocationDelay = IsLegacyNodeForwardDelayHeader(header);
    while (getline(file, line)) {
        // 跳过空行、#开头行、纯空格行
        if (line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == string::npos) {
            continue;
        }
        const std::vector<std::string> fields = SplitCsvRow(line);

        NodeEle nodeEle = {};
        nodeEle.nodeIdStr = GetRequiredCsvField(fields, headerIndex, "nodeId", filename);
        nodeEle.nodeTypeStr = GetRequiredCsvField(fields, headerIndex, "nodeType", filename);
        nodeEle.portNumStr = GetRequiredCsvField(fields, headerIndex, "portNum", filename);
        if (legacyForwardDelayIsAllocationDelay)
        {
            nodeEle.forwardDelay = "";
            nodeEle.allocationDelay = GetOptionalCsvField(fields, headerIndex, "forwardDelay");
        }
        else
        {
            nodeEle.forwardDelay = GetOptionalCsvField(fields, headerIndex, "forwardDelay");
            nodeEle.allocationDelay = GetOptionalCsvField(fields, headerIndex, "allocationDelay");
        }
        nodeEle.systemIdStr = GetOptionalCsvField(fields, headerIndex, "systemId");

        // 解析节点ID（范围 or 单个节点）
        ParseNodeRange(nodeEle.nodeIdStr, nodeEle);
    }
    file.close();

    std::vector<UbStructuredNodeConfig> nodes;
    nodes.reserve(nodeEle_map.size());
    for (const auto& [nodeId, nodeEle] : nodeEle_map)
    {
        nodes.push_back(UbStructuredNodeConfig{
            nodeId,
            nodeEle.nodeTypeStr,
            static_cast<uint32_t>(stoul(nodeEle.portNumStr)),
            nodeEle.allocationDelay,
            nodeEle.forwardDelay,
            nodeEle.systemIdStr.empty() ? 0 : static_cast<uint32_t>(stoul(nodeEle.systemIdStr)),
        });
    }
    CreateNode(nodes);
}

void
UbUtils::CreateNode(const std::vector<UbStructuredNodeConfig>& nodes)
{
    const uint32_t firstNodeId = NodeList::GetNNodes();
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        const auto& nodeConfig = nodes[index];
        NS_ABORT_MSG_IF(nodeConfig.nodeId != firstNodeId + index,
                        "node IDs must be consecutive and continue from the current NodeList");

        Ptr<Node> node = CreateObject<Node>(nodeConfig.systemId);
        Ptr<UbSwitch> sw = CreateObject<UbSwitch>();
        node->AggregateObject(sw);
        Ptr<ns3::UbLdstInstance> ldst = CreateObject<UbLdstInstance>();
        node->AggregateObject(ldst);
        ldst->Init(node->GetId());
        if (nodeConfig.nodeType == "DEVICE")
        {
            Ptr<UbController> ubCtrl = CreateObject<UbController>();
            node->AggregateObject(ubCtrl);
            ubCtrl->CreateUbFunction();
            ubCtrl->CreateUbTransaction();
            sw->SetNodeType(UB_DEVICE);
        }
        else if (nodeConfig.nodeType == "SWITCH")
        {
            sw->SetNodeType(UB_SWITCH);
        }
        else
        {
            NS_ABORT_MSG("unsupported structured topology node type: " << nodeConfig.nodeType);
        }
        for (uint32_t portId = 0; portId < nodeConfig.portCount; ++portId)
        {
            Ptr<UbPort> port = CreateObject<UbPort>();
            port->SetAddress(Mac48Address::Allocate());
            node->AddDevice(port);
        }
        sw->Init();
        auto cc = UbCongestionControl::Create(UB_SWITCH);
        cc->OnSwitchAttached(sw);
        if (!nodeConfig.forwardDelay.empty())
        {
            sw->SetAttribute("InPortProcessingDelay", StringValue(nodeConfig.forwardDelay));
        }
        if (!nodeConfig.allocationDelay.empty())
        {
            auto allocator = sw->GetAllocator();
            allocator->SetAttribute("AllocationTime", StringValue(nodeConfig.allocationDelay));
        }
    }
}

// 读取路由
void UbUtils::AddRoutingTable(const string &filename)
{
    PrintTimestamp("[setup] Load " + DisplayFilename(filename));
    // node_id,dest,outport,metric
    std::ifstream file(filename);
    if (!file.is_open()) {
        NS_ASSERT_MSG(0, "Can not open File: " << filename);
    }

    uint32_t nodeStart;
    uint32_t nodeEnd;
    uint32_t destStart;
    uint32_t destEnd;
    uint32_t destport;
    uint32_t outport;
    uint32_t metric;
    uint32_t rowNumber = 1;
    std::string line;
    std::vector<uint16_t> outports;
    std::vector<uint32_t> metrics;
    std::vector<RouteRangeCsvRow> rangeRows;
    using MetricPorts = std::map<uint32_t, std::vector<uint16_t>>;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, MetricPorts> exactRows;
    std::map<std::pair<uint32_t, uint32_t>, MetricPorts> primaryRows;

    auto exactBuilder = std::make_unique<UbNextHopIndexBuilder>(NodeList::GetNNodes());
    getline(file, line);
    while (std::getline(file, line)) {
        ++rowNumber;
        std::stringstream ss(line);

        // 跳过空行、#开头行、纯空格行
        if (line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == string::npos) {
            continue;
        }
        std::string cell;
        std::getline(ss, cell, ',');
        std::tie(nodeStart, nodeEnd) = ParseUint32RangeField(cell);
        std::getline(ss, cell, ',');
        std::tie(destStart, destEnd) = ParseUint32RangeField(cell);
        std::getline(ss, cell, ',');
        destport = static_cast<uint32_t>(std::stoi(cell));
        std::getline(ss, cell, ',');
        // read outports
        std::stringstream sOutports(cell);
        outports.clear();
        while (sOutports >> outport) {
            outports.push_back(outport);
        }
        std::getline(ss, cell, ',');
        std::stringstream sMetrics(cell);
        metrics.clear();
        while (sMetrics >> metric) {
            metrics.push_back(metric);
        }
        const auto routeValue = NormalizeRouteValue(outports, metrics, rowNumber);
        const bool isRangeRoute = nodeStart != nodeEnd || destStart != destEnd;
        if (!isRangeRoute) {
            for (const auto& [port, routeMetric] : routeValue) {
                exactRows[{nodeStart, destStart, destport}][routeMetric].push_back(port);
                primaryRows[{nodeStart, destStart}][routeMetric].push_back(port);
            }
            continue;
        }

        rangeRows.push_back(RouteRangeCsvRow{rowNumber,
                                             nodeStart,
                                             nodeEnd,
                                             destStart,
                                             destEnd,
                                             destport,
                                             routeValue});
    }
    ValidateRouteRangeRowsNoConflicts(rangeRows);

    auto classifyMetrics = [](const MetricPorts& metricPorts) {
        UbStructuredTableRoute route{};
        bool shortest = true;
        for (const auto& [metric, ports] : metricPorts)
        {
            auto& destination = shortest ? route.shortestPorts : route.nonShortestPorts;
            destination.insert(destination.end(), ports.begin(), ports.end());
            shortest = false;
        }
        return route;
    };
    for (const auto& [lookup, metricPorts] : exactRows)
    {
        auto route = classifyMetrics(metricPorts);
        std::tie(route.currentNodeId, route.destinationNodeId, route.destinationPortId) = lookup;
        exactBuilder->AddRoute(route);
    }
    for (const auto& [lookup, metricPorts] : primaryRows)
    {
        const auto route = classifyMetrics(metricPorts);
        exactBuilder->AddPrimaryRoute(lookup.first,
                                      lookup.second,
                                      route.shortestPorts,
                                      route.nonShortestPorts);
    }

    // The runtime keeps only the minimum-metric ports and one combined non-shortest group.
    // TODO: Preserve numeric metrics if a future routing policy needs more than these two groups.
    for (const auto& row : rangeRows) {
        const uint32_t nodeStart = row.nodeStart;
        const uint32_t nodeEnd = row.nodeEnd;
        const uint32_t destStart = row.destStart;
        const uint32_t destEnd = row.destEnd;
        const uint32_t destport = row.destPort;
        for (uint32_t nodeId = nodeStart;; ++nodeId) {
            auto rt = NodeList::GetNode(nodeId)->GetObject<ns3::UbSwitch>()->GetRoutingProcess();
            const auto minMetricIt =
                std::min_element(row.value.begin(),
                                 row.value.end(),
                                 [](const auto& left, const auto& right) {
                                     return left.second < right.second;
                                 });
            NS_ASSERT_MSG(minMetricIt != row.value.end(), "compressed route row must contain metrics");
            const uint32_t shortestMetric = minMetricIt->second;
            for (const auto& [outport, metric] : row.value) {
                std::vector<uint16_t> outPortGroup = {outport};
                if (metric == shortestMetric) {
                    rt->GetRoutingTable().AddShortestRouteRange(destStart, destEnd, destport, outPortGroup);
                } else {
                    rt->GetRoutingTable().AddOtherRouteRange(destStart, destEnd, destport, outPortGroup);
                }
            }
            if (nodeId == nodeEnd) {
                break;
            }
        }
    }
    if (!exactRows.empty()) {
        const auto nextHopIndex = exactBuilder->Build();
        for (uint32_t nodeId = 0; nodeId < NodeList::GetNNodes(); ++nodeId) {
            auto routing = NodeList::GetNode(nodeId)->GetObject<UbSwitch>()->GetRoutingProcess();
            routing->SetNextHopIndex(nextHopIndex);
        }
    }
    file.close();
}

void
UbUtils::AddRoutingTable(const std::vector<UbStructuredTableRoute>& routes)
{
    if (routes.empty())
    {
        return;
    }
    for (const auto& route : routes)
    {
        NS_ABORT_MSG_IF(route.currentNodeId >= NodeList::GetNNodes() ||
                            route.destinationNodeId >= NodeList::GetNNodes(),
                        "structured table route references an unknown node");
        Ptr<Node> current = NodeList::GetNode(route.currentNodeId);
        Ptr<Node> destination = NodeList::GetNode(route.destinationNodeId);
        NS_ABORT_MSG_IF(route.destinationPortId >= destination->GetNDevices(),
                        "structured table route destination port is outside the node port count");
        NS_ABORT_MSG_IF(
            route.destinationPortId >= BYTE_RANGE - 1,
            "structured table route destination port cannot be encoded in the UB address");
        NS_ABORT_MSG_IF(route.shortestPorts.empty() && route.nonShortestPorts.empty(),
                        "structured table route must contain at least one output port");

        auto validatePorts = [&](const std::vector<uint16_t>& ports) {
            for (uint16_t port : ports)
            {
                NS_ABORT_MSG_IF(
                    port >= current->GetNDevices(),
                    "structured table route output port is outside the node port count");
            }
        };
        validatePorts(route.shortestPorts);
        validatePorts(route.nonShortestPorts);
    }

    const auto nextHopIndex = ns3::UbNextHopIndex::Build(routes, NodeList::GetNNodes());
    for (uint32_t nodeId = 0; nodeId < NodeList::GetNNodes(); ++nodeId)
    {
        auto routing = NodeList::GetNode(nodeId)->GetObject<UbSwitch>()->GetRoutingProcess();
        routing->SetNextHopIndex(nextHopIndex);
    }
}

void
UbUtils::AddRuleRoutingTable(const std::string& portGroupFilename,
                             const std::string& ruleFilename,
                             const std::string& rangeRuleFilename)
{
    PrintTimestamp("[setup] Load " + DisplayFilename(portGroupFilename));
    std::ifstream portGroupFile(portGroupFilename);
    NS_ABORT_MSG_IF(!portGroupFile.is_open(), "Can not open File: " << portGroupFilename);

    auto parseValues = [](const std::string& field) {
        std::vector<uint32_t> values;
        std::stringstream stream(field);
        uint32_t value;
        while (stream >> value)
        {
            values.push_back(value);
        }
        return values;
    };

    std::vector<UbPortGroup> portGroups;
    std::string line;
    std::getline(portGroupFile, line);
    while (std::getline(portGroupFile, line))
    {
        if (line.empty())
        {
            continue;
        }
        std::stringstream row(line);
        std::string nodeField;
        std::string groupField;
        std::string portsField;
        std::getline(row, nodeField, ',');
        std::getline(row, groupField, ',');
        std::getline(row, portsField, ',');
        const auto portValues = parseValues(portsField);
        std::vector<uint16_t> ports;
        ports.reserve(portValues.size());
        for (uint32_t port : portValues)
        {
            NS_ABORT_MSG_IF(port > std::numeric_limits<uint16_t>::max(),
                            "routing port group contains an invalid port");
            ports.push_back(static_cast<uint16_t>(port));
        }
        portGroups.push_back(UbPortGroup{
            static_cast<uint32_t>(std::stoul(nodeField)),
            static_cast<uint32_t>(std::stoul(groupField)),
            std::move(ports),
        });
    }

    PrintTimestamp("[setup] Load " + DisplayFilename(ruleFilename));
    std::ifstream ruleFile(ruleFilename);
    NS_ABORT_MSG_IF(!ruleFile.is_open(), "Can not open File: " << ruleFilename);
    std::vector<UbRoutingRule> routingRules;
    std::getline(ruleFile, line);
    while (std::getline(ruleFile, line))
    {
        if (line.empty())
        {
            continue;
        }
        std::stringstream row(line);
        std::array<std::string, 5> fields;
        for (size_t index = 0; index < fields.size(); ++index)
        {
            std::getline(row, fields[index], ',');
        }
        routingRules.push_back(UbRoutingRule{
            static_cast<uint32_t>(std::stoul(fields[0])),
            UbRuleDestination{
                static_cast<uint32_t>(std::stoul(fields[1])),
                static_cast<uint32_t>(std::stoul(fields[2])),
            },
            parseValues(fields[3]),
            parseValues(fields[4]),
        });
    }

    std::vector<UbRoutingRangeRule> routingRangeRules;
    if (!rangeRuleFilename.empty())
    {
        PrintTimestamp("[setup] Load " + DisplayFilename(rangeRuleFilename));
        std::ifstream rangeRuleFile(rangeRuleFilename);
        NS_ABORT_MSG_IF(!rangeRuleFile.is_open(), "Can not open File: " << rangeRuleFilename);
        std::getline(rangeRuleFile, line);
        while (std::getline(rangeRuleFile, line))
        {
            if (line.empty())
            {
                continue;
            }
            std::stringstream row(line);
            std::array<std::string, 6> fields;
            for (size_t index = 0; index < fields.size(); ++index)
            {
                std::getline(row, fields[index], ',');
            }
            routingRangeRules.push_back(UbRoutingRangeRule{
                static_cast<uint32_t>(std::stoul(fields[0])),
                static_cast<uint32_t>(std::stoul(fields[1])),
                static_cast<uint32_t>(std::stoul(fields[2])),
                static_cast<uint32_t>(std::stoul(fields[3])),
                parseValues(fields[4]),
                parseValues(fields[5]),
            });
        }
    }
    AddRuleRoutingTable(portGroups, routingRules, routingRangeRules);
}

void
UbUtils::AddRuleRoutingTable(const std::vector<UbPortGroup>& portGroups,
                             const std::vector<UbRoutingRule>& routingRules,
                             const std::vector<UbRoutingRangeRule>& routingRangeRules,
                             const std::vector<UbTopologyLink>& topologyLinks,
                             const std::vector<UbTopologyRule>& topologyRules)
{
    if (portGroups.empty() && routingRules.empty() && routingRangeRules.empty() &&
        topologyRules.empty())
    {
        return;
    }

    auto ruleTable = std::make_shared<UbRuleRoutingTable>();
    for (const auto& portGroup : portGroups)
    {
        NS_ABORT_MSG_IF(portGroup.nodeId >= NodeList::GetNNodes(),
                        "structured port group references an unknown node");
        const uint32_t portCount = NodeList::GetNode(portGroup.nodeId)->GetNDevices();
        for (uint16_t port : portGroup.ports)
        {
            NS_ABORT_MSG_IF(port >= portCount,
                            "structured port group contains a port outside the node port count");
        }
        ruleTable->AddPortGroup(portGroup);
    }
    for (const auto& rule : routingRules)
    {
        NS_ABORT_MSG_IF(rule.currentNodeId >= NodeList::GetNNodes() ||
                            rule.destination.nodeId >= NodeList::GetNNodes(),
                        "structured routing rule references an unknown node");
        if (rule.destination.portId != UbRuleDestination::ANY_PORT)
        {
            NS_ABORT_MSG_IF(
                rule.destination.portId >=
                    NodeList::GetNode(rule.destination.nodeId)->GetNDevices(),
                "structured routing rule destination port is outside the node port count");
            NS_ABORT_MSG_IF(
                rule.destination.portId >= BYTE_RANGE - 1,
                "structured routing rule destination port cannot be encoded in the UB address");
        }
        ruleTable->AddRule(rule);
    }
    for (const auto& rule : routingRangeRules)
    {
        NS_ABORT_MSG_IF(rule.currentNodeId >= NodeList::GetNNodes() ||
                            rule.destinationStartNodeId > rule.destinationEndNodeId ||
                            rule.destinationEndNodeId >= NodeList::GetNNodes(),
                        "structured routing range references an invalid node range");
        if (rule.destinationPortId != UbRuleDestination::ANY_PORT)
        {
            NS_ABORT_MSG_IF(
                rule.destinationPortId >= BYTE_RANGE - 1,
                "structured routing range destination port cannot be encoded in the UB address");
            for (uint32_t destinationNodeId = rule.destinationStartNodeId;
                 destinationNodeId <= rule.destinationEndNodeId;
                 ++destinationNodeId)
            {
                NS_ABORT_MSG_IF(
                    rule.destinationPortId >= NodeList::GetNode(destinationNodeId)->GetNDevices(),
                    "structured routing range destination port is outside a node port count");
            }
        }
        ruleTable->AddRangeRule(rule);
    }
    for (const auto& link : topologyLinks)
    {
        NS_ABORT_MSG_IF(link.node1 >= NodeList::GetNNodes() || link.node2 >= NodeList::GetNNodes(),
                        "topology routing link references an unknown node");
        NS_ABORT_MSG_IF(link.port1 >= NodeList::GetNode(link.node1)->GetNDevices() ||
                            link.port2 >= NodeList::GetNode(link.node2)->GetNDevices(),
                        "topology routing link references an invalid port");
        ruleTable->AddTopologyLink(link);
    }
    for (const auto& rule : topologyRules)
    {
        for (const auto& node : rule.nodes)
        {
            NS_ABORT_MSG_IF(node.nodeId >= NodeList::GetNNodes(),
                            "topology rule references an unknown node");
            const bool isHost =
                NodeList::GetNode(node.nodeId)->GetObject<UbSwitch>()->GetNodeType() == UB_DEVICE;
            NS_ABORT_MSG_IF(isHost != node.isHost,
                            "topology rule node type differs from the installed node");
        }
        ruleTable->AddTopologyRule(rule);
    }
    ruleTable->PrepareForLookup();

    for (uint32_t nodeId = 0; nodeId < NodeList::GetNNodes(); ++nodeId)
    {
        auto routing = NodeList::GetNode(nodeId)->GetObject<UbSwitch>()->GetRoutingProcess();
        routing->SetRuleRoutingTable(ruleTable);
    }
}

UbUtils::LinkDelayOffsetStats
UbUtils::InstallStructuredTopology(const UbStructuredTopologyConfig& config,
                                   Time offsetWindow,
                                   uint32_t offsetSeed)
{
    NS_ABORT_MSG_IF(NodeList::GetNNodes() != 0,
                    "structured topology installation requires an empty NodeList");
    NS_ABORT_MSG_IF(config.nodes.empty(), "structured topology must contain at least one node");
    PrintTimestamp("[setup] Install structured topology and routing");
    CreateNode(config.nodes);
    const LinkDelayOffsetStats offsetStats = CreateTopo(config.links, offsetWindow, offsetSeed);
    if (config.tableNextHopIndex != nullptr)
    {
        for (uint32_t nodeId = 0; nodeId < NodeList::GetNNodes(); ++nodeId)
        {
            auto routing = NodeList::GetNode(nodeId)->GetObject<UbSwitch>()->GetRoutingProcess();
            routing->SetNextHopIndex(config.tableNextHopIndex);
        }
    }
    else
    {
        AddRoutingTable(config.tableRoutes);
    }
    std::vector<UbTopologyLink> topologyLinks;
    if (!config.topologyRules.empty())
    {
        topologyLinks.reserve(config.links.size());
        for (const auto& link : config.links)
        {
            NS_ABORT_MSG_IF(link.port1 > std::numeric_limits<uint16_t>::max() ||
                                link.port2 > std::numeric_limits<uint16_t>::max(),
                            "topology routing cannot encode a physical port above uint16 range");
            topologyLinks.push_back(UbTopologyLink{
                link.node1,
                static_cast<uint16_t>(link.port1),
                link.node2,
                static_cast<uint16_t>(link.port2),
            });
        }
    }
    if (!config.portGroups.empty() || !config.routingRules.empty() ||
        !config.routingRangeRules.empty() || !config.topologyRules.empty())
    {
        AddRuleRoutingTable(config.portGroups,
                            config.routingRules,
                            config.routingRangeRules,
                            topologyLinks,
                            config.topologyRules);
    }
    return offsetStats;
}

// 读取TP配置文件
void UbUtils::ParseLine(const std::string &line, Connection &conn)
{
    std::stringstream ss(line);
    std::string item;

    // 读取node1
    getline(ss, item, ',');
    conn.node1 = stoi(item);

    // 读取port1
    getline(ss, item, ',');
    conn.port1 = stoi(item);

    // 读取tpn1
    getline(ss, item, ',');
    conn.tpn1 = stoi(item);

    // 读取node2
    getline(ss, item, ',');
    conn.node2 = stoi(item);

    // 读取port2
    getline(ss, item, ',');
    conn.port2 = stoi(item);

    // 读取tpn2
    getline(ss, item, ',');
    conn.tpn2 = stoi(item);

    // 读取priority
    getline(ss, item, ',');
    conn.priority = stoi(item);

    // 读取metrics
    getline(ss, item, ',');
    if (!item.empty()) {
        conn.metrics = stoi(item);
    } else {
        conn.metrics = UINT32_MAX;
    }
}

void UbUtils::CreateTp(const string &filename)
{
    std::unordered_map<uint32_t, std::vector<uint32_t>> NodeTpns;
    // key1:node1 key2:node2 value:Connection
    ifstream file(filename);
    if (!file.is_open()) { // 没有TP文件则使用实时创建TP模式
        PrintTimestamp("[setup] Skip " + DisplayFilename(filename) +
                       " (not found; traffic will reserve TP connections before endpoints "
                       "materialize on demand).");
        return ;
    }
    PrintTimestamp("[setup] Load " + DisplayFilename(filename));
    string line;
    // 跳过标题行
    getline(file, line);

    while (getline(file, line)) {
        // 跳过空行、#开头行、纯空格行
        if (line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == string::npos) {
            continue;
        }
        Connection conn;
        ParseLine(line, conn);
        Ptr<Node> sendNode = NodeList::GetNode(conn.node1);
        Ptr<Node> recvNode = NodeList::GetNode(conn.node2);
        auto sendCtrl = sendNode->GetObject<UbController>();
        auto recvCtrl = recvNode->GetObject<UbController>();
        sendCtrl->GetTpConnManager()->AddUnilateralConnection(conn, conn.node1);
        recvCtrl->GetTpConnManager()->AddUnilateralConnection(conn, conn.node2);

        PreloadLocalTpIfOwned(sendNode,
                              conn.node1,
                              conn.node2,
                              conn.port1,
                              conn.port2,
                              conn.priority,
                              conn.tpn1,
                              conn.tpn2);
        PreloadLocalTpIfOwned(recvNode,
                              conn.node2,
                              conn.node1,
                              conn.port2,
                              conn.port1,
                              conn.priority,
                              conn.tpn2,
                              conn.tpn1);
    }
    file.close();
    return ;
}

void UbUtils::SetRecord(int fieldCount, string field, TrafficRecord &record)
{
    switch (static_cast<FIELDCOUNT>(fieldCount)) {
        case FIELDCOUNT::TASKID:
            record.taskId = stoi(field);
            break;
        case FIELDCOUNT::SOURCENODE:
            record.sourceNode = stoi(field);
            break;
        case FIELDCOUNT::DESTNODE:
            record.destNode = stoi(field);
            break;
        case FIELDCOUNT::DATASIZE:
            record.dataSize = stoi(field);
            break;
        case FIELDCOUNT::OPTYPE:
            record.opType = field;
            break;
        case FIELDCOUNT::PRIORITY:
            record.priority = static_cast<int>(ParseTrafficPriorityField(field));
            break;
        case FIELDCOUNT::DELAY:
            record.delay = field;
            break;
        case FIELDCOUNT::PHASEID:
            record.phaseId = stoi(field);
            break;
        case FIELDCOUNT::DEPENDONPHASES: {
            if (!field.empty()) {
                stringstream depStream(field);
                string dep;
                while (depStream >> dep) {
                    record.dependOnPhases.push_back(stoi(dep));
                }
            }
            break;
        }
        case FIELDCOUNT::SRCPORTID:
            record.hasSrcPortId = !field.empty();
            record.srcPortId = record.hasSrcPortId ? static_cast<uint32_t>(std::stoul(field)) : 0;
            break;
        case FIELDCOUNT::DSTPORTID:
            record.hasDstPortId = !field.empty();
            record.dstPortId = record.hasDstPortId ? static_cast<uint32_t>(std::stoul(field)) : 0;
            break;
    }
}

vector<TrafficRecord> UbUtils::LoadTrafficConfig(const string &filename)
{
    vector<TrafficRecord> records;
    ForEachTrafficRecordInternal(filename, "Load", [&](const TrafficRecord& record) {
        UbTrafficGen::Get()->SetPhaseDepend(record.phaseId, record.taskId);
        records.push_back(record);
    });
    return records;
}

void
UbUtils::ForEachTrafficRecord(const string& filename,
                              const std::function<void(const TrafficRecord&)>& callback)
{
    ForEachTrafficRecordInternal(filename, "Load", callback);
}

void
UbUtils::ForEachTrafficRecordView(const string& filename,
                                  const std::function<void(const TrafficRecordView&)>& callback)
{
    ForEachTrafficRecordViewInternal(filename, "Load", callback);
}

void
UbUtils::RegisterTrafficPhaseDependencies(const string& filename)
{
    (void)RegisterTrafficPhaseDependenciesAndGetStats(filename);
}

UbUtils::TrafficLoadStats
UbUtils::RegisterTrafficPhaseDependenciesAndGetStats(const string& filename)
{
    TrafficLoadStats stats;
    ForEachTrafficPhaseIndexRecord(filename, [&](uint32_t taskId, uint32_t phaseId) {
        UbTrafficGen::Get()->RegisterPhaseTaskDuringInitialLoad(phaseId);
        ++stats.recordCount;
        stats.maxTaskId = std::max<uint32_t>(stats.maxTaskId, taskId);
    });
    return stats;
}

void
UbUtils::ForEachTrafficPhaseIndexRecord(const string& filename,
                                        const std::function<void(uint32_t, uint32_t)>& callback)
{
    PrintTimestamp("[traffic] Index phase dependencies from " + DisplayFilename(filename));
    ifstream file(filename);
    if (!file.is_open()) {
        NS_ASSERT_MSG(0, "Can not open File: " << filename);
        return;
    }

    string line;
    NS_ABORT_MSG_IF(!getline(file, line), "traffic.csv is missing its header");
    const std::vector<std::string> header = SplitCsvRow(line);
    ValidateTrafficCsvHeader(header);
    const std::size_t expectedFieldCount = header.size();
    while (getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == string::npos) {
            continue;
        }

        ValidateTrafficCsvRowWidth(line, expectedFieldCount);

        uint32_t taskId = 0;
        uint32_t phaseId = 0;
        int fieldCount = 0;
        size_t fieldStart = 0;
        bool hasTaskId = false;
        bool hasPhaseId = false;

        while (fieldStart <= line.size()) {
            size_t fieldEnd = line.find(',', fieldStart);
            if (fieldEnd == string::npos) {
                fieldEnd = line.size();
            }

            if (fieldCount == static_cast<int>(FIELDCOUNT::TASKID) ||
                fieldCount == static_cast<int>(FIELDCOUNT::PHASEID)) {
                size_t valueStart = fieldStart;
                while (valueStart < fieldEnd &&
                       (line[valueStart] == ' ' || line[valueStart] == '\t')) {
                    ++valueStart;
                }
                size_t valueEnd = fieldEnd;
                while (valueEnd > valueStart &&
                       (line[valueEnd - 1] == ' ' || line[valueEnd - 1] == '\t')) {
                    --valueEnd;
                }
                if (valueStart < valueEnd) {
                    const uint32_t value = ParseUint32Field(line, valueStart, valueEnd);
                    if (fieldCount == static_cast<int>(FIELDCOUNT::TASKID)) {
                        taskId = value;
                        hasTaskId = true;
                    } else {
                        phaseId = value;
                        hasPhaseId = true;
                    }
                }
            }

            if (hasTaskId && hasPhaseId) {
                break;
            }
            if (fieldEnd == line.size()) {
                break;
            }
            fieldStart = fieldEnd + 1;
            ++fieldCount;
        }

        if (hasTaskId && hasPhaseId) {
            callback(taskId, phaseId);
        }
    }
}

void
UbUtils::ForEachTrafficRecordInternal(const string& filename,
                                      const string& action,
                                      const std::function<void(const TrafficRecord&)>& callback)
{
    PrintTimestamp("[traffic] " + action + " " + DisplayFilename(filename));
    ifstream file(filename);
    if (!file.is_open()) {
        NS_ASSERT_MSG(0, "Can not open File: " << filename);
        return;
    }
    string line;
    NS_ABORT_MSG_IF(!getline(file, line), "traffic.csv is missing its header");
    const std::vector<std::string> header = SplitCsvRow(line);
    ValidateTrafficCsvHeader(header);
    const std::size_t expectedFieldCount = header.size();
    TrafficRecord record;
    while (getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == string::npos) {
            continue;
        }

        ValidateTrafficCsvRowWidth(line, expectedFieldCount);

        record.taskId = 0;
        record.sourceNode = 0;
        record.destNode = 0;
        record.dataSize = 0;
        record.opType.clear();
        record.priority = 0;
        record.delay.clear();
        record.phaseId = 0;
        record.dependOnPhases.clear();
        record.srcPortId = 0;
        record.dstPortId = 0;
        record.hasSrcPortId = false;
        record.hasDstPortId = false;

        int fieldCount = 0;
        size_t fieldStart = 0;
        while (fieldStart <= line.size()) {
            size_t fieldEnd = line.find(',', fieldStart);
            if (fieldEnd == string::npos) {
                fieldEnd = line.size();
            }

            SetTrafficRecordField(fieldCount,
                                  std::string_view(line).substr(fieldStart,
                                                                fieldEnd - fieldStart),
                                  record);
            ++fieldCount;

            if (fieldEnd == line.size()) {
                break;
            }
            fieldStart = fieldEnd + 1;
        }
        NS_ABORT_MSG_IF(
            fieldCount != 9 && fieldCount != 11,
            "traffic.csv row must have 9 base fields or 11 fields with srcPortId,dstPortId");
        callback(record);
    }
    file.close();
}

void
UbUtils::ForEachTrafficRecordViewInternal(
    const string& filename,
    const string& action,
    const std::function<void(const TrafficRecordView&)>& callback)
{
    PrintTimestamp("[traffic] " + action + " " + DisplayFilename(filename));
    ifstream file(filename);
    if (!file.is_open()) {
        NS_ASSERT_MSG(0, "Can not open File: " << filename);
        return;
    }
    string line;
    NS_ABORT_MSG_IF(!getline(file, line), "traffic.csv is missing its header");
    const std::vector<std::string> header = SplitCsvRow(line);
    ValidateTrafficCsvHeader(header);
    const std::size_t expectedFieldCount = header.size();
    while (getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == string::npos) {
            continue;
        }

        ValidateTrafficCsvRowWidth(line, expectedFieldCount);

        TrafficRecordView record;
        int fieldCount = 0;
        size_t fieldStart = 0;
        while (fieldStart <= line.size()) {
            size_t fieldEnd = line.find(',', fieldStart);
            if (fieldEnd == string::npos) {
                fieldEnd = line.size();
            }

            SetTrafficRecordViewField(fieldCount,
                                      std::string_view(line).substr(fieldStart,
                                                                    fieldEnd - fieldStart),
                                      record);
            ++fieldCount;

            if (fieldEnd == line.size()) {
                break;
            }
            fieldStart = fieldEnd + 1;
        }
        NS_ABORT_MSG_IF(
            fieldCount != 9 && fieldCount != 11,
            "traffic.csv row must have 9 base fields or 11 fields with srcPortId,dstPortId");
        callback(record);
    }
    file.close();
}

// 从TXT文件加载配置
void UbUtils::SetComponentsAttribute(const string &filename)
{
    PrintTimestamp("[setup] Load " + DisplayFilename(filename));
    g_config_path = std::string(filename);
    std::ifstream file(filename.c_str());
    if (!file.good()) {
        NS_ASSERT_MSG(0, "Can not open File: " << filename);
    }
    ValidateLegacyNetworkAttributeKeys(filename);
    uint32_t rewrittenLines = 0;
    const std::string configStoreFilename = CreateConfigStoreInput(filename, rewrittenLines);
    if (rewrittenLines > 0)
    {
        PrintTimestamp("[setup] Adapt legacy network attributes: " +
                       std::to_string(rewrittenLines) + " line(s)");
    }
    Config::SetDefault("ns3::ConfigStore::Filename", StringValue(configStoreFilename));
    Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("RawText"));
    Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Load"));
    ConfigStore config;
    config.ConfigureDefaults();
    ValidateConfiguredRoutingDefaults(filename);
    if (configStoreFilename != filename)
    {
        Config::SetDefault("ns3::ConfigStore::Filename", StringValue(filename));
        std::error_code ec;
        std::filesystem::remove(configStoreFilename, ec);
    }
}

void UbUtils::TopoTraceConnect()
{
    BooleanValue val;
    g_trace_enable.GetValue(val);
    TraceEnable = val.Get();

    if (!TraceEnable) {
        return; // 若不开启总开关则直接返回
    }

    g_task_trace_enable.GetValue(val);
    TaskTraceEnable = val.Get();

    g_packet_trace_enable.GetValue(val);
    PacketTraceEnable = val.Get();

    g_port_trace_enable.GetValue(val);
    PortTraceEnable = val.Get();

    g_record_pkt_trace_enable.GetValue(val);
    RecordTraceEnabled = val.Get();

    bool queueTraceEnabled = false;
    if (GlobalValue::GetValueByNameFailSafe("UB_QUEUE_TRACE_ENABLE", val))
    {
        queueTraceEnabled = val.Get();
    }

    bool flowControlTraceEnabled = false;
    if (GlobalValue::GetValueByNameFailSafe("UB_FLOW_CONTROL_TRACE_ENABLE", val))
    {
        flowControlTraceEnabled = val.Get();
    }

    bool congestionControlTraceEnabled = false;
    if (GlobalValue::GetValueByNameFailSafe("UB_CONGESTION_CONTROL_TRACE_ENABLE", val))
    {
        congestionControlTraceEnabled = val.Get();
    }

    NS_LOG_UNCOND("--- UnifiedBus Trace System Configuration ---");
    NS_LOG_UNCOND("UB_TRACE_ENABLE: " << (TraceEnable ? "ON" : "OFF"));
    if (TraceEnable) {
        NS_LOG_UNCOND("  UB_TASK_TRACE_ENABLE:   " << (TaskTraceEnable ? "ON" : "OFF") << "  (Task level events)");
        NS_LOG_UNCOND("  UB_PACKET_TRACE_ENABLE: " << (PacketTraceEnable ? "ON" : "OFF") << "  (Packet Send/ACK timestamps, essential for detailed task latency breakdown)");
        NS_LOG_UNCOND("  UB_PORT_TRACE_ENABLE:   " << (PortTraceEnable ? "ON" : "OFF") << "  (All port traffic, high volume, for throughput)");
        NS_LOG_UNCOND("  UB_RECORD_PKT_TRACE:    " << (RecordTraceEnabled ? "ON" : "OFF") << "  (Per-hop packet path tracking)");
        NS_LOG_UNCOND("  UB_QUEUE_TRACE_ENABLE:  "
                      << (queueTraceEnabled ? "ON" : "OFF")
                      << "  (QueueTrace_* files from queue events and periodic sampling)");
        NS_LOG_UNCOND("  UB_FLOW_CONTROL_TRACE_ENABLE: "
                      << (flowControlTraceEnabled ? "ON" : "OFF")
                      << "  (Algorithm-emitted PFC/CBFC trace files)");
        NS_LOG_UNCOND("  UB_CONGESTION_CONTROL_TRACE_ENABLE: "
                      << (congestionControlTraceEnabled ? "ON" : "OFF")
                      << "  (Algorithm-emitted DCQCN/CAQM trace files)");
    }
    NS_LOG_UNCOND("-------------------------------------------");

    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
        Ptr<Node> node = NodeList::GetNode(i);
        Ptr<UbController> ubCtrl = node->GetObject<ns3::UbController>();
        Ptr<UbSwitch> sw = node->GetObject<ns3::UbSwitch>();
        
        if (PacketTraceEnable) {
            sw->TraceConnectWithoutContext("LastPacketTraversesNotify", MakeCallback(SwitchLastPacketTraversesNotify));
        }

        std::map<uint32_t, Ptr<UbTransportChannel>> tpnMap;
        if (ubCtrl) {
            tpnMap = ubCtrl->GetTransportChannelManager()->GetTpnMap();
            for (const auto &pair : tpnMap) { // 设置 TP的trace callback
                auto tp = pair.second;
                if (tp) {
                    if (PacketTraceEnable) {
                        tp->TraceConnectWithoutContext("FirstPacketSendsNotify", MakeCallback(TpFirstPacketSendsNotify));
                        tp->TraceConnectWithoutContext("LastPacketSendsNotify", MakeCallback(TpLastPacketSendsNotify));
                        tp->TraceConnectWithoutContext("LastPacketACKsNotify", MakeCallback(TpLastPacketACKsNotify));
                        tp->TraceConnectWithoutContext("LastPacketReceivesNotify", MakeCallback(TpLastPacketReceivesNotify));
                    }
                    if (TaskTraceEnable) {
                        tp->TraceConnectWithoutContext("WqeSegmentSendsNotify", MakeCallback(TpWqeSegmentSendsNotify));
                        tp->TraceConnectWithoutContext("WqeSegmentCompletesNotify", MakeCallback(TpWqeSegmentCompletesNotify));
                    }
                    if (RecordTraceEnabled) {
                        tp->TraceConnectWithoutContext("TpRecvNotify", MakeCallback(TpRecvNotify));
                    }
                } else {
                    NS_ASSERT_MSG(0, "TP is null");
                }
            }
            Ptr<UbLdstInstance> ldstInstance = node->GetObject<UbLdstInstance>();
            if (ldstInstance != nullptr && TaskTraceEnable) {
                ldstInstance->TraceConnectWithoutContext("MemTaskCompletesNotify", MakeCallback(LdstMemTaskCompletesNotify));
                ldstInstance->TraceConnectWithoutContext("LastPacketACKsNotify", MakeCallback(LdstLastPacketACKsNotify));
                ldstInstance->TraceConnectWithoutContext("MemTaskStartsNotify", MakeCallback(LdstThreadMemTaskStartsNotify));
                ldstInstance->TraceConnectWithoutContext("FirstPacketSendsNotify", MakeCallback(LdstThreadFirstPacketSendsNotify));
                ldstInstance->TraceConnectWithoutContext("LastPacketSendsNotify", MakeCallback(LdstThreadLastPacketSendsNotify));
            }
            if (RecordTraceEnabled) {
                auto ldstApi = ubCtrl->GetUbFunction()->GetUbLdstApi();
                ldstApi->TraceConnectWithoutContext("LdstRecvNotify", MakeCallback(LdstRecvNotify));
            }
            if (PacketTraceEnable) {
                ubCtrl->SetCtpPacketTimingTraceCallbacks(MakeCallback(CtpFirstPacketSendsNotify),
                                                         MakeCallback(CtpLastPacketACKsNotify));
            }
        }

        if (queueTraceEnabled) {
            uint32_t DevicesNum = node->GetNDevices();
            for (uint32_t i = 0; i < DevicesNum; i++) {
                Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(i));
                if (port) {
                    port->GetUbQueue()->TraceConnectWithoutContext(
                        "UbEnqueue",
                        MakeBoundCallback(&UbUtils::QueueEgressEnqueueNotify, node->GetId(), i));
                    port->GetUbQueue()->TraceConnectWithoutContext(
                        "UbDequeue",
                        MakeBoundCallback(&UbUtils::QueueEgressDequeueNotify, node->GetId(), i));
                } else {
                    NS_ASSERT_MSG(0, "port is null");
                }
            }
        }

        if (PortTraceEnable) {
            uint32_t DevicesNum = node->GetNDevices();
            for (uint32_t i = 0; i < DevicesNum; i++) { // 设置 port的trace callback
                Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(i));
                if (port) {
                    port->TraceConnectWithoutContext("PortTxNotify", MakeCallback(PortTxNotify));
                    port->TraceConnectWithoutContext("PortRxNotify", MakeCallback(PortRxNotify));
                } else {
                    NS_ASSERT_MSG(0, "port is null");
                }
            }
        }

        if (sw != nullptr && queueTraceEnabled) {
            sw->GetQueueManager()->TraceConnectWithoutContext(
                "OutPortBufferBytes",
                MakeBoundCallback(&UbUtils::QueueVoqNotify, node->GetId()));
            sw->GetQueueManager()->TraceConnectWithoutContext(
                "IngressQueueOccupancyBytes",
                MakeBoundCallback(&UbUtils::QueueIngressOccupancyNotify, node->GetId()));
        }
    }

    StartQueueSampler();
}

void UbUtils::SingleTpTraceConnect(uint32_t nodeId, uint32_t tpn)
{
    BooleanValue val;
    g_trace_enable.GetValue(val);
    TraceEnable = val.Get();

    if (!TraceEnable) {
        return; // 若不开启总开关则直接返回
    }

    g_task_trace_enable.GetValue(val);
    TaskTraceEnable = val.Get();

    g_packet_trace_enable.GetValue(val);
    PacketTraceEnable = val.Get();

    g_record_pkt_trace_enable.GetValue(val);
    RecordTraceEnabled = val.Get();

    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<UbController> ubCtrl = node->GetObject<ns3::UbController>();
    Ptr<UbTransportChannel> tp = ubCtrl->GetTransportChannelManager()->PeekLiveTp(tpn);
    if (tp) {
        if (PacketTraceEnable) {
            tp->TraceConnectWithoutContext("FirstPacketSendsNotify", MakeCallback(TpFirstPacketSendsNotify));
            tp->TraceConnectWithoutContext("LastPacketSendsNotify", MakeCallback(TpLastPacketSendsNotify));
            tp->TraceConnectWithoutContext("LastPacketACKsNotify", MakeCallback(TpLastPacketACKsNotify));
            tp->TraceConnectWithoutContext("LastPacketReceivesNotify", MakeCallback(TpLastPacketReceivesNotify));
        }
        if (TaskTraceEnable) {
            tp->TraceConnectWithoutContext("WqeSegmentSendsNotify", MakeCallback(TpWqeSegmentSendsNotify));
            tp->TraceConnectWithoutContext("WqeSegmentCompletesNotify", MakeCallback(TpWqeSegmentCompletesNotify));
        }
        if (RecordTraceEnabled) {
            tp->TraceConnectWithoutContext("TpRecvNotify", MakeCallback(TpRecvNotify));
        }
    }
}

void UbUtils::ClientTraceConnect(int srcNode)
{
    if (!TraceEnable || !TaskTraceEnable) {
        return; // 若不开启trace或不开启task trace则直接返回
    }
    Ptr<ns3::UbApp> client = DynamicCast<ns3::UbApp>(NodeList::GetNode(srcNode)->GetApplication(0));
    client->TraceConnectWithoutContext("MemTaskStartsNotify", MakeCallback(DagMemTaskStartsNotify));
    client->TraceConnectWithoutContext("MemTaskCompletesNotify", MakeCallback(DagMemTaskCompletesNotify));
    client->TraceConnectWithoutContext("ComputeTaskQueuedNotify", MakeCallback(DagComputeTaskQueuedNotify));
    client->TraceConnectWithoutContext("ComputeTaskStartsNotify", MakeCallback(DagComputeTaskStartsNotify));
    client->TraceConnectWithoutContext("ComputeTaskCompletesNotify", MakeCallback(DagComputeTaskCompletesNotify));
    client->TraceConnectWithoutContext("WqeTaskStartsNotify", MakeCallback(DagWqeTaskStartsNotify));
    client->TraceConnectWithoutContext("WqeTaskCompletesNotify", MakeCallback(DagWqeTaskCompletesNotify));
}

bool UbUtils::QueryAttributeInfo(int argc, char *argv[])
{
    std::string className;
    std::string attrName;
    std::string globalName;
    std::string ignoredCasePath;
    bool printUbGlobals = false;

    CommandLine cmd;
    cmd.AddValue("ClassName", "Target class name", className);
    cmd.AddValue("AttributeName", "Target attribute name (optional)", attrName);
    cmd.AddValue("GlobalName", "Target Unified Bus global value name (optional)", globalName);
    cmd.AddValue("PrintUbGlobals", "Print Unified Bus global values with type metadata", printUbGlobals);
    cmd.AddValue("case-path", "Ignored case path for metadata-only queries", ignoredCasePath);
    cmd.AddNonOption("casePath", "Ignored case path for metadata-only queries", ignoredCasePath);
    cmd.Parse(argc, argv);

    auto isUbGlobal = [](const std::string& name) {
        return name.rfind("UB_", 0) == 0;
    };
    auto renderGlobalInfo = [](const GlobalValue& globalValue) {
        StringValue value;
        globalValue.GetValue(value);
        std::cout << "Global: " << globalValue.GetName() << '\n'
                  << "Description: " << globalValue.GetHelp() << '\n'
                  << "DataType: " << globalValue.GetChecker()->GetValueTypeName() << '\n'
                  << "Default: " << value.Get() << std::endl;
    };

    if (!globalName.empty()) {
        for (auto gvit = GlobalValue::Begin(); gvit != GlobalValue::End(); ++gvit) {
            if ((*gvit)->GetName() == globalName && isUbGlobal(globalName)) {
                renderGlobalInfo(*(*gvit));
                return true;
            }
        }
        std::cout << "Global not found!" << std::endl;
        return true;
    }

    if (printUbGlobals) {
        std::vector<GlobalValue*> ubGlobals;
        for (auto gvit = GlobalValue::Begin(); gvit != GlobalValue::End(); ++gvit) {
            if (isUbGlobal((*gvit)->GetName())) {
                ubGlobals.push_back(*gvit);
            }
        }
        std::sort(
            ubGlobals.begin(),
            ubGlobals.end(),
            [](const GlobalValue* lhs, const GlobalValue* rhs) {
                return lhs->GetName() < rhs->GetName();
            }
        );
        for (const auto* globalValue : ubGlobals) {
            renderGlobalInfo(*globalValue);
        }
        return true;
    }

    if (className == "" || className.empty()) {
        return false;
    }

    TypeId tid = TypeId::LookupByName(className);  // 获取TypeId

    if (!attrName.empty()) {  // attrName not empty
        struct TypeId::AttributeInformation info;
        if (tid.LookupAttributeByName(attrName, &info)) {  // 输出单个属性值
            std::cout << "Attribute: " << info.name << '\n'
                      << "Description: " << info.help << '\n'
                      << "DataType: " << info.checker->GetValueTypeName() << '\n'
                      << "Default: " << info.initialValue->SerializeToString(info.checker)
                      << std::endl;
        } else {
            std::cout << "Attribute not found!" << std::endl;
        }
    } else {  // 输出所有属性
        for (uint32_t i = 0; i < tid.GetAttributeN(); ++i) {
            TypeId::AttributeInformation info = tid.GetAttribute(i);
            std::cout << "Attribute: " << info.name << '\n'
                      << "Description: " << info.help << '\n'
                      << "DataType: " << info.checker->GetValueTypeName() << '\n'
                      << "Default: " << info.initialValue->SerializeToString(info.checker)
                      << std::endl;
        }
    }
    return true;  // 执行完后退出程序
}

void UbUtils::InitFaultMoudle(const string &FaultConfigFile)
{
    Ptr<UbFault> ubFault = CreateObject<UbFault>();
    for (auto it = NodeList::Begin(); it != NodeList::End(); ++it) {
        Ptr<Node> node = *it;
        uint16_t portNum = node->GetNDevices();
        for (int i = 0; i < portNum; i++) {
            Ptr<UbPort> port = DynamicCast<ns3::UbPort>(node->GetDevice(i));
            port->SetFaultCallBack(MakeCallback(&UbFault::FaultCallback, ubFault));
        }
    }

    ubFault->InitFault(FaultConfigFile);
}

} // namespace utils
