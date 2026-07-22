// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/config.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/ub-next-hop-index.h"
#include "ns3/ub-routing-process.h"
#include "ns3/ub-rule-routing.h"
#include "ns3/ub-switch.h"
#include "ns3/ub-utils.h"

#include <csignal>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace ns3;

namespace
{

#ifndef _WIN32
int
RunStructuredTestInChild(const std::function<void()>& function)
{
    const pid_t pid = fork();
    if (pid == -1)
    {
        return -1;
    }
    if (pid == 0)
    {
        function();
        std::_Exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid)
    {
        return -1;
    }
    return status;
}
#endif

class UbRuleRoutingTableTest : public TestCase
{
  public:
    UbRuleRoutingTableTest()
        : TestCase("Structured rule routing preserves exact and range precedence")
    {
    }

    void DoRun() override
    {
        auto rules = std::make_shared<UbRuleRoutingTable>();
        rules->AddPortGroup({7, 10, {2, 3}});
        rules->AddPortGroup({7, 11, {4}});
        rules->AddPortGroup({7, 12, {9}});
        rules->AddRule({7, {42, UbRuleDestination::ANY_PORT}, {10}, {11}});
        rules->AddRule({7, {42, 2}, {12}, {}});
        rules->AddRangeRule({7, 100, 199, 2, {10}, {11}});
        rules->AddRule({7, {150, 2}, {12}, {}});
        rules->PrepareForLookup();

        std::vector<uint16_t> shortestPorts;
        std::vector<uint16_t> nonShortestPorts;
        bool handled = rules->TryLookup(7, 42, 2, shortestPorts, nonShortestPorts);
        NS_TEST_ASSERT_MSG_EQ(handled, true, "exact destination-port rule should be handled");
        NS_TEST_ASSERT_MSG_EQ((shortestPorts == std::vector<uint16_t>({9})),
                              true,
                              "exact rule should win");
        NS_TEST_ASSERT_MSG_EQ(nonShortestPorts.empty(), true, "exact result should be complete");

        handled = rules->TryLookup(7, 42, 3, shortestPorts, nonShortestPorts);
        NS_TEST_ASSERT_MSG_EQ(handled, true, "node rule should serve unmatched ports");
        NS_TEST_ASSERT_MSG_EQ((shortestPorts == std::vector<uint16_t>({2, 3})),
                              true,
                              "node rule ports");
        NS_TEST_ASSERT_MSG_EQ((nonShortestPorts == std::vector<uint16_t>({4})),
                              true,
                              "alternate ports");

        handled = rules->TryLookup(7, 120, 2, shortestPorts, nonShortestPorts);
        NS_TEST_ASSERT_MSG_EQ(handled, true, "range should own an in-range lookup");
        NS_TEST_ASSERT_MSG_EQ((shortestPorts == std::vector<uint16_t>({2, 3})),
                              true,
                              "range ports");

        handled = rules->TryLookup(7, 150, 2, shortestPorts, nonShortestPorts);
        NS_TEST_ASSERT_MSG_EQ(handled, true, "exact rule should override range");
        NS_TEST_ASSERT_MSG_EQ((shortestPorts == std::vector<uint16_t>({9})),
                              true,
                              "exact range override");

        Ptr<UbRoutingProcess> routing = CreateObject<UbRoutingProcess>();
        routing->SetNodeId(7);
        routing->SetRuleRoutingTable(rules);
        routing->AddShortestRoute(utils::NodeIdToIp(43).Get(), {6});

        RoutingKey key{};
        key.sip = utils::NodeIdToIp(1).Get();
        key.dip = utils::NodeIdToIp(42, 2).Get();
        key.routingType = RoutingType::PER_FLOW_SHORTEST_PATHS;
        bool selectedShortestPath = false;
        NS_TEST_ASSERT_MSG_EQ(routing->GetOutPort(key, selectedShortestPath),
                              9,
                              "rule source should feed the existing selector");

        key.dip = utils::NodeIdToIp(43).Get();
        NS_TEST_ASSERT_MSG_EQ(routing->GetOutPort(key, selectedShortestPath),
                              6,
                              "rule miss should preserve CSV table fallback");
    }
};

class UbNextHopIndexTest : public TestCase
{
  public:
    UbNextHopIndexTest()
        : TestCase("Packed next-hop index preserves complete table candidates")
    {
    }

