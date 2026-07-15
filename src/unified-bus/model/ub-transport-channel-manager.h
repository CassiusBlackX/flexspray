// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_TRANSPORT_CHANNEL_MANAGER_H
#define UB_TRANSPORT_CHANNEL_MANAGER_H

#include "ub-lifecycle.h"
#include "ub-datatype.h"

#include "ns3/ipv4-address.h"
#include "ns3/object.h"

#include <map>
#include <vector>

namespace ns3
{

class UbCongestionControl;
class UbController;
class UbTransportChannel;

class UbTransportChannelManager : public Object
{
  public:
    static TypeId GetTypeId();

    void Init(UbController* controller);
    bool CreateTp(uint32_t src,
                  uint32_t dest,
                  uint8_t sport,
                  uint8_t dport,
                  UbPriority priority,
                  uint32_t srcTpn,
                  uint32_t dstTpn,
                  Ptr<UbCongestionControl> congestionCtrl,
                  bool drainOnly = false);
    Ptr<UbTransportChannel> GetTp(uint32_t tpn) const;
    Ptr<UbTransportChannel> CreateReservedTpEndpoint(uint32_t tpn);
    Ptr<UbTransportChannel> ResolveInboundTp(uint32_t srcTpn,
                                             uint32_t dstTpn,
                                             Ipv4Address sourceIp,
                                             Ipv4Address destinationIp,
                                             UbPriority priority);
    UbDestroyStatus DestroyTp(uint32_t tpn,
                              UbDestroyReason reason = UbDestroyReason::EXPLICIT,
                              UbDestroyCallback callback = {});
    void NotifyStateChanged(uint32_t tpn);
    void ForceDispose();

    bool Contains(uint32_t tpn) const;
    uint32_t GetCount() const;
    std::map<uint32_t, Ptr<UbTransportChannel>> GetTpnMap() const;

  private:
    enum class EndpointState : uint8_t
    {
        ACTIVE,
        DRAINING,
    };

    struct EndpointRecord
    {
        Ptr<UbTransportChannel> channel;
        EndpointState state{EndpointState::ACTIVE};
        UbDestroyReason reason{UbDestroyReason::EXPLICIT};
        std::vector<UbDestroyCallback> callbacks;
    };

    Ptr<UbTransportChannel> CreateReservedTpEndpointLocked(uint32_t tpn, bool drainOnly = false);
    void BeginDrainOnlyDestroy(uint32_t tpn);
    bool CanFinalize(const EndpointRecord& record) const;
    void FinalizeDestroy(uint32_t tpn, bool force);
    void NotifyLifecycleStateChanged(UbLifecycleResourceIdentity identity);

    UbController* m_controller{nullptr};
    std::map<uint32_t, EndpointRecord> m_endpoints;
    uint32_t m_transportsCount{0};
};

} // namespace ns3

#endif // UB_TRANSPORT_CHANNEL_MANAGER_H
