// SPDX-License-Identifier: GPL-2.0-only
#include "ub-link-health.h"

#include "ns3/global-value.h"
#include "ns3/log.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-link.h"
#include "ns3/ub-port.h"
#include "ns3/ub-routing-process.h"
#include "ns3/ub-switch.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbLinkHealth");

namespace {

GlobalValue g_linkHealthLocalDetect("UB_LINK_HEALTH_LOCAL_DETECT_NS",
                                    "Delay (ns) before a device notices that its own port was "
                                    "isolated and stops admitting new segments to it.",
                                    UintegerValue(0),
                                    MakeUintegerChecker<uint64_t>());
GlobalValue g_linkHealthRemoteNotify("UB_LINK_HEALTH_REMOTE_NOTIFY_NS",
                                     "Delay (ns) before remote devices learn that a peer device "
                                     "port was isolated (management/control notification).",
                                     UintegerValue(2000),
                                     MakeUintegerChecker<uint64_t>());

std::map<std::pair<uint32_t, uint32_t>, DataRate>&
OriginalRates()
{
    static std::map<std::pair<uint32_t, uint32_t>, DataRate> rates;
    return rates;
}

std::string
Trim(const std::string& value)
{
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool
ParseU64(const std::string& text, uint64_t& out)
{
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c));
        }))
    {
        return false;
    }
    try
    {
        out = std::stoull(text);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

Ptr<UbPort>
GetPort(uint32_t nodeId, uint32_t portId)
{
    if (nodeId >= NodeList::GetNNodes())
    {
        return nullptr;
    }
    Ptr<Node> node = NodeList::GetNode(nodeId);
    if (node == nullptr || portId >= node->GetNDevices())
    {
        return nullptr;
    }
    return DynamicCast<UbPort>(node->GetDevice(portId));
}

Ptr<UbPort>
GetPeer(const Ptr<UbPort>& port)
{
    Ptr<UbLink> link = port == nullptr ? nullptr : DynamicCast<UbLink>(port->GetChannel());
    return link == nullptr ? nullptr : link->GetDestination(port);
}

bool
IsSwitchNode(uint32_t nodeId)
{
    Ptr<UbSwitch> sw = NodeList::GetNode(nodeId)->GetObject<UbSwitch>();
    return sw != nullptr && sw->GetNodeType() == UB_SWITCH;
}

uint64_t
GetNs(GlobalValue& gv)
{
    UintegerValue value;
    gv.GetValue(value);
    return value.Get();
}

void
ApplyDeviceLocalIsolation(uint32_t nodeId, uint16_t portId, bool isolated)
{
    Ptr<UbController> ctrl = NodeList::GetNode(nodeId)->GetObject<UbController>();
    if (ctrl != nullptr)
    {
        ctrl->SetLocalPortIsolated(portId, isolated);
    }
}

void
ApplyDeviceRemoteIsolation(uint32_t nodeId, uint16_t portId, bool isolated)
{
    for (uint32_t other = 0; other < NodeList::GetNNodes(); ++other)
    {
        if (other == nodeId)
        {
            continue;
        }
        Ptr<UbController> ctrl = NodeList::GetNode(other)->GetObject<UbController>();
        if (ctrl != nullptr)
        {
            ctrl->SetPeerPortIsolated(nodeId, portId, isolated);
        }
    }
}

void
SetEndpointIsolated(const Ptr<UbPort>& port, bool peerIsSwitch, bool isolated)
{
    const uint32_t nodeId = port->GetNode()->GetId();
    const uint16_t portId = static_cast<uint16_t>(port->GetIfIndex());
    if (IsSwitchNode(nodeId))
    {
        // A switch output toward a device is the only route to that device port: keep it
        // routable so already-queued and in-flight packets drain. Device-side TP suspension
        // stops new traffic toward it.
        if (!peerIsSwitch)
        {
            return;
        }
        Ptr<UbRoutingProcess> routing =
            NodeList::GetNode(nodeId)->GetObject<UbSwitch>()->GetRoutingProcess();
        if (isolated)
        {
            routing->SetOutputIsolated(portId);
        }
        else
        {
            routing->ClearOutputIsolated(portId);
        }
        return;
    }
    const uint64_t localNs = GetNs(g_linkHealthLocalDetect);
    const uint64_t remoteNs = GetNs(g_linkHealthRemoteNotify);
    if (localNs == 0)
    {
        ApplyDeviceLocalIsolation(nodeId, portId, isolated);
    }
    else
    {
        Simulator::Schedule(NanoSeconds(localNs),
                            &ApplyDeviceLocalIsolation,
                            nodeId,
                            portId,
                            isolated);
    }
    if (remoteNs == 0)
    {
        ApplyDeviceRemoteIsolation(nodeId, portId, isolated);
    }
    else
    {
        Simulator::Schedule(NanoSeconds(remoteNs),
                            &ApplyDeviceRemoteIsolation,
                            nodeId,
                            portId,
                            isolated);
    }
}

void
SaveOriginalRate(const Ptr<UbPort>& port)
{
    const std::pair<uint32_t, uint32_t> key{port->GetNode()->GetId(), port->GetIfIndex()};
    OriginalRates().emplace(key, port->GetDataRate());
}

void
RunRow(UbLinkHealthRow row)
{
    switch (row.action)
    {
    case UbLinkHealthAction::DEGRADE:
        if (row.hasRate)
        {
            UbLinkHealthSetLinkRate(row.nodeId, row.portId, row.rate);
        }
        else
        {
            UbLinkHealthDegradeLanes(row.nodeId, row.portId, row.laneNum, row.laneDen);
        }
        return;
    case UbLinkHealthAction::RESTORE:
        UbLinkHealthRestoreLinkRate(row.nodeId, row.portId);
        return;
    case UbLinkHealthAction::ISOLATE:
        UbLinkHealthSetLinkIsolated(row.nodeId, row.portId, true);
        return;
    case UbLinkHealthAction::UNISOLATE:
        UbLinkHealthSetLinkIsolated(row.nodeId, row.portId, false);
        return;
    case UbLinkHealthAction::ISOLATE_NODE:
        UbLinkHealthSetNodeIsolated(row.nodeId, true);
        return;
    case UbLinkHealthAction::UNISOLATE_NODE:
        UbLinkHealthSetNodeIsolated(row.nodeId, false);
        return;
    case UbLinkHealthAction::DOWN:
        UbLinkHealthSetLinkDown(row.nodeId, row.portId);
        return;
    case UbLinkHealthAction::DOWN_NODE:
    {
        Ptr<Node> node = NodeList::GetNode(row.nodeId);
        for (uint32_t portId = 0; portId < node->GetNDevices(); ++portId)
        {
            Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(portId));
            if (port != nullptr && GetPeer(port) != nullptr)
            {
                UbLinkHealthSetLinkDown(row.nodeId, portId);
            }
        }
        return;
    }
    }
}

} // namespace

