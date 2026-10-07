// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_CTP_H
#define UB_CTP_H

#include "ns3/callback.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/simple-ref-count.h"
#include "ns3/traced-callback.h"
#include "ns3/ub-datatype.h"
#include "ns3/ub-lifecycle.h"
#include "ns3/ub-queue-manager.h"
#include "ns3/ub-tag.h"
#include "ub-transaction.h"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <tuple>
#include <vector>

namespace ns3
{

class UbCna16NetworkHeader;
class UbCompactAckTransactionHeader;
class UbCompactMAExtTah;
class UbCompactTransactionHeader;
class UbJetty;
class UbRoutingProcess;
class UbTransaction;
class UbWqeSegment;
struct RoutingKey;

struct UbCtpCongestionKey
{
    uint32_t dstNodeId{0};
    uint32_t dstEntityId{0};
    uint8_t vl{0};

    bool operator<(const UbCtpCongestionKey& other) const;
};

struct UbCtpRoutingPolicy
{
    RoutingType routingType{RoutingType::PER_FLOW_SHORTEST_PATHS};
    uint32_t nextSprayIndex{0};
};

struct UbCtpTransportConstraints
{
    uint32_t maxSegmentBytes{UB_MTU_BYTE};
    bool onePacketPerSegment{true};
};

struct UbCtpTxState
{
    std::vector<uint32_t> outPorts;
    UbCtpRoutingPolicy routing;
};

struct UbCtpRxState
{
    uint32_t receivedPackets{0};
    uint32_t generatedResponses{0};
};

class UbCtpEntityState
{
  public:
    explicit UbCtpEntityState(UbCtpEntityKey key = {});
    UbCtpTxState& Tx();
    const UbCtpTxState& Tx() const;
    UbCtpRxState& Rx();
    const UbCtpRxState& Rx() const;
    const UbCtpEntityKey& GetKey() const;
    void AddOutPort(uint32_t outPort);
    const std::vector<uint32_t>& GetOutPorts() const;

  private:
    UbCtpEntityKey m_key;
    UbCtpTxState m_tx;
    UbCtpRxState m_rx;
};

enum class UbCtpQueueClass : uint8_t
{
    DATA = 0,
    ACK = 1,
    CNP = 2,
};

class UbCtpTxQueue : public UbIngressQueue
{
  public:
    static TypeId GetTypeId(void);
    UbCtpTxQueue();
    ~UbCtpTxQueue() override;

    IngressQueueType GetIngressQueueType() override;
    bool IsEmpty() override;
    Ptr<Packet> GetNextPacket() override;
    uint32_t GetNextPacketSize() override;
    void EnqueueData(Ptr<Packet> packet);
    void EnqueueAck(Ptr<Packet> packet);
    void EnqueueCnp(Ptr<Packet> packet);
    void Enqueue(Ptr<Packet> packet);
    Ptr<Packet> PeekNextPacket() const;
    uint32_t GetQueuedPacketCountForTest() const;

  private:
    std::queue<Ptr<Packet>>& QueueForClass(UbCtpQueueClass queueClass);
    const std::queue<Ptr<Packet>>& QueueForClass(UbCtpQueueClass queueClass) const;
    const std::queue<Ptr<Packet>>* GetPriorityQueue() const;
    std::queue<Ptr<Packet>>* GetPriorityQueue();
    void EnqueueToClass(UbCtpQueueClass queueClass, Ptr<Packet> packet);

    std::queue<Ptr<Packet>> m_dataQueue;
    std::queue<Ptr<Packet>> m_ackQueue;
    std::queue<Ptr<Packet>> m_cnpQueue;
};

class UbCtpTransportService : public Object
{
  public:
    static TypeId GetTypeId(void);
    UbCtpTransportService();
    ~UbCtpTransportService() override;

    void SetNode(Ptr<Node> node);
    Ptr<Node> GetNode() const;
    void SetRoutingType(RoutingType routingType);
    void SetBoundOutPortCountForTest(uint32_t boundOutPortCount);
    void SetExperimentTaAckPacketBytesForTest(uint32_t packetBytes);
    UbCtpTransportConstraints GetTransactionConstraints() const;

