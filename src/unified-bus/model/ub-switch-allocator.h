// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_UBSWITCH_ALLOCATOR_H
#define UB_UBSWITCH_ALLOCATOR_H

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ns3/event-id.h"
#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/node.h"

namespace ns3 {

class UbSwitch;
class UbIngressQueue;
class UbPort;
class UbTransportChannelManager;

// outport, priority, voq/TpChannel/ctrlq
typedef std::vector<std::vector<std::vector<Ptr<UbIngressQueue> > > > IngressSource_t;
typedef std::vector<bool> EgressStatus_t;

/**
 * @brief 交换机调度器基类
 */
class UbSwitchAllocator : public Object {
public:
    UbSwitchAllocator();
    virtual ~UbSwitchAllocator();
    static TypeId GetTypeId (void);
    void TriggerAllocator(Ptr<UbPort> outPort);
    void ScheduleTriggerAllocator(Ptr<UbPort> outPort);
    virtual void AllocateNextPacket(Ptr<UbPort> outPort);
    virtual void Init();
    void SetNodeId(uint32_t nodeId) {m_nodeId = nodeId;}
    void RegisterUbIngressQueue(Ptr<UbIngressQueue> ingressQueue, uint32_t outPort, uint32_t priority);
    void UnregisterUbIngressQueue(Ptr<UbIngressQueue> ingressQueue, uint32_t outPort, uint32_t priority);
    Ptr<UbIngressQueue> ReplaceUbIngressQueueWithEmptyPlaceholder(Ptr<UbIngressQueue> ingressQueue);
    bool ReplaceUbIngressQueue(Ptr<UbIngressQueue> expected, Ptr<UbIngressQueue> replacement);
    void RegisterEgressStauts(uint32_t portsNum);
    void SetEgressStatus(uint32_t portId, bool status);
    bool GetEgressStatus(uint32_t portId);
    void DoDispose() override;
    void CheckDeadlock();
    bool HasPendingOrActiveWork(Ptr<UbIngressQueue> ingressQueue) const;
    uint32_t GetPendingEventCountForTest() const;
    uint32_t GetForcedEventResidueForTest() const { return m_forcedEventResidue; }

protected:
    struct IngressCandidate
    {
        Ptr<UbIngressQueue> queue;
        uint32_t ingressQueueSlot;
    };

    struct PortEventState
    {
        EventId allocation;
        bool allocationPending{false};
        EventId retrigger;
        bool retriggerPending{false};
        std::unordered_map<uint64_t, EventId> notifications;
        uint32_t schedulingWorkCount{0};
        Ptr<UbIngressQueue> activeIngressQueue;
    };

    uint32_t GetIngressQueueSlotCount(uint32_t outPort, uint32_t priority) const;
    std::optional<IngressCandidate> FindNextEligibleIngressQueue(uint32_t outPort,
                                                                 uint32_t priority,
                                                                 uint32_t startIngressQueueSlot,
                                                                 Ptr<UbPort> egressPort) const;
    void InitializePortEventState(uint32_t portsNum);
    void ScheduleAllocation(Ptr<UbPort> outPort);
    void ScheduleOwnedRetrigger(uint32_t outPortId, Ptr<UbPort> outPort);
    void SchedulePortNotification(Ptr<UbPort> outPort);
    void ScheduleDeadlockCheck();
    void RunAllocation(Ptr<UbPort> outPort);
    void RunOwnedRetrigger(uint32_t outPortId, Ptr<UbPort> outPort);
    void RunPortNotification(uint32_t outPortId, uint64_t token, Ptr<UbPort> outPort);
    void RunDeadlockCheck();
    void FinalizeDisposeStorage();
    void NotifyAllocationFinish(Ptr<UbPort> outPort);
    void TryReclaimReleasedTp(Ptr<UbIngressQueue> ingressQueue);
    void SetActiveIngressQueue(Ptr<UbIngressQueue> ingressQueue);
    void ClearActiveIngressQueue(uint32_t outPortId);

    Time m_allocationTime;
    uint32_t m_nodeId;
    IngressSource_t m_ingressSources;
    EgressStatus_t m_egressStatus;
    std::vector<bool> m_isRunning;	
    std::vector<bool> m_oneMoreRound;
    uint32_t m_voqIngressSlotCount = 0;
    bool m_enableDirectAllocationCompletion = true;
    std::vector<PortEventState> m_portEventState;
    EventId m_deadlockEvent;
    bool m_deadlockPending{false};
    uint64_t m_nextEventToken{0};
    uint32_t m_forcedEventResidue = 0;
    uint32_t m_runningAllocationCallbacks{0};
    bool m_disposed{false};
    bool m_deferredDisposeCleanup{false};
    Ptr<UbTransportChannelManager> m_transportChannelManager;
};


/**
 * @brief 轮询算法的交换机调度器
 */
class UbRoundRobinAllocator : public UbSwitchAllocator {
public:
    UbRoundRobinAllocator() {}
    virtual ~UbRoundRobinAllocator() {}
    static TypeId GetTypeId(void);

    virtual void Init() override;
    Ptr<UbIngressQueue> SelectNextIngressQueue(Ptr<UbPort> outPort);
    virtual void AllocateNextPacket(Ptr<UbPort> outPort) override;

private:
    std::vector<std::vector<uint32_t> > m_rrIdx;
    std::vector<std::vector<bool> > m_rrPhaseSeeded;
};


/**
 * @brief 基于 DWRR 的 VL 间调度器
 *
 *  - 每个 [outPort][priority(VL)] 维护：
 *      - m_quantum
 *      - m_deficit  
 *  - m_currVlIdx[outPort]：当前在 VL 间 DWRR 轮询到的 VLAN
 *  - 相同 VLAN 内多个 IngressQueue 按 RR 轮询（m_rrIdx）
 */
class UbDwrrAllocator : public UbSwitchAllocator {
public:
    UbDwrrAllocator() {}
    virtual ~UbDwrrAllocator() {}
    static TypeId GetTypeId(void);

    virtual void Init() override;
    Ptr<UbIngressQueue> SelectNextIngressQueue(Ptr<UbPort> outPort);
    virtual void AllocateNextPacket(Ptr<UbPort> outPort) override;

    void SetQuantum(uint32_t priority, uint32_t quantum);
    void SetQuantum(uint32_t outPort, uint32_t priority, uint32_t quantum);

private:
    std::vector<std::vector<uint32_t> > m_rrIdx;
    std::vector<std::vector<bool> > m_rrPhaseSeeded;
    std::vector<std::vector<uint32_t> > m_quantum;
    std::vector<std::vector<uint32_t> > m_deficit;
    std::vector<uint32_t> m_currVlIdx;

    // 记录最近一次在某个 VLAN 下选中的 ingress queue slot，用于额度不足时回滚 m_rrIdx
    std::vector<std::vector<uint32_t> > m_lastSelectedIngressQueueSlot;

    uint32_t    m_defaultQuantum;
    std::string m_vlQuantumsStr;
    void ApplyDefaultQuantum();
    void ParseAndApplyVlQuantums(const std::string& s);
};

} /* namespace ns3 */

#endif /* UB_UbSwitchAllocator_H */