void
UbLinkHealthSetLinkRate(uint32_t nodeId, uint32_t portId, DataRate rate)
{
    Ptr<UbPort> port = GetPort(nodeId, portId);
    Ptr<UbPort> peer = GetPeer(port);
    NS_ABORT_MSG_IF(port == nullptr || peer == nullptr,
                    "link_health: node " << nodeId << " port " << portId << " is not attached");
    SaveOriginalRate(port);
    SaveOriginalRate(peer);
    port->SetDataRate(rate);
    peer->SetDataRate(rate);
}

void
UbLinkHealthDegradeLanes(uint32_t nodeId, uint32_t portId, uint32_t num, uint32_t den)
{
    Ptr<UbPort> port = GetPort(nodeId, portId);
    NS_ABORT_MSG_IF(port == nullptr, "link_health: unknown node/port " << nodeId << "/" << portId);
    SaveOriginalRate(port);
    const DataRate original = OriginalRates().at({nodeId, portId});
    UbLinkHealthSetLinkRate(nodeId,
                            portId,
                            DataRate(original.GetBitRate() / den * num));
}

void
UbLinkHealthRestoreLinkRate(uint32_t nodeId, uint32_t portId)
{
    Ptr<UbPort> port = GetPort(nodeId, portId);
    Ptr<UbPort> peer = GetPeer(port);
    NS_ABORT_MSG_IF(port == nullptr || peer == nullptr,
                    "link_health: node " << nodeId << " port " << portId << " is not attached");
    for (const Ptr<UbPort>& p : {port, peer})
    {
        auto it = OriginalRates().find({p->GetNode()->GetId(), p->GetIfIndex()});
        if (it != OriginalRates().end())
        {
            p->SetDataRate(it->second);
        }
    }
}

