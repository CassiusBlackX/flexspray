// SPDX-License-Identifier: GPL-2.0-only
// Fixed, single-packet ROI experiment. No protocol behavior is implemented here.
#include "ns3/core-module.h"
#include "ns3/ub-function.h"
#include "ns3/ub-transport-channel-manager.h"
#include "ns3/ub-utils.h"

#include <deque>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <set>

using namespace ns3;
namespace
{
constexpr uint32_t JETTY = 3;
constexpr uint64_t DATA_ADDRESS = 0x1000;
constexpr uint64_t COUNTER_ADDRESS = 0x2000;
constexpr uint32_t DATA_BYTES = 64;

std::string
Json(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value)
    {
        if (c == '"' || c == '\\')
        {
            out << '\\' << c;
        }
        else if (c < 32)
        {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        }
        else
        {
            out << c;
        }
    }
    return out.str() + '"';
}

std::string
Hex(uint64_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

struct Event
{
    uint64_t seq;
    int64_t time;
    std::string node, name, task, opcode, address, bytes, value, packetBytes;
};

struct Experiment
{
    // Appending diagnostic events must not invalidate pointers returned by Find.
    std::deque<Event> events;
    std::vector<std::string> errors;
    Ptr<UbTransportChannel> aTp, bTp;
    UbFault packetClassifier;
    bool treatment{false};
    uint64_t counter{0};
    bool committed{false};
    bool updated{false};
    std::set<uint32_t> completions;

    void Record(std::string node, std::string name, std::string task = {},
                std::string opcode = {}, std::string address = {}, std::string bytes = {},
                std::string value = {}, std::string packetBytes = {})
    {
        events.push_back({events.size() + 1, Simulator::Now().GetPicoSeconds(), node, name,
                          task, opcode, address, bytes, value, packetBytes});
    }

    void Fail(const std::string& reason)
    {
        errors.push_back(reason);
        Record("B", "error");
        std::cerr << reason << '\n';
    }

    void Execute(Ptr<const UbWqeSegment> request, const UbTransactionRule&,
                 UbTargetCompletion done)
    {
        const auto opcode = request->GetType();
        const auto address = request->GetRemoteAddress();
        const auto bytes = request->GetLogicalBytes();
        const auto task = std::to_string(request->GetTaskId());
        const auto op = std::to_string(static_cast<unsigned>(opcode));
        Record("B", "target_execute", task, op, Hex(address), std::to_string(bytes));
        const bool combined = opcode == UbTransactionOpcode::WRITE_WITH_ATOMIC_STORE_ADD;
        if ((!combined && opcode != UbTransactionOpcode::WRITE) || combined != treatment)
        {
            Fail("unexpected opcode");
            return;
        }
        if (address == DATA_ADDRESS && bytes == DATA_BYTES)
        {
            if (committed)
            {
                Fail("duplicate data commit");
                return;
            }
            const auto& notify = request->GetTransactionFields().notify;
            if (combined && (!notify || notify->address != COUNTER_ADDRESS || updated ||
                             notify->data > std::numeric_limits<uint64_t>::max() - counter))
            {
                Fail("missing/invalid notify address, duplicate update or counter overflow");
                return;
            }
            committed = true;
            Record("B", "data_commit", task, op, Hex(address), std::to_string(bytes));
            if (combined)
            {
                Record("B", "notify_value_decoded", task, op, Hex(notify->address), "8",
                       std::to_string(notify->data));
                UpdateCounter(counter + notify->data, task, op);
            }
        }
        else if (!combined && address == COUNTER_ADDRESS && bytes == 8)
        {
            auto payload = request->GetExplicitPayload();
            if (!payload || payload->GetSize() != 8 || updated)
            {
                Fail("missing/wrong post payload or duplicate counter update");
                return;
            }
            uint8_t wire[8];
            payload->CopyData(wire, 8);
            uint64_t value = 0;
            for (auto byte : wire)
            {
                value = (value << 8) | byte;
            }
            Record("B", "post_value_decoded", task, op, Hex(address), "8",
                   std::to_string(value));
            UpdateCounter(value, task, op);
        }
        else
        {
            Fail("unexpected address or length");
            return;
        }
        done(UbWorkExecutionResult{});
    }

    void UpdateCounter(uint64_t value, const std::string& task, const std::string& op)
    {
        updated = true;
        counter = value;
        Record("B", "counter_update", task, op, Hex(COUNTER_ADDRESS), "8",
               std::to_string(counter));
        if (counter == DATA_BYTES)
        {
            Record("B", "counter_ready", task, op, Hex(COUNTER_ADDRESS), "8",
                   std::to_string(counter));
            if (!committed)
            {
                Fail("early-ready");
            }
        }
        else
        {
            Fail(counter > DATA_BYTES ? "counter overshoot" : "unexpected counter value");
        }
    }

    void Complete(uint32_t task, uint32_t)
    {
        Record("A", "a_complete", std::to_string(task));
        if (!completions.insert(task).second)
        {
            Fail("duplicate A completion");
        }
    }

    // The existing fault classifier excludes TPACK/control frames before invoking the same
    // rule-driven decoder as RTP reception. Both helpers operate on copies, never live bytes.
    void PacketEvent(bool atA, bool transmit, Ptr<Packet> packet)
    {
        UbFlowTag flow;
        packet->PeekPacketTag(flow);
        RetransFaultPacketInfo info;
        if (!packetClassifier.TryGetRetransFaultPacketInfo(packet, atA ? 0 : 1, 0,
                                                        flow.GetFlowId(), info))
        {
            Record(atA ? "A" : "B", transmit ? "other_tx" : "other_rx", {}, {}, {}, {}, {},
                   std::to_string(packet->GetSize()));
            return;
        }
        if (info.packetType != RetransFaultPacketType::DATA)
        {
            Record(atA ? "A" : "B", transmit ? "tpack_tx" : "tpack_rx", {}, {}, {}, {}, {},
                   std::to_string(packet->GetSize()));
            return;
        }
        UbTransportChannel::ReceivedDataPacketContext decoded;
        if (!(atA ? bTp : aTp)->ParseReceivedDataPacketForTest(packet->Copy(), decoded))
        {
            Fail("RTP data decode failed");
            return;
        }
        auto opcode = decoded.transactionHeader.GetUbTransactionOpcode();
        const bool ack = opcode == static_cast<unsigned>(UbTransactionOpcode::TAACK);
        Record(atA ? "A" : "B",
               ack ? (transmit ? "taack_tx" : "taack_rx")
                   : (transmit ? "request_tx" : "request_rx"),
               std::to_string(decoded.flowTag.GetFlowId()), std::to_string(opcode),
               ack ? "" : Hex(decoded.maExtHeader.GetVirtualAddress()),
               std::to_string(decoded.payloadBytes),
               ack ? "" : std::to_string(decoded.transactionHeader.GetOrder()),
               std::to_string(packet->GetSize()));
    }
    void ATx(Ptr<Packet> p, Time) { PacketEvent(true, true, p); }
    void BTx(Ptr<Packet> p, Time) { PacketEvent(false, true, p); }
    void ARx(Ptr<Packet> p) { PacketEvent(true, false, p); }
    void BRx(Ptr<Packet> p) { PacketEvent(false, false, p); }

    std::vector<const Event*> Find(const std::string& name, const std::string& task = {}) const
    {
        std::vector<const Event*> found;
        for (const auto& event : events)
        {
            if (event.name == name && (task.empty() || event.task == task))
            {
                found.push_back(&event);
            }
        }
        return found;
    }

    void Validate()
    {
        for (const auto& name : {"wqe_submit", "request_tx", "request_rx", "target_execute",
                                 "taack_tx", "taack_rx", "a_complete"})
        {
            if (Find(name).size() != (treatment ? 1u : 2u))
            {
                Fail(std::string("wrong count: ") + name);
            }
        }
        if (Find("data_commit").size() != 1 || Find("counter_ready").size() != 1 ||
            Find("counter_update").size() != 1 || Find(treatment ? "notify_value_decoded" : "post_value_decoded").size() != 1 ||
            Find("request_tx", "1").size() != 1 || counter != DATA_BYTES || !committed ||
            completions != (treatment ? std::set<uint32_t>{1} : std::set<uint32_t>{1, 2}))
        {
            Fail("missing/duplicate terminal evidence");
        }
        const auto commits = Find("data_commit");
        const auto ready = Find("counter_ready");
        const auto dataTx = Find("request_tx", "1");
        const auto postTx = Find("request_tx", "2");
        if (commits.size() == 1 && ready.size() == 1 && commits[0]->seq >= ready[0]->seq)
        {
            Fail("data commit must precede ready");
        }
        if (dataTx.size() == 1 &&
            (dataTx[0]->value != "1" ||
             (!treatment && postTx.size() == 1 && postTx[0]->value != "2")))
        {
            Fail("wire order must be RO then SO");
        }
        auto dataAck = Find("taack_rx", "1");
        auto post = Find("request_tx", "2");
        if (!treatment &&
            (dataAck.size() != 1 || post.size() != 1 || dataAck[0]->seq >= post[0]->seq))
        {
            Fail("post did not follow data TAACK");
        }
    }

    void Write(const std::filesystem::path& dir, const std::string& variant)
    {
        std::ofstream csv(dir / "events.csv");
        csv << "seq,time_ps,node,event,task_id,opcode,address,byte_count,value,packet_bytes\n";
        for (const auto& e : events)
        {
            csv << e.seq << ',' << e.time << ',' << e.node << ',' << e.name << ',' << e.task
                << ',' << e.opcode << ',' << e.address << ',' << e.bytes << ',' << e.value
                << ',' << e.packetBytes << '\n';
        }
        std::ofstream out(dir / "summary.json");
        out << "{\n\"schema_version\":1,\"case_id\":" << Json(variant)
            << ",\"variant\":" << Json(variant) << ",\"valid\":"
            << (errors.empty() ? "true" : "false") << ",\"errors\":[";
        for (size_t i = 0; i < errors.size(); ++i)
        {
            out << (i ? "," : "") << Json(errors[i]);
        }
        out << "],\n";
        auto timestamp = [&](const std::string& name, const std::string& task = "") {
            auto found = Find(name, task);
            return found.size() == 1 ? std::to_string(found[0]->time) : std::string("null");
        };
        out << "\"t_submit_ps\":" << timestamp("wqe_submit", "1")
            << ",\"t_data_commit_ps\":" << timestamp("data_commit")
            << ",\"t_counter_ready_ps\":" << timestamp("counter_ready")
            << ",\"t_a_complete_ps\":" << timestamp("a_complete", treatment ? "1" : "2");
        // Both submissions are made at simulation time zero; retain null on missing events.
        out << ",\"b_ready_latency_ps\":" << timestamp("counter_ready")
            << ",\"a_completion_latency_ps\":" << timestamp("a_complete", treatment ? "1" : "2")
            << ",\"counter_address\":\"0x2000\",\"counter_final\":" << counter
            << ",\"expected_total\":64,\"committed_ranges\":"
            << (committed ? "[[\"0x1000\",\"0x1040\"]]" : "[]")
            << ",\"target_execution_count\":" << Find("target_execute").size()
            << ",\"readiness_count\":" << Find("counter_ready").size()
            << ",\"a_completion_count\":" << Find("a_complete").size()
            << ",\"data_packet_count\":" << Find("request_tx", "1").size() << "\n}\n";
        csv.close();
        out.close();
        if (!csv || !out)
        {
            throw std::runtime_error("failed to write events/summary");
        }
    }
};

// Discover all scalar UB defaults/globals at runtime, then query the instantiated objects.
// Pointer attributes are object relationships, not parameter values or portable addresses.
void
Snapshot(const std::filesystem::path& dir, const std::string& variant,
         const std::vector<std::pair<std::string, Ptr<Object>>>& objects)
{
    std::map<std::string, std::string> values;
    for (uint16_t i = 0; i < TypeId::GetRegisteredN(); ++i)
    {
        auto type = TypeId::GetRegistered(i);
        if (type.GetName().rfind("ns3::Ub", 0) != 0)
        {
            continue;
        }
        for (uint32_t j = 0; j < type.GetAttributeN(); ++j)
        {
            auto a = type.GetAttribute(j);
            if (a.checker->GetValueTypeName().find("Pointer") == std::string::npos &&
                a.checker->GetValueTypeName().find("ObjectPtrContainer") == std::string::npos)
            {
                values["default/" + type.GetName() + "::" + a.name] =
                    a.initialValue->SerializeToString(a.checker);
            }
        }
    }
    for (auto it = GlobalValue::Begin(); it != GlobalValue::End(); ++it)
    {
        StringValue v;
        (*it)->GetValue(v);
        values["global/" + (*it)->GetName()] = v.Get();
    }
    for (const auto& [label, object] : objects)
    {
        auto type = object->GetInstanceTypeId();
        while (type != ObjectBase::GetTypeId())
        {
            for (uint32_t j = 0; j < type.GetAttributeN(); ++j)
            {
                auto a = type.GetAttribute(j);
                StringValue v;
                if (a.checker->GetValueTypeName().find("Pointer") == std::string::npos &&
                    a.checker->GetValueTypeName().find("ObjectPtrContainer") == std::string::npos &&
                    object->GetAttributeFailSafe(a.name, v))
                {
                    values[label + "/" + type.GetName() + "::" + a.name] = v.Get();
                }
            }
            type = type.GetParent();
        }
    }
    std::ofstream out(dir / "resolved-parameters.json");
    out << "{\"schema_version\":1,\"variant\":" << Json(variant)
        << ",\"program\":\"ub-roi-write-counter\",\"build_stamp\":" << Json(__DATE__ " " __TIME__)
        << ",\"time_resolution\":\"ps\",\"rate_bps_each_direction\":400000000000,"
           "\"propagation_delay_ps_each_direction\":1000000,\"target_execution_delay_ps\":0,"
           "\"transport\":\"RTP\",\"service_mode\":\"ROI\",\"rtp_pairs\":1,"
           "\"jetty\":3,\"data_address\":\"0x1000\",\"counter_address\":\"0x2000\","
           "\"counter_initial\":0,\"data_bytes\":64,\"expected_total\":64,"
           "\"watchdog_ps\":1000000000,\"trace_boundary\":"
        << Json("Port TraComEventNotify = accepted transmission start; PktRcvNotify = full packet "
                "arrival before switch processing. packet_bytes includes modeled link/network/TP/TA "
                "headers, excludes physical preamble/FEC; TPACK logged separately.")
        << ",\"runtime_values\":{\n";
    bool first = true;
    for (const auto& [key, value] : values)
    {
        out << (first ? "" : ",\n") << Json(key) << ':' << Json(value);
        first = false;
    }
    out << "\n}}\n";
    out.close();
    if (!out)
    {
        throw std::runtime_error("failed to write resolved parameters");
    }
}
} // namespace

