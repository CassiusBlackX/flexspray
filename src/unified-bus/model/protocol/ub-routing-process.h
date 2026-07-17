// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_ROUTING_PROCESS_H
#define UB_ROUTING_PROCESS_H

#include "ns3/ub-datatype.h"
#include "ns3/node.h"
#include "ns3/ub-routing-table.h"
#include "ns3/ub-routing-policy.h"

#include <array>
#include <unordered_map>
#include <vector>

namespace ns3 {

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
    UbRoutingTable& GetRoutingTable();
    const UbRoutingTable& GetRoutingTable() const;

    int GetOutPort(RoutingKey& key,
                   bool& selectedShortestPath,
                   uint16_t inPort = UINT16_MAX);
    int SelectOutPort(RoutingKey& key,
                      const std::vector<uint16_t>& shortestPorts,
                      const std::vector<uint16_t>& nonShortestPorts,
                      bool& selectedShortestPath,
                      uint16_t inPort = UINT16_MAX);
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
    std::unordered_map<std::vector<uint16_t>, uint64_t, VectorHash> m_rrNextIndexByCandidates;

    UbRoutingTable::PortSetView LookupCandidatePorts(uint32_t& dip, bool shortest) const;

    uint32_t m_nodeId{0};
    MultipathSelector m_multipathSelector{MultipathSelector::HASH64};
    UbRoutingTable m_table;
    uint64_t m_hotPathLookups{0};
    uint64_t m_hotPathTemporaryContainerAllocations{0};
};

} // namespace ns3

#endif // UB_ROUTING_PROCESS_H
