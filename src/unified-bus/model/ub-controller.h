// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_CONTROL_H
#define UB_CONTROL_H

#include <ns3/core-module.h>
#include <vector>
#include <map>
#include <unordered_map>
#include "ub-datatype.h"
#include "ns3/callback.h"
#include "ns3/ub-port.h"
#include "ns3/ub-switch-allocator.h"
#include "protocol/ub-function.h"
#include "protocol/ub-ctp.h"
#include "ns3/ub-tp-connection-manager.h"
#include "ns3/ub-transport-channel-manager.h"


using namespace utils;
namespace ns3 {

class UbLdstApi;
class UbFunction;
class UbTransaction;
class UbTransportChannel;
class UbTransportGroup;
class UbPort;

/**
 * @brief UB Management Unit
 */
class UbController : public Object {
public:
    static TypeId GetTypeId();

    UbController();
    ~UbController();

    Ptr<UbTransportChannelManager> GetTransportChannelManager() const;

    Ptr<UbTransportGroup> CreateRtpTpGroup(
        const std::vector<Ptr<UbTransportChannel>>& members,
        uint32_t initialSelectionCursor);
    Ptr<UbTransportGroup> ResolveRtpTpGroup(
        const UbLifecycleResourceIdentity& identity) const;
    Ptr<UbTransportGroup> ResolveRtpTpGroupForMember(
        const UbLifecycleResourceIdentity& tpIdentity) const;
    UbTpgMemberMutationStatus AddRtpTpGroupMember(
        const UbLifecycleResourceIdentity& groupIdentity,
        const UbLifecycleResourceIdentity& tpIdentity);
    UbTpgMemberMutationStatus RemoveRtpTpGroupMember(
        const UbLifecycleResourceIdentity& groupIdentity,
        const UbLifecycleResourceIdentity& tpIdentity,
        UbDestroyReason reason);
    UbDestroyStatus DestroyRtpTpGroup(
        const UbLifecycleResourceIdentity& groupIdentity,
        UbDestroyReason reason = UbDestroyReason::EXPLICIT);
    void NotifyRtpTpGroupUseLeaseReleased(
        const UbLifecycleResourceIdentity& groupIdentity);

    // Port management
    /**
     * @brief Associates a port with a reachable destination.
     * @param port The source port.
     * @param destination The reachable destination address.
     */
    void AddPortDestination(Ptr<UbPort> port, uint32_t destination);

    /**
     * @brief Removes an association between a port and a destination.
     * @param port The source port.
     * @param destination The destination address to remove.
     */
    void RemovePortDestination(Ptr<UbPort> port, uint32_t destination);

    /**
     * @brief Gets all available ports for a given destination.
     * @param destination The destination address to look up.
     * @return A vector of ports that can reach the destination. Returns an empty vector if none found.
     */
    std::vector<Ptr<UbPort>> GetAvailablePorts(uint32_t destination) const;

    Ptr<UbFunction> GetUbFunction();

    Ptr<UbTransaction> GetUbTransaction();

    Ptr<UbCtpTransportService> GetCtpTransportService();
    Ptr<UbCtpTransportService> PeekCtpTransportService() const;
    using CtpPacketTimingCallback = Callback<void,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t,
                                             uint32_t>;
    void SetCtpPacketTimingTraceCallbacks(CtpPacketTimingCallback firstPacketSendsCallback,
                                          CtpPacketTimingCallback lastPacketAcksCallback);

    /**
     * @brief 1.每个controller创建一个Function
     */
    void CreateUbFunction();

    /**
     * @brief 2.每个controller创建一个Transaction
     */
    void CreateUbTransaction();

    void SetTpConnManager(Ptr<TpConnectionManager> conn)
    {
        m_tpnConn = conn;
    }
    Ptr<TpConnectionManager> GetTpConnManager() { return m_tpnConn; }

    uint32_t GetActiveSenderFlowCount() const { return m_activeSenderFlowCount; }
    bool IsDisposing() const { return m_disposing; }
    void IncrementActiveSenderFlowCount() { ++m_activeSenderFlowCount; }
    void DecrementActiveSenderFlowCount()
    {
        if (m_activeSenderFlowCount > 0)
        {
            --m_activeSenderFlowCount;
        }
    }
private:
  void DoDispose() override;
  void FinalizeRtpTpGroupMemberRemoval(Ptr<UbTransportGroup> group, uint32_t index);
  void FinalizeRtpTpGroupDestroy(Ptr<UbTransportGroup> group);

  Ptr<UbFunction> m_function;       // 功能层
  Ptr<UbTransaction> m_transaction; // 事务层
  Ptr<UbCtpTransportService> m_ctpTransportService;
  CtpPacketTimingCallback m_ctpFirstPacketSendsCallback;
  CtpPacketTimingCallback m_ctpLastPacketAcksCallback;
  // Resource storage
  Ptr<UbTransportChannelManager> m_transportChannelManager;
  uint64_t m_nextRtpTpGroupId{0};
  std::map<uint64_t, Ptr<UbTransportGroup>> m_rtpTpGroups{};
  std::map<uint32_t, uint64_t> m_rtpTpGroupByTpn{};
  std::unordered_map<uint32_t, std::vector<Ptr<UbPort>>> m_destinationToPortsMap{};

  Ptr<TpConnectionManager> m_tpnConn; // 当前节点维护的tpnConn
  uint32_t m_activeSenderFlowCount{0};
  bool m_disposing{false};
};

} // namespace ns3

#endif /* UB_CONTROL_H */
