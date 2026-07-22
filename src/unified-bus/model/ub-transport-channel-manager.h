// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_TRANSPORT_CHANNEL_MANAGER_H
#define UB_TRANSPORT_CHANNEL_MANAGER_H

#include "protocol/ub-transport.h"
#include "ub-datatype.h"
#include "ub-lifecycle.h"

#include "ns3/ipv4-address.h"
#include "ns3/object.h"

#include <map>
#include <vector>

namespace ns3
{

class UbCongestionControl;
class UbController;
class UbIngressQueue;

struct UbTransportEndpointStorageSnapshot
{
    uint64_t liveBackingObjects{0};
    uint64_t dormantRecords{0};
    uint64_t backingRehydrations{0};
};

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
    bool TryGetEndpointIdentity(uint32_t tpn, UbLifecycleResourceIdentity& identity) const;
    Ptr<UbTransportChannel> CreateReservedTpEndpoint(uint32_t tpn);
    Ptr<UbTransportChannel> ResolveInboundTp(uint32_t srcTpn,
                                             uint32_t dstTpn,
                                             Ipv4Address sourceIp,
                                             Ipv4Address destinationIp,
                                             UbPriority priority);
    bool ReclaimBackingState(uint32_t tpn);
    Ptr<UbTransportChannel> ResolveLiveTp(uint32_t tpn,
                                          const UbLifecycleResourceIdentity& expectedIdentity);
    UbDestroyStatus DestroyTp(uint32_t tpn,
                              UbDestroyReason reason = UbDestroyReason::EXPLICIT,
                              UbDestroyCallback callback = {});
    void NotifyStateChanged(uint32_t tpn);
    void ForceDispose();

    bool Contains(uint32_t tpn) const;
    uint32_t GetCount() const;
    std::map<uint32_t, Ptr<UbTransportChannel>> GetTpnMap() const;
    UbTransportEndpointStorageSnapshot GetStorageSnapshot() const;

  private:
    enum class EndpointState : uint8_t
    {
        LIVE,
        DORMANT,
        DRAINING,
    };

    struct EndpointRecord
    {
        Ptr<UbTransportChannel> channel;
        std::unique_ptr<UbTransportDormantState> dormantState;
        Ptr<UbIngressQueue> allocatorPlaceholder;
        EndpointState state{EndpointState::LIVE};
        UbDestroyReason reason{UbDestroyReason::EXPLICIT};
        std::vector<UbDestroyCallback> callbacks;
    };

    Ptr<UbTransportChannel> CreateReservedTpEndpointLocked(uint32_t tpn, bool drainOnly = false);
    bool ReclaimBackingStateLocked(uint32_t tpn);
    Ptr<UbTransportChannel> ResolveLiveTpLocked(
        uint32_t tpn,
        const UbLifecycleResourceIdentity& expectedIdentity);
    bool TryGetEndpointIdentityLocked(uint32_t tpn, UbLifecycleResourceIdentity& identity) const;
    void BeginDrainOnlyDestroy(uint32_t tpn);
    bool CanFinalize(const EndpointRecord& record) const;
    void FinalizeDestroy(uint32_t tpn, bool force);
    void NotifyLifecycleStateChanged(UbLifecycleResourceIdentity identity);

    UbController* m_controller{nullptr};
    std::map<uint32_t, EndpointRecord> m_endpoints;
    uint32_t m_transportsCount{0};
    uint64_t m_backingRehydrations{0};
};

} // namespace ns3

#endif // UB_TRANSPORT_CHANNEL_MANAGER_H
