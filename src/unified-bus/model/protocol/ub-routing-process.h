// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_ROUTING_PROCESS_H
#define UB_ROUTING_PROCESS_H

#include "ns3/node.h"
#include "ns3/ub-routing-table.h"

#include <vector>

namespace ns3 {

struct RoutingKey
{
    uint32_t sip;
    uint32_t dip;
    uint16_t sport;
    uint16_t dport;
    uint8_t priority;
    bool useShortestPath;
    bool usePacketSpray;
    bool hashIncludesTransportPorts{false};
};

class UbRoutingProcess : public Object
{
  public:
    UbRoutingProcess();
    ~UbRoutingProcess() override = default;
    static TypeId GetTypeId();
    void SetNodeId(uint32_t nodeId);

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

    void GetShortestOutPorts(uint32_t destIp, std::vector<uint16_t>& outPorts);
    void GetOtherOutPorts(uint32_t destIp, std::vector<uint16_t>& outPorts);
    const std::vector<uint16_t> GetAllOutPorts(uint32_t destIp);
    void GetShortestCandidates(uint32_t& dip,
                               uint16_t inPortId,
                               std::vector<uint16_t>& outPorts);
    void GetNonShortestCandidates(uint32_t& dip,
                                  uint16_t inPortId,
                                  std::vector<uint16_t>& outPorts);

    int GetOutPort(RoutingKey& key,
                   bool& selectedShortestPath,
                   uint16_t inPort = UINT16_MAX);
    int SelectOutPort(RoutingKey& key,
                      const std::vector<uint16_t>& shortestPorts,
                      const std::vector<uint16_t>& nonShortestPorts,
                      bool& selectedShortestPath);
    int SelectAdaptiveOutPort(RoutingKey& key,
                              const std::vector<uint16_t>& shortestPorts,
                              const std::vector<uint16_t>& nonShortestPorts,
                              bool& selectedShortestPath);

    bool RemoveShortestRoute(uint32_t destIp);
    bool RemoveOtherRoute(uint32_t destIp);

    uint64_t GetHotPathLookupCountForTest() const;
    uint64_t GetHotPathTemporaryContainerAllocationCountForTest() const;

  private:
    enum class UbRoutingAlgorithm : uint8_t
    {
        HASH = 0,
        ADAPTIVE = 1,
    };

    UbRoutingTable::PortSetView LookupCandidatePorts(uint32_t& dip, bool shortest) const;

    uint32_t m_nodeId{0};
    UbRoutingAlgorithm m_routingAlgorithm{UbRoutingAlgorithm::HASH};
    UbRoutingTable m_table;
    uint64_t m_hotPathLookups{0};
    uint64_t m_hotPathTemporaryContainerAllocations{0};
};

} // namespace ns3

#endif // UB_ROUTING_PROCESS_H