    void DoRun() override
    {
        const std::vector<utils::UbStructuredTableRoute> routes = {
            {7, 42, 2, {3, 2}, {4}},
            {7, 42, 3, {5}, {6}},
            {7, 42, 2, {7}, {8}},
        };
        const auto index = UbNextHopIndex::Build(routes, 44);
        NS_TEST_ASSERT_MSG_EQ(index->GetPortIdWidthBits(), 8u, "small IDs should use 8 bits");

        std::vector<uint16_t> shortestPorts;
        std::vector<uint16_t> nonShortestPorts;
        bool handled = index->TryLookup(7, 42, 2, shortestPorts, nonShortestPorts);
        NS_TEST_ASSERT_MSG_EQ(handled, true, "exact destination port should be indexed");
        NS_TEST_ASSERT_MSG_EQ((shortestPorts == std::vector<uint16_t>({2, 3, 7})),
                              true,
                              "duplicate rows should merge shortest candidates");
        NS_TEST_ASSERT_MSG_EQ((nonShortestPorts == std::vector<uint16_t>({4, 8})),
                              true,
                              "duplicate rows should merge non-shortest candidates");

        handled = index->TryLookup(7, 42, 10, shortestPorts, nonShortestPorts);
        NS_TEST_ASSERT_MSG_EQ(handled, false, "exact lookup must not hide a missing port");

        handled =
            index->TryLookup(7, 42, UbRuleDestination::ANY_PORT, shortestPorts, nonShortestPorts);
        NS_TEST_ASSERT_MSG_EQ(handled, true, "primary address should aggregate destination ports");
        NS_TEST_ASSERT_MSG_EQ((shortestPorts == std::vector<uint16_t>({2, 3, 5, 7})),
                              true,
                              "primary address should aggregate shortest candidates");

        Ptr<UbRoutingProcess> routing = CreateObject<UbRoutingProcess>();
        routing->SetNodeId(7);
        routing->SetNextHopIndex(index);
        routing->GetShortestOutPorts(utils::NodeIdToIp(42, 2).Get(), shortestPorts);
        NS_TEST_ASSERT_MSG_EQ((shortestPorts == std::vector<uint16_t>({2, 3, 7})),
                              true,
                              "direct lookup should use the packed exact index");

        routing->GetShortestOutPorts(utils::NodeIdToIp(42, 10).Get(), shortestPorts);
        NS_TEST_ASSERT_MSG_EQ(shortestPorts.empty(),
                              true,
                              "direct lookup should not hide a missing destination port");

        RoutingKey key{};
        key.sip = utils::NodeIdToIp(1).Get();
        key.dip = utils::NodeIdToIp(42, 10).Get();
        key.routingType = RoutingType::PER_FLOW_SHORTEST_PATHS;
        bool selectedShortestPath = false;
        const int outPort = routing->GetOutPort(key, selectedShortestPath);
        const bool foundOutPort = outPort >= 0;
        const bool usedAggregatedShortestPort =
            outPort == 2 || outPort == 3 || outPort == 5 || outPort == 7;
        NS_TEST_ASSERT_MSG_EQ(foundOutPort,
                              true,
                              "packet lookup should preserve primary-address fallback");
        NS_TEST_ASSERT_MSG_EQ(usedAggregatedShortestPort,
                              true,
                              "primary-address fallback should use aggregated shortest candidates");
    }
};

class UbStructuredTopologyInstallTest : public TestCase
{
  public:
    UbStructuredTopologyInstallTest()
        : TestCase("Prepared structured protocol installs topology and routing")
    {
    }