void
UbLinkHealthSetLinkIsolated(uint32_t nodeId, uint32_t portId, bool isolated)
{
    Ptr<UbPort> port = GetPort(nodeId, portId);
    Ptr<UbPort> peer = GetPeer(port);
    NS_ABORT_MSG_IF(port == nullptr || peer == nullptr,
                    "link_health: node " << nodeId << " port " << portId << " is not attached");
    const bool portIsSwitch = IsSwitchNode(nodeId);
    const bool peerIsSwitch = IsSwitchNode(peer->GetNode()->GetId());
    SetEndpointIsolated(port, peerIsSwitch, isolated);
    SetEndpointIsolated(peer, portIsSwitch, isolated);
}

void
ApplyDeviceLocalPortDown(uint32_t nodeId, uint16_t portId)
{
    Ptr<UbController> ctrl = NodeList::GetNode(nodeId)->GetObject<UbController>();
    if (ctrl != nullptr)
    {
        ctrl->OnLocalPortDown(portId);
    }
}

void
SetEndpointDown(const Ptr<UbPort>& port, bool peerIsSwitch)
{
    const uint32_t nodeId = port->GetNode()->GetId();
    const uint16_t portId = static_cast<uint16_t>(port->GetIfIndex());
    port->SetHardDown(true);
    if (IsSwitchNode(nodeId))
    {
        // A switch sees its own port go down: stop routing over it when an alternative exists
        // (switch-to-switch). A device-facing port is the only path to that device port.
        if (peerIsSwitch)
        {
            NodeList::GetNode(nodeId)->GetObject<UbSwitch>()->GetRoutingProcess()->SetOutputIsolated(
                portId);
        }
        return;
    }
    // A device only learns about its OWN port (local link detection); remote ends are not told
    // by the simulator - they must detect the failure themselves (transport failure detector).
    const uint64_t localNs = GetNs(g_linkHealthLocalDetect);
    if (localNs == 0)
    {
        ApplyDeviceLocalPortDown(nodeId, portId);
    }
    else
    {
        Simulator::Schedule(NanoSeconds(localNs), &ApplyDeviceLocalPortDown, nodeId, portId);
    }
}

void
UbLinkHealthSetNodeIsolated(uint32_t nodeId, bool isolated)
{
    Ptr<Node> node = NodeList::GetNode(nodeId);
    for (uint32_t portId = 0; portId < node->GetNDevices(); ++portId)
    {
        Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(portId));
        if (port != nullptr && GetPeer(port) != nullptr)
        {
            UbLinkHealthSetLinkIsolated(nodeId, portId, isolated);
        }
    }
}

void
UbLinkHealthSetLinkDown(uint32_t nodeId, uint32_t portId)
{
    Ptr<UbPort> port = GetPort(nodeId, portId);
    Ptr<UbPort> peer = GetPeer(port);
    NS_ABORT_MSG_IF(port == nullptr || peer == nullptr,
                    "link_health: node " << nodeId << " port " << portId << " is not attached");
    const bool portIsSwitch = IsSwitchNode(nodeId);
    const bool peerIsSwitch = IsSwitchNode(peer->GetNode()->GetId());
    SetEndpointDown(port, peerIsSwitch);
    SetEndpointDown(peer, portIsSwitch);
}

void
UbLinkHealthReset()
{
    OriginalRates().clear();
}