    UbCtpEntityState& GetOrCreateEntityState(const UbCtpEntityKey& key);
    void ConfigureRoutingPolicy(const UbCtpEntityKey& key, RoutingType routingType);
    uint32_t GetEntityStateCount() const;
    Ptr<UbCtpTransactionContext> GetOrCreateTransactionContext(const UbCtpEntityKey& key);
    bool HasTransactionContextForTesting(const UbCtpEntityKey& key) const;
    Ptr<Packet> BuildDataPacket(Ptr<UbWqeSegment> segment, const UbCtpEntityKey& key);
    Ptr<Packet> BuildResponsePacketForTest(Ptr<UbWqeSegment> response,
                                           const UbCtpEntityKey& key);
    bool HandleReceivedPacket(Ptr<Packet> packet);
    bool HandleReceivedPacket(Ptr<Packet> packet, uint32_t ingressPortId);
    bool SendSegment(Ptr<UbWqeSegment> segment, const UbCtpEntityKey& key);
    UbCtpPreparationResult PrepareJettyWithResult(Ptr<UbJetty> jetty,
                                                  const UbCtpEntityKey& key);
    bool PrepareJetty(Ptr<UbJetty> jetty, const UbCtpEntityKey& key);
    bool HasJettyPreparation(uint32_t jettyNum) const;
    bool CanSubmitWqe(uint32_t jettyNum, Ptr<UbWqe> wqe) const;
    bool CanSubmitWqe(uint32_t jettyNum, Ptr<UbWqe> wqe, const UbCtpEntityKey& key) const;
    bool SubmitPreparedWqe(uint32_t jettyNum, Ptr<UbWqe> wqe);
    bool NotifyJettyWork(uint32_t jettyNum);
    bool UnprepareJetty(uint32_t jettyNum);
    void StartJetty(Ptr<UbJetty> jetty, const UbCtpEntityKey& key);
    void TryContinueJettyTransmission(const UbCtpEntityKey& entityKey);
    void ReleaseJetty(uint32_t jettyNum);
    bool HasJettyBindingForTest(uint32_t jettyNum) const;
    UbCtpEntityKey MakeSourceGroupKeyForRequest(const UbCtpEntityKey& key) const;
    UbCtpEntityKey MakeTxKeyForSegment(const UbCtpEntityKey& jettyKey,
                                       Ptr<UbWqeSegment> segment) const;
    void CompleteFromTaAckForTest(const UbCtpEntityKey& key, uint32_t taSsn);
    void RecordCnpForTest(const UbCtpEntityKey& key);
    uint32_t GetQueueCount() const;
    uint32_t GetCongestionStateCountForTest() const;
    Ptr<UbCtpTxQueue> GetOrCreateQueue(const UbCtpEntityKey& key,
                                       uint32_t outPort,
                                       uint32_t priority);
    void RegisterQueueForTest(Ptr<UbCtpTxQueue> queue,
                              uint32_t outPort,
                              uint32_t priority);
    bool AdmitSegmentForTest(Ptr<UbWqeSegment> segment,
                             const UbCtpEntityKey& key,
                             uint32_t taSsn);
    bool SendResponseSegmentForTest(Ptr<UbWqeSegment> response, const UbCtpEntityKey& key);
    uint32_t GetQueuedPacketCountForTest(const UbCtpEntityKey& key) const;
    uint32_t GetQueuedPacketCountForTest(const UbCtpEntityKey& key,
                                         uint32_t outPort,
                                         uint32_t priority) const;
    uint32_t GetRegisteredQueueCountForTest() const;
    bool IsPacketTraceEnabledForTest() const;
    const UbLifecycleResourceIdentity& GetLifecycleOwnerForTest() const;

  private:
    using QueueKey = std::tuple<UbCtpEntityKey, uint32_t, uint32_t>;

    Ptr<UbTransaction> GetTransactionOwner() const;
    UbCtpTransactionState& GetTransactionState(const UbCtpEntityKey& key) const;
    void ValidateRoutingType(RoutingType routingType, const std::string& owner) const;
    bool TryGetJettyPreparation(uint32_t jettyNum,
                                Ptr<UbJetty>& jetty,
                                UbCtpEntityKey& key) const;

