// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_ROUTING_PROCESS_H
#define UB_ROUTING_PROCESS_H

#include "ns3/ub-datatype.h"
#include "ns3/node.h"
#include "ns3/ub-routing-table.h"
#include "ns3/ub-routing-policy.h"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ns3 {

class UbNextHopIndex;
class UbRuleRoutingTable;

struct RoutingKey
{
    uint32_t sip;
    uint32_t dip;
    uint16_t sport;
    uint16_t dport;
    uint8_t priority;
    RoutingType routingType{RoutingType::PER_FLOW_SHORTEST_PATHS};
};

class UbRoutingProcess : public Object
{
  public:
    UbRoutingProcess();
    ~UbRoutingProcess() override = default;
    static TypeId GetTypeId();
    void SetNodeId(uint32_t nodeId);
    void SetNextHopIndex(std::shared_ptr<const UbNextHopIndex> nextHopIndex);
    void SetRuleRoutingTable(std::shared_ptr<const UbRuleRoutingTable> ruleRoutingTable);
    UbRoutingTable& GetRoutingTable();
    const UbRoutingTable& GetRoutingTable() const;

    // Compatibility surface for callers that predate UbRoutingTable ownership.
    void AddShortestRoute(uint32_t destIp, const std::vector<uint16_t>& outPorts);
    void AddOtherRoute(uint32_t destIp, const std::vector<uint16_t>& outPorts);
    void AddShortestRouteRange(uint32_t startNodeId,
                               uint32_t endNodeId,
                               const std::vector<uint16_t>& outPorts);
    void AddOtherRouteRange(uint32_t startNodeId,
                            uint32_t endNodeId,
                            const std::vector<uint16_t>& outPorts);
    void AddShortestRouteRange(uint32_t startNodeId,
                               uint32_t endNodeId,
                               uint32_t dstPortId,
                               const std::vector<uint16_t>& outPorts);
    void AddOtherRouteRange(uint32_t startNodeId,
                            uint32_t endNodeId,
                            uint32_t dstPortId,
                            const std::vector<uint16_t>& outPorts);
    void GetShortestOutPorts(uint32_t destIp, std::vector<uint16_t>& outPorts) const;
    void GetOtherOutPorts(uint32_t destIp, std::vector<uint16_t>& outPorts) const;
    std::vector<uint16_t> GetAllOutPorts(uint32_t destIp) const;
    void GetShortestCandidates(uint32_t& dip,
                               uint16_t inPort,
                               std::vector<uint16_t>& outPorts) const;
    void GetNonShortestCandidates(uint32_t& dip,
                                  uint16_t inPort,
                                  std::vector<uint16_t>& outPorts) const;
    bool TryGetRuleCandidates(uint32_t dip,
                              std::vector<uint16_t>& shortestPorts,
                              std::vector<uint16_t>& nonShortestPorts) const;
    bool TryGetRuleCandidatesWithPrimaryFallback(
        uint32_t dip,
        uint16_t inPort,
        std::vector<uint16_t>& shortestPorts,
        std::vector<uint16_t>& nonShortestPorts) const;
    bool RemoveShortestRoute(uint32_t destIp);
    bool RemoveOtherRoute(uint32_t destIp);

    int GetOutPort(RoutingKey& key,
                   bool& selectedShortestPath,
                   uint16_t inPort = UINT16_MAX);
    int SelectOutPort(RoutingKey& key,
                      const std::vector<uint16_t>& shortestPorts,
                      const std::vector<uint16_t>& nonShortestPorts,
                      bool& selectedShortestPath,
                      uint16_t inPort = UINT16_MAX);
    int SelectAdaptiveOutPort(RoutingKey& key,
                              const std::vector<uint16_t>& shortestPorts,
                              const std::vector<uint16_t>& nonShortestPorts,
                              bool& selectedShortestPath);
    void ValidateRoutingType(RoutingType routingType, const std::string& owner) const;
    uint64_t GetHotPathLookupCountForTest() const;
    uint64_t GetHotPathTemporaryContainerAllocationCountForTest() const;

  private:
    struct VectorHash {
        size_t operator()(const std::vector<uint16_t>& v) const
        {
            std::string hashKey;
            for (int i : v) {
                hashKey += std::to_string(i);
            }
            return Hash64(hashKey);
        }
    };

    std::array<uint8_t, 17> BuildHashBytes(const RoutingKey& rtKey) const;
    uint32_t CalcCrc32(const std::array<uint8_t, 17>& bytes) const;
    uint32_t CalcToeplitzHash(const std::array<uint8_t, 17>& bytes) const;
    int SelectCandidate(RoutingKey& key,
                        const UbRoutingCandidateView& candidates,
                        bool& selectedShortestPath,
                        uint16_t inPort);
    bool TryGetIndexedCandidates(uint32_t dip,
                                 std::vector<uint16_t>& shortestPorts,
                                 std::vector<uint16_t>& nonShortestPorts) const;
    bool TryGetIndexedCandidatesWithPrimaryFallback(
        uint32_t dip,
        uint16_t inPort,
        std::vector<uint16_t>& shortestPorts,
        std::vector<uint16_t>& nonShortestPorts) const;
    std::unordered_map<std::vector<uint16_t>, uint64_t, VectorHash> m_rrNextIndexByCandidates;

    UbRoutingTable::PortSetView LookupCandidatePorts(uint32_t& dip, bool shortest) const;

    uint32_t m_nodeId{0};
    MultipathSelector m_multipathSelector{MultipathSelector::HASH64};
    UbRoutingTable m_table;
    std::shared_ptr<const UbNextHopIndex> m_nextHopIndex;
    std::shared_ptr<const UbRuleRoutingTable> m_ruleRoutingTable;
    uint64_t m_hotPathLookups{0};
    uint64_t m_hotPathTemporaryContainerAllocations{0};
};

} // namespace ns3

#endif // UB_ROUTING_PROCESS_H