    void DoRun() override
    {
        Config::Reset();
        Time::SetResolution(Time::PS);
        std::stringstream payload;
        payload << "NS3_UB_STRUCTURED 3\n"
                << "NODES 2\n"
                << "0 DEVICE 1 1ns 2ns 0\n"
                << "1 DEVICE 1 1ns 2ns 0\n"
                << "LINKS 1\n"
                << "0 0 1 0 400Gbps 10ns\n"
                << "TABLE 1\n"
                << "0 1 0 1 0 0\n"
                << "PORT_GROUPS 1\n"
                << "1 0 1 0\n"
                << "RULES 1\n"
                << "1 0 0 1 0 0\n"
                << "RULE_RANGES 0\n"
                << "TOPOLOGY_RULES 1\n"
                << "dimension 1 2 1 fullMesh 2\n"
                << "0 HOST 1 0\n"
                << "1 HOST 1 1\n"
                << "END\n";

        const auto config = utils::UbUtils::ReadStructuredTopologyConfig(payload);
        NS_TEST_ASSERT_MSG_EQ(config.nodes.size(), 2u, "parser should retain both nodes");
        NS_TEST_ASSERT_MSG_EQ(config.tableRouteCount, 1u, "parser should count table routes");
        NS_TEST_ASSERT_MSG_EQ((config.tableNextHopIndex != nullptr),
                              true,
                              "table should compile while reading");
        NS_TEST_ASSERT_MSG_EQ(config.topologyRules.size(),
                              1u,
                              "parser should retain topology rules");

        utils::UbUtils::Get()->InstallStructuredTopology(config, Time(0), 1);
        const auto tablePorts =
            NodeList::GetNode(0)->GetObject<UbSwitch>()->GetRoutingProcess()->GetAllOutPorts(
                utils::NodeIdToIp(1, 0).Get());
        const auto rulePorts =
            NodeList::GetNode(1)->GetObject<UbSwitch>()->GetRoutingProcess()->GetAllOutPorts(
                utils::NodeIdToIp(0, 0).Get());
        NS_TEST_ASSERT_MSG_EQ((tablePorts == std::vector<uint16_t>({0})),
                              true,
                              "table route should resolve");
        NS_TEST_ASSERT_MSG_EQ((rulePorts == std::vector<uint16_t>({0})),
                              true,
                              "rule route should resolve");

        Simulator::Destroy();
        Config::Reset();
    }
};

#ifndef _WIN32
class UbStructuredTopologyRejectsInvalidInputTest : public TestCase
{
  public:
    UbStructuredTopologyRejectsInvalidInputTest()
        : TestCase("Prepared structured protocol rejects invalid input")
    {
    }

    void DoRun() override
    {
        int status = RunStructuredTestInChild([]() {
            std::stringstream payload("NOT_STRUCTURED 1\n");
            (void)utils::UbUtils::ReadStructuredTopologyConfig(payload);
        });
        NS_TEST_ASSERT_MSG_EQ(WIFSIGNALED(status), 1, "invalid header should abort");
        NS_TEST_ASSERT_MSG_EQ(WTERMSIG(status), SIGABRT, "invalid header should use SIGABRT");

        status = RunStructuredTestInChild([]() {
            std::stringstream payload;
            payload << "NS3_UB_STRUCTURED 1\n"
                    << "NODES 2\n"
                    << "0 DEVICE 1 1ns 0ns 0\n"
                    << "1 DEVICE 1 1ns 0ns 0\n"
                    << "LINKS 1\n"
                    << "0 1 1 0 400Gbps 10ns\n"
                    << "TABLE 0\n"
                    << "PORT_GROUPS 0\n"
                    << "RULES 0\n"
                    << "END\n";
            const auto config = utils::UbUtils::ReadStructuredTopologyConfig(payload);
            utils::UbUtils::Get()->InstallStructuredTopology(config, Time(0), 1);
        });
        NS_TEST_ASSERT_MSG_EQ(WIFSIGNALED(status), 1, "invalid physical port should abort");
        NS_TEST_ASSERT_MSG_EQ(WTERMSIG(status), SIGABRT, "invalid port should use SIGABRT");
    }
};
#endif

class UbStructuredRoutingTestSuite : public TestSuite
{
  public:
    UbStructuredRoutingTestSuite()
        : TestSuite("unified-bus-structured-routing", Type::UNIT)
    {
        AddTestCase(new UbRuleRoutingTableTest(), TestCase::Duration::QUICK);
        AddTestCase(new UbNextHopIndexTest(), TestCase::Duration::QUICK);
        AddTestCase(new UbStructuredTopologyInstallTest(), TestCase::Duration::QUICK);
#ifndef _WIN32
        AddTestCase(new UbStructuredTopologyRejectsInvalidInputTest(), TestCase::Duration::QUICK);
#endif
    }
};

static UbStructuredRoutingTestSuite g_ubStructuredRoutingTestSuite;

} // namespace