    Ptr<Packet> BuildDataPacketWithTaSsn(Ptr<UbWqeSegment> segment,
                                         UbCtpEntityState& state,
                                         const UbCtpEntityKey& packetKey,
                                         uint32_t taSsn,
                                         uint32_t progressBytes,
                                         uint8_t loadBalanceSalt);
    void PadTaAckPacketForExperiment(Ptr<Packet> packet) const;
    bool SendAdmittedFragment(Ptr<UbWqeSegment> segment,
                              UbCtpEntityState& state,
                              const UbCtpEntityKey& packetKey,
                              uint32_t taSsn,
                              uint32_t progressBytes);
    bool DispatchInboundTransaction(Ptr<UbWqeSegment> request, const UbCtpEntityKey& key);
    Ptr<UbWqeSegment> BuildInboundTaOperation(const UbCompactTransactionHeader& taHeader,
                                              const UbCompactMAExtTah& maHeader,
                                              UbTransactionOpcode taOpcode,
                                              const UbTransactionFields& fields,
                                              bool maetahPresent,
                                              uint32_t srcNodeId,
                                              uint32_t dstNodeId,
                                              uint32_t priority,
                                              uint32_t payloadBytes,
                                              uint32_t flowId,
                                              uint32_t flowSize,
                                              Ptr<const Packet> explicitPayload);
    uint32_t SelectOutPort(Ptr<UbWqeSegment> segment,
                           UbCtpEntityState& state,
                           const UbCtpEntityKey& routeKey,
                           uint16_t loadBalanceSalt);
    uint16_t NextLoadBalanceSalt(UbCtpEntityState& state, const UbCtpEntityKey& routeKey);
    void RegisterQueueIfNeeded(Ptr<UbCtpTxQueue> queue,
                               uint32_t outPort,
                               uint32_t priority);
    void RunTrackedCompleteFromTaAck(UbCtpEntityKey key,
                                     uint32_t taSsn,
                                     Ptr<UbWqeSegment> response,
                                     uint64_t token);
    void CancelTrackedCompleteFromTaAck(UbCtpEntityKey key);
    void TryRetireTransactionState(const UbCtpEntityKey& key);
    bool SendResponseSegment(Ptr<UbWqeSegment> response, UbCtpEntityState& state);
    void CompleteFromTaAck(const UbCtpEntityKey& key,
                           uint32_t taSsn,
                           Ptr<UbWqeSegment> response);
    void CompleteFromTaAckNow(const UbCtpEntityKey& key,
                              uint32_t taSsn,
                              Ptr<UbWqeSegment> response);
    std::map<uint32_t, Ptr<UbWqeSegment>>::iterator FindOutstandingSegment(
        UbCtpTransactionState& state,
        uint32_t taSsn);
    uint32_t ResolveWireTaSsn(UbCtpTransactionState& state, uint16_t wireTaSsn) const;
    bool ShouldTraceWindow(const UbCtpEntityKey& key) const;
    bool ShouldDelayTaAck(const UbCtpEntityKey& key, uint32_t taSsn) const;
    void AddCtpNetworkHeaders(Ptr<Packet> packet,
                              const UbCtpEntityKey& key,
                              CtpOpcode opcode,
                              uint8_t loadBalanceSalt = 0);
    void AddCtpNetworkHeaders(Ptr<Packet> packet,
                              const UbCtpEntityKey& key,
                              CtpOpcode opcode,
                              uint8_t loadBalanceSalt,
                              RoutingType routingType);
    void FirstPacketSendsNotify(uint32_t taskId,
                                const UbCtpEntityKey& key,
                                uint32_t taSsn,
                                uint32_t outPort,
                                uint32_t payloadBytes,
                                UbTransactionOpcode opcode);
    void LastPacketACKsNotify(uint32_t taskId,
                              const UbCtpEntityKey& key,
                              uint32_t taSsn,
                              uint32_t payloadBytes,
                              UbTransactionOpcode opcode);
    void CtpRecvNotify(uint32_t packetUid,
                       const UbCtpEntityKey& key,
                       uint32_t taSsn,
                       PacketType type,
                       uint32_t size,
                       uint32_t taskId,
                       UbPacketTraceTag traceTag);
    void TraceWindowEvent(const UbCtpEntityKey& key,
                          const std::string& event,
                          uint32_t taSsn,
                          const UbCtpTransactionContext& context,
                          uint32_t outPort = UINT32_MAX) const;
    void TraceDetailEvent(const UbCtpEntityKey& key,
                          const std::string& event,
                          uint32_t taSsn,
                          uint32_t bytes = UINT32_MAX,
                          uint32_t expectedBytes = UINT32_MAX,
                          uint32_t flowBytes = UINT32_MAX,
                          uint32_t flowSize = UINT32_MAX,
                          uint32_t outPort = UINT32_MAX) const;
    void DoDispose() override;

    Ptr<Node> m_node;
    UbLifecycleResourceIdentity m_lifecycleOwner;
    UbTrackedEventSet m_delayedTaAckEvents;
    UbCtpRoutingPolicy m_defaultRoutingPolicy;
    bool m_pktTraceEnabled{false};
    uint32_t m_boundOutPortCount{0};
    uint32_t m_experimentTaAckPacketBytes{0};
    bool m_windowTraceEnabled{false};
    uint32_t m_windowTraceSrcNode{UINT32_MAX};
    uint32_t m_windowTraceDstNode{UINT32_MAX};
    uint32_t m_delayTaAckSrcNode{UINT32_MAX};
    uint32_t m_delayTaAckDstNode{UINT32_MAX};
    uint32_t m_delayTaAckSequence{UINT32_MAX};
    uint32_t m_delayTaAckModulo{0};
    uint32_t m_delayTaAckRemainder{0};
    Time m_delayTaAckTime{NanoSeconds(0)};
    std::map<UbCtpEntityKey, std::vector<Ptr<UbJetty>>> m_keyJettys;
    std::map<UbCtpEntityKey, uint32_t> m_keyJettyRrIndex;
    std::map<UbCtpEntityKey, UbCtpEntityState> m_entityStates;
    std::map<QueueKey, Ptr<UbCtpTxQueue>> m_queues;
    std::set<Ptr<UbCtpTxQueue>> m_registeredQueues;
    std::map<UbCtpCongestionKey, uint32_t> m_congestionSignals;
    TracedCallback<uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t>
        m_traceFirstPacketSendsNotify;
    TracedCallback<uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t>
        m_traceLastPacketACKsNotify;
    TracedCallback<uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   uint32_t,
                   PacketType,
                   uint32_t,
                   uint32_t,
                   UbPacketTraceTag>
        m_ctpRecvNotify;
};

} // namespace ns3

#endif // UB_CTP_H