int
main(int argc, char* argv[])
{
    Time::SetResolution(Time::PS);
    std::string variant = "control", outputDir;
    CommandLine cmd;
    cmd.AddValue("variant", "control or treatment", variant);
    cmd.AddValue("output-dir", "New evidence directory", outputDir);
    cmd.Parse(argc, argv);
    if ((variant != "control" && variant != "treatment") || outputDir.empty())
    {
        std::cerr << "require --variant=control|treatment and --output-dir\n";
        return 2;
    }
    const std::filesystem::path dir(outputDir);
    if (std::filesystem::exists(dir / "events.csv") ||
        std::filesystem::exists(dir / "summary.json") ||
        std::filesystem::exists(dir / "resolved-parameters.json"))
    {
        std::cerr << "refusing to overwrite evidence\n";
        return 2;
    }
    std::filesystem::create_directories(dir);
    Experiment experiment; // Outlives all callbacks, including Simulator::Destroy.
    experiment.treatment = variant == "treatment";
    try
    {
        Config::SetDefault("ns3::UbPort::UbDataRate", DataRateValue(DataRate("400Gbps")));
        Config::SetDefault("ns3::UbLink::Delay", TimeValue(MicroSeconds(1)));
        Config::SetDefault("ns3::UbController::InitiatorProtocolDispatchDelay", TimeValue(Time(0)));
        Config::SetDefault("ns3::UbController::InitiatorProtocolCompletionVisibilityDelay",
                           TimeValue(Time(0)));
        utils::UbUtils::Get()->CreateNode(std::vector<utils::UbStructuredNodeConfig>{
            {0, "DEVICE", 1, "", ""}, {1, "DEVICE", 1, "", ""}});
        auto a = NodeList::GetNode(0);
        auto b = NodeList::GetNode(1);
        auto ap = DynamicCast<UbPort>(a->GetDevice(0));
        auto bp = DynamicCast<UbPort>(b->GetDevice(0));
        auto link = CreateObject<UbLink>();
        ap->Attach(link);
        bp->Attach(link);
        for (auto node : {a, b})
        {
            auto route = node->GetObject<UbSwitch>()->GetRoutingProcess();
            const auto peer = 1 - node->GetId();
            route->GetRoutingTable().AddShortestRoute(NodeIdToIp(peer).Get(), {0});
            route->GetRoutingTable().AddShortestRoute(NodeIdToIp(peer, 0).Get(), {0});
        }
        auto ac = a->GetObject<UbController>();
        auto bc = b->GetObject<UbController>();
        Connection connection;
        connection.ownerNodeId = 0;
        connection.node1 = 0;
        connection.port1 = 0;
        connection.tpn1 = 101;
        connection.node2 = 1;
        connection.port2 = 0;
        connection.tpn2 = 202;
        connection.priority = 7;
        connection.metrics = 0;
        ac->GetTpConnManager()->AddUnilateralConnection(connection, 0);
        bc->GetTpConnManager()->AddUnilateralConnection(connection, 1);
        ac->GetTransportChannelManager()->CreateTp(0, 1, 0, 0, 7, 101, 202,
                                                   UbCongestionControl::Create(UB_DEVICE));
        bc->GetTransportChannelManager()->CreateTp(1, 0, 0, 0, 7, 202, 101,
                                                   UbCongestionControl::Create(UB_DEVICE));
        experiment.aTp = ac->GetTransportChannelManager()->GetTp(101);
        experiment.bTp = bc->GetTransportChannelManager()->GetTp(202);
        auto function = ac->GetUbFunction();
        function->CreateJetty(0, 1, JETTY);
        if (!ac->GetUbTransaction()->JettyBindTp(0, 1, JETTY, false, {101}) ||
            !bc->GetUbTransaction()->SetTargetExecutionDelay(Time(0)) ||
            !bc->GetUbTransaction()->SetTargetExecutor(
                [&experiment](Ptr<const UbWqeSegment> req, const UbTransactionRule& rule,
                              UbTargetCompletion done) { experiment.Execute(req, rule, done); }))
        {
            throw std::runtime_error("channel/executor setup rejected");
        }
        ac->GetUbTransaction()->SetTransactionServiceMode(JETTY, TransactionServiceMode::ROI);
        function->GetJetty(JETTY)->SetClientCallback(MakeCallback(&Experiment::Complete, &experiment));
        if (!ap->TraceConnectWithoutContext("TraComEventNotify", MakeCallback(&Experiment::ATx, &experiment)) ||
            !bp->TraceConnectWithoutContext("TraComEventNotify", MakeCallback(&Experiment::BTx, &experiment)) ||
            !ap->TraceConnectWithoutContext("PktRcvNotify", MakeCallback(&Experiment::ARx, &experiment)) ||
            !bp->TraceConnectWithoutContext("PktRcvNotify", MakeCallback(&Experiment::BRx, &experiment)))
        {
            throw std::runtime_error("trace connection failed");
        }
        Snapshot(dir, variant, {{"A/port", ap}, {"B/port", bp}, {"link", link},
                 {"A/controller", ac}, {"B/controller", bc},
                 {"A/transaction", ac->GetUbTransaction()}, {"B/transaction", bc->GetUbTransaction()},
                 {"A/tp", experiment.aTp}, {"B/tp", experiment.bTp},
                 {"A/switch", a->GetObject<UbSwitch>()}, {"B/switch", b->GetObject<UbSwitch>()},
                 {"A/routing", a->GetObject<UbSwitch>()->GetRoutingProcess()},
                 {"B/routing", b->GetObject<UbSwitch>()->GetRoutingProcess()},
                 {"A/allocator", a->GetObject<UbSwitch>()->GetAllocator()},
                 {"B/allocator", b->GetObject<UbSwitch>()->GetAllocator()},
                 {"A/queues", a->GetObject<UbSwitch>()->GetQueueManager()},
                 {"B/queues", b->GetObject<UbSwitch>()->GetQueueManager()}});
        experiment.Record("B", "counter_init", {}, {}, Hex(COUNTER_ADDRESS), "8", "0");
        const uint32_t taskCount = experiment.treatment ? 1 : 2;
        for (uint32_t task = 1; task <= taskCount; ++task)
        {
            auto wqe = function->CreateWqe(0, 1, task == 1 ? 64 : 8, task,
                experiment.treatment ? UbTransactionOpcode::WRITE_WITH_ATOMIC_STORE_ADD
                                     : UbTransactionOpcode::WRITE);
            if (experiment.treatment)
            {
                UbTransactionFields fields;
                fields.notify = UbNotifyFields{COUNTER_ADDRESS, 0, 0, DATA_BYTES};
                wqe->SetTransactionFields(fields);
            }
            wqe->SetRemoteAddress(task == 1 ? DATA_ADDRESS : COUNTER_ADDRESS);
            wqe->SetOrderType(task == 1 ? OrderType::ORDER_RELAX : OrderType::ORDER_STRONG);
            if (task == 2)
            {
                const uint8_t bytes[8] = {0, 0, 0, 0, 0, 0, 0, 64};
                wqe->SetExplicitPayload(Create<Packet>(bytes, 8));
            }
            experiment.Record("A", "wqe_submit", std::to_string(task),
                              std::to_string(static_cast<unsigned>(wqe->GetType())),
                              Hex(wqe->GetRemoteAddress()), task == 1 ? "64" : "8");
            if (function->SubmitWqe(wqe, JETTY).status != UbWqeSubmissionStatus::ACCEPTED)
            {
                experiment.Fail("WQE submission rejected");
            }
        }
        Simulator::Stop(MilliSeconds(1));
        Simulator::Run();
        experiment.Validate();
        const auto stoppedAt = Simulator::Now().GetPicoSeconds();
        Simulator::Destroy();
        experiment.aTp = nullptr;
        experiment.bTp = nullptr;
        utils::UbUtils::Get()->Destroy();
        experiment.Record("A", "cleanup_complete");
        // Destroy resets Now(); retain the observed simulation stop boundary for cleanup.
        experiment.events.back().time = stoppedAt;
        experiment.Write(dir, variant);
        return experiment.errors.empty() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        Simulator::Destroy();
        utils::UbUtils::Get()->Destroy();
        return 2;
    }
}