bool
TryReadLinkHealthSchedule(const std::string& path,
                          std::vector<UbLinkHealthRow>& rows,
                          std::string& error)
{
    rows.clear();
    std::ifstream in(path);
    if (!in.is_open())
    {
        return true; // optional file
    }
    std::string line;
    uint32_t lineNo = 0;
    bool headerSeen = false;
    while (std::getline(in, line))
    {
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (Trim(line).empty())
        {
            continue;
        }
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, ','))
        {
            fields.push_back(Trim(field));
        }
        if (!headerSeen)
        {
            headerSeen = true;
            if (fields != std::vector<std::string>{"timeNs", "nodeId", "portId", "action", "value"})
            {
                error = path + ": header must be timeNs,nodeId,portId,action,value";
                return false;
            }
            continue;
        }
        if (fields.size() == 4)
        {
            fields.emplace_back();
        }
        const std::string where = path + ":" + std::to_string(lineNo) + ": ";
        if (fields.size() != 5)
        {
            error = where + "expected 5 fields";
            return false;
        }
        UbLinkHealthRow row;
        row.line = lineNo;
        uint64_t nodeId = 0;
        uint64_t portId = 0;
        if (!ParseU64(fields[0], row.timeNs) || !ParseU64(fields[1], nodeId) ||
            !ParseU64(fields[2], portId))
        {
            error = where + "timeNs/nodeId/portId must be non-negative integers";
            return false;
        }
        row.nodeId = static_cast<uint32_t>(nodeId);
        row.portId = static_cast<uint32_t>(portId);
        row.value = fields[4];
        const std::string& action = fields[3];
        if (action == "DEGRADE")
        {
            row.action = UbLinkHealthAction::DEGRADE;
            const auto slash = row.value.find('/');
            uint64_t num = 0;
            uint64_t den = 0;
            if (slash != std::string::npos && ParseU64(row.value.substr(0, slash), num) &&
                ParseU64(row.value.substr(slash + 1), den) && den != 0 && num != 0 && num <= den)
            {
                row.laneNum = static_cast<uint32_t>(num);
                row.laneDen = static_cast<uint32_t>(den);
            }
            else
            {
                DataRateValue rate;
                if (!rate.DeserializeFromString(row.value, MakeDataRateChecker()) ||
                    rate.Get().GetBitRate() == 0)
                {
                    error = where + "DEGRADE value must be a data rate (e.g. 300Gbps) or a lane "
                                    "fraction num/den with 0<num<=den, got '" +
                            row.value + "'";
                    return false;
                }
                row.hasRate = true;
                row.rate = rate.Get();
            }
        }
        else if (action == "RESTORE")
        {
            row.action = UbLinkHealthAction::RESTORE;
        }
        else if (action == "ISOLATE")
        {
            row.action = UbLinkHealthAction::ISOLATE;
        }
        else if (action == "UNISOLATE")
        {
            row.action = UbLinkHealthAction::UNISOLATE;
        }
        else if (action == "ISOLATE_NODE")
        {
            row.action = UbLinkHealthAction::ISOLATE_NODE;
        }
        else if (action == "DOWN")
        {
            row.action = UbLinkHealthAction::DOWN;
        }
        else if (action == "DOWN_NODE")
        {
            row.action = UbLinkHealthAction::DOWN_NODE;
        }
        else if (action == "UNISOLATE_NODE")
        {
            row.action = UbLinkHealthAction::UNISOLATE_NODE;
        }
        else
        {
            error = where + "unknown action '" + action + "'";
            return false;
        }
        // Topology validation.
        if (row.nodeId >= NodeList::GetNNodes())
        {
            error = where + "unknown node " + std::to_string(row.nodeId);
            return false;
        }
        if (row.action != UbLinkHealthAction::ISOLATE_NODE &&
            row.action != UbLinkHealthAction::UNISOLATE_NODE)
        {
            Ptr<UbPort> port = GetPort(row.nodeId, row.portId);
            if (port == nullptr || GetPeer(port) == nullptr)
            {
                error = where + "node " + std::to_string(row.nodeId) + " port " +
                        std::to_string(row.portId) + " is not an attached UB port";
                return false;
            }
        }
        rows.push_back(row);
    }
    return true;
}

void
ScheduleLinkHealthActions(const std::vector<UbLinkHealthRow>& rows)
{
    for (const auto& row : rows)
    {
        Simulator::Schedule(NanoSeconds(row.timeNs), &RunRow, row);
    }
}

} // namespace ns3
