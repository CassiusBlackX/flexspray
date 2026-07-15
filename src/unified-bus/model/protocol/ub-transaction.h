// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_TRANSACTION_H
#define UB_TRANSACTION_H

#include <ns3/node.h>
#include <deque>
#include <optional>
#include <tuple>
#include <vector>
#include <unordered_map>
#include "ns3/ub-datatype.h"
#include "ns3/ub-ctp.h"
#include "ns3/ub-network-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/callback.h"
#include "ns3/ub-lifecycle.h"

namespace ns3 {
    /**
     * @brief Transaction Service Mode enumeration
     */
    enum class TransactionServiceMode : uint8_t {
        ROI = 0,    // Reliable Ordered by Initiator
        ROT = 1,    // Reliable Ordered by Target
        ROL = 2,    // Reliable Ordered by Lower Layer
        UNO = 3     // Unreliable No Order
    };

    class UbController;
    class UbJetty;
    class UbFunction;
    class UbTransportChannel;
    class UbTransportGroup;

    enum class UbRtpTransportReferenceKind : uint8_t
    {
        DIRECT_TP = 0,
        TPG = 1,
    };

    enum class UbRtpOperationPreflightResult : uint8_t
    {
        ACCEPTING = 0,
        NOT_ACCEPTING = 1,
        NO_MEMBER = 2,
        STALE_REFERENCE = 3,
    };

    class UbRtpTransportReference
    {
      public:
        static UbRtpTransportReference ForDirectTp(
            uint32_t tpn,
            const UbLifecycleResourceIdentity& identity);
        static UbRtpTransportReference ForTpGroup(
            const UbLifecycleResourceIdentity& identity);

        UbRtpTransportReferenceKind GetKind() const;
        const UbLifecycleResourceIdentity& GetIdentity() const;
        bool IsDirectTp() const;
        uint32_t GetDirectTpn() const;

      private:
        UbRtpTransportReference() = default;
        UbRtpTransportReference(UbRtpTransportReferenceKind kind,
                                const UbLifecycleResourceIdentity& identity,
                                uint32_t directTpn);

        UbRtpTransportReferenceKind m_kind{UbRtpTransportReferenceKind::DIRECT_TP};
        UbLifecycleResourceIdentity m_identity;
        uint32_t m_directTpn{UINT32_MAX};

        friend class UbTransaction;
        friend struct UbTransactionOperationSnapshot;
    };

    struct UbTransactionOperationSnapshot
    {
        UbLifecycleResourceIdentity wqeIdentity;
        UbLifecycleResourceIdentity operationIdentity;
        UbRtpTransportReference transportReference;
        uint32_t directTpn{UINT32_MAX};
        uint32_t activeSegments{0};
        uint32_t activeTransportWorkLeases{0};
        std::vector<UbLifecycleResourceIdentity> segmentIdentities;
        std::vector<UbLifecycleResourceIdentity> resolvedTpIdentities;
    };
    /**
    * @brief
    */
    class UbTransaction : public Object {
    public:

        static TypeId GetTypeId(void);

        UbTransaction();
        UbTransaction(Ptr<Node> node);
        virtual ~UbTransaction();

        UbFunction* GetFunction();
        void SetFunction(UbFunction* function);

        Ptr<UbJetty> GetJetty(uint32_t jettyNum);

        bool JettyBindTp(uint32_t src, uint32_t dest, uint32_t jettyNum, bool multiPath, std::vector<uint32_t> tpns);

        void DestroyJettyTpMap(uint32_t jettyNum);
        void ReleaseRtpTpGroupBindingLeases(
            const UbLifecycleResourceIdentity& groupIdentity);
        void EraseRtpTpGroupBindings(
            const UbLifecycleResourceIdentity& groupIdentity);

        const std::vector<Ptr<UbTransportChannel>> GetJettyRelatedTpVec(uint32_t jettyNum);

        std::vector<Ptr<UbJetty>> GetTpRelatedJettyVec(uint32_t tpn);

        // function层调用
        void TriggerScheduleWqeSegment(uint32_t jettyNum,
                                       Ptr<UbWqe> originatingWqe,
                                       bool lifecycleOperation);

        // tp调用
        void ApplyScheduleWqeSegment(Ptr<UbTransportChannel> tp);
        Ptr<UbWqeSegment> PrepareNextCtpWqeSegment(Ptr<UbJetty> jetty,
                                                   uint32_t maxSegmentBytes);
        void CommitCtpWqeSegment(Ptr<UbWqeSegment> segment);
        UbCtpTransactionState& GetOrCreateCtpTransactionState(const UbCtpEntityKey& key);
        UbCtpTransactionState* FindCtpTransactionState(const UbCtpEntityKey& key);
        const UbCtpTransactionState* FindCtpTransactionState(const UbCtpEntityKey& key) const;

        bool ProcessWqeSegmentComplete(Ptr<UbWqeSegment> wqeSegment);
        void HandleInboundTaUnit(uint32_t localTpn, Ptr<UbWqeSegment> segment);
        Ptr<UbWqeSegment> ProcessInboundTaRequest(Ptr<UbWqeSegment> request);
        bool ProcessInboundTaResponse(Ptr<UbWqeSegment> response);
        uint64_t DeriveRemoteAddressForTest(const Ptr<UbWqeSegment>& request) const
        {
            return DeriveRemoteAddress(request);
        }

        void TriggerTpTransmit(uint32_t jettyNum);

        void TpInit(Ptr<UbTransportChannel> tp);

        void TpDeinit(uint32_t tpn);

        // 判断wqe是否能发送
        bool IsOrderedByInitiator(uint32_t jettyNum, Ptr<UbWqe> wqe);
        // TODO: support ROT
        bool IsOrderedByTarget(Ptr<UbWqe> wqe);
        // TODO: support Reliable
        bool IsReliable(Ptr<UbWqe> wqe);
        bool IsUnreliable(Ptr<UbWqe> wqe);

        void SetTransactionServiceMode(uint32_t jettyNum, TransactionServiceMode mode);
        TransactionServiceMode GetTransactionServiceMode(uint32_t jettyNum);

        // 新增wqe任务
        void AddWqe(uint32_t jettyNum, Ptr<UbWqe> wqe);

        const UbRtpTransportReference* FindRtpTransportReference(uint32_t jettyNum) const;
        bool IsRtpTransportReferenceCurrent(
            const UbRtpTransportReference& reference) const;
        UbRtpOperationPreflightResult PreflightRtpOperation(
            const UbRtpTransportReference& reference) const;
        bool TryAdmitRtpWqe(uint32_t jettyNum,
                            Ptr<UbWqe> wqe,
                            const UbLifecycleResourceIdentity& wqeIdentity);
        bool TryAdmitCtpWqe(uint32_t jettyNum,
                            Ptr<UbWqe> wqe,
                            const UbLifecycleResourceIdentity& wqeIdentity,
                            const UbCtpEntityKey& key);
        bool HasWqeOperation(uint32_t jettyNum, uint32_t wqeId) const;
        bool HasLifecycleOperationForJetty(uint32_t jettyNum) const;
        uint32_t GetActiveOperationCount() const;
        uint32_t GetAdmissionDomainReadySourceCountForTest(
            const UbLifecycleResourceIdentity& identity) const;
        bool IsAdmissionDomainBlockedForTest(
            const UbLifecycleResourceIdentity& identity) const;
        uint64_t GetAdmissionDomainCapacityWakeCountForTest(
            const UbLifecycleResourceIdentity& identity) const;
        uint32_t GetAdmissionDomainCountForTest() const;
        void NotifyRtpTpGroupMemberAvailable(
            const UbLifecycleResourceIdentity& groupIdentity);
        void TerminateRtpTpGroupOperations(
            const UbLifecycleResourceIdentity& groupIdentity,
            UbWorkOutcome outcome,
            bool forceDrain);
        void RetireRtpAdmissionDomain(
            const UbLifecycleResourceIdentity& identity);
        bool TryGetOperationSnapshot(uint32_t jettyNum,
                                     uint32_t wqeId,
                                     UbTransactionOperationSnapshot& snapshot) const;
        uint32_t GetTpRoundRobinIndexForTest(uint32_t tpn) const;

        // 某个wqe完成，刷新状态
        void WqeFinish(uint32_t jettyNum,
                       Ptr<UbWqe> wqe,
                       UbWorkOutcome outcome = UbWorkOutcome::COMPLETED);
        void CompleteWqeOperation(uint32_t jettyNum,
                                  uint32_t wqeId,
                                  UbWorkOutcome outcome);
        void ForceDrainTerminalPendingOperations(uint32_t jettyNum);

        // 判断本地tp是否处于使用状态
        bool IsTpInUse(uint32_t tpn);
        bool HasPendingTransportWork(uint32_t tpn) const;

        // 判断与本地tp配对的远程tp是否处于使用状态
        bool IsPeerTpInUse(uint32_t tpn);

        std::vector<uint32_t> GetUselessTpns();
    private:

        void DoDispose() override;

        struct AdmissionDomainKey;
        struct OperationRecord;
        void RunTrackedPrepareRtpWqeSegment(uint32_t jettyNum,
                                            Ptr<UbWqe> originatingWqe,
                                            AdmissionDomainKey domainKey,
                                            uint64_t eventToken);
        void RunTrackedAdmitRtpWqeSegment(Ptr<UbWqeSegment> segment,
                                          Ptr<UbWqe> originatingWqe,
                                          AdmissionDomainKey domainKey,
                                          uint64_t eventToken);
        enum class RtpSegmentAdmissionResult : uint8_t
        {
            ADMITTED,
            STALE_PREPARED_CANDIDATE,
            DEFERRED,
            REJECTED,
        };
        RtpSegmentAdmissionResult TryAdmitRtpSegment(
            uint32_t jettyNum,
            Ptr<UbWqe> originatingWqe,
            Ptr<UbWqeSegment> segment,
            Ptr<UbTransportChannel>& admittedTp);
        void ClearRtpScheduleChain(uint32_t jettyNum, Ptr<UbWqe> originatingWqe);
        struct AdmissionDomainKey
        {
            UbRtpTransportReferenceKind kind{UbRtpTransportReferenceKind::DIRECT_TP};
            UbLifecycleResourceIdentity identity;

            bool operator==(const AdmissionDomainKey&) const = default;
            bool operator<(const AdmissionDomainKey& other) const
            {
                return std::tie(kind,
                                identity.ownerNodeId,
                                identity.kind,
                                identity.localId,
                                identity.generation) <
                       std::tie(other.kind,
                                other.identity.ownerNodeId,
                                other.identity.kind,
                                other.identity.localId,
                                other.identity.generation);
            }
        };
        struct ReadySourceKey
        {
            enum class Kind : uint8_t
            {
                LOCAL_OPERATION,
                REMOTE_ORIGIN,
            };

            Kind kind{Kind::LOCAL_OPERATION};
            uint32_t jettyNum{0};
            uint32_t wqeId{0};
            UbLifecycleResourceIdentity operationIdentity;
        };
        struct RemoteSourceKey
        {
            uint32_t originJettyNum{0};
            uint32_t taskId{0};
            UbLifecycleResourceIdentity operationIdentity;

            bool operator<(const RemoteSourceKey& other) const
            {
                return std::tie(originJettyNum,
                                taskId,
                                operationIdentity.ownerNodeId,
                                operationIdentity.kind,
                                operationIdentity.localId,
                                operationIdentity.generation) <
                       std::tie(other.originJettyNum,
                                other.taskId,
                                other.operationIdentity.ownerNodeId,
                                other.operationIdentity.kind,
                                other.operationIdentity.localId,
                                other.operationIdentity.generation);
            }
        };
        struct RemoteSourceState
        {
            std::deque<Ptr<UbWqeSegment>> segments;
            bool readyQueued{false};
        };
        struct AdmissionDomainState
        {
            std::deque<ReadySourceKey> readySources;
            bool blocked{false};
            bool eventPending{false};
            bool sourceActive{false};
            EventId pendingEvent;
            std::optional<ReadySourceKey> activeSource;
            uint64_t capacityWakeEvents{0};
        };
        static AdmissionDomainKey MakeAdmissionDomainKey(
            const UbRtpTransportReference& reference);
        void StartRtpScheduleChain(uint32_t jettyNum, Ptr<UbWqe> originatingWqe);
        void EnqueueRtpSource(uint32_t jettyNum,
                              Ptr<UbWqe> originatingWqe,
                              bool restoreBlockedFront);
        void EnqueueRemoteRtpSource(uint32_t tpn, Ptr<UbWqeSegment> segment);
        void RunRemoteRtpSource(const AdmissionDomainKey& domainKey,
                                const ReadySourceKey& source);
        void ScheduleAdmissionDomain(const AdmissionDomainKey& domainKey);
        void CompleteAdmissionDomainAttempt(uint32_t jettyNum, Ptr<UbWqe> originatingWqe);
        void CancelRtpScheduleChain(AdmissionDomainKey domainKey,
                                    uint32_t jettyNum,
                                    Ptr<UbWqe> originatingWqe);
        void DetachRtpSource(OperationRecord& operation);
        void ForceDrainTerminalPendingOperation(uint32_t jettyNum, uint32_t wqeId);
        void DeferRtpSource(uint32_t jettyNum, Ptr<UbWqe> originatingWqe);
        void WakeDeferredRtpSources(Ptr<UbTransportChannel> tp);
        void WakeAdmissionDomain(const AdmissionDomainKey& key, bool capacityTransition);
        void RunAdmissionDomainWake(AdmissionDomainKey domainKey);
        bool IsCurrentWqeOperation(uint32_t jettyNum, Ptr<UbWqe> wqe) const;
        bool RecordSegmentSuccess(uint32_t jettyNum,
                                  uint32_t wqeId,
                                  uint32_t taSsn,
                                  Ptr<UbWqeSegment> completion);
        struct RtpTransportResolution
        {
            Ptr<UbTransportChannel> directTp;
            Ptr<UbTransportGroup> group;
        };
        Ptr<UbController> GetOwnerController() const;
        bool ResolveRtpTransportReference(
            const UbRtpTransportReference& reference,
            RtpTransportResolution& resolution) const;

        bool IsUrmaReadWriteRequest(const Ptr<UbWqeSegment>& segment);

        void ValidateUrmaServiceModeOrDie(uint32_t jettyNum, const Ptr<UbWqeSegment>& segment);
        Ptr<UbWqeSegment> ExecuteRemoteWriteAndBuildAck(uint32_t localTpn,
                                                        Ptr<UbWqeSegment> request);
        Ptr<UbWqeSegment> ExecuteRemoteReadAndBuildResponse(uint32_t localTpn,
                                                            Ptr<UbWqeSegment> request);
        void CompleteLocalRequestFromResponse(Ptr<UbWqeSegment> response);
        uint64_t DeriveRemoteAddress(const Ptr<UbWqeSegment>& request) const;

        void ReleaseTransportUseLease(OperationRecord& operation);

        uint32_t m_nodeId;
        UbFunction* m_function{nullptr};
        Ptr<UbLifecycleCoordinator> m_coordinator;

        // Tpn和Tp的对应map
        std::map<uint32_t, Ptr<UbTransportChannel>> m_tpnMap;
        std::map<uint32_t, UbRtpTransportReference> m_jettyRtpReferences;
        // 每个 Jetty 下次从哪个 TP 开始调度，避免短 WQE 总被第一个 TP 抢占。
        std::map<uint32_t, uint32_t> m_jettyTpNextIndex;
        // 新 Jetty 的初始 TP 轮转位置；一任务一 Jetty 的短流也能展开到多 TP。
        uint32_t m_nextJettyTpStartIndex = 0;
        std::map<uint32_t, std::map<RemoteSourceKey, RemoteSourceState>>
            m_tpRelatedRemoteRequests;

        std::map<uint32_t, uint32_t> m_tpRRIndex;
        Ptr<UniformRandomVariable> m_random;        //随机数产生工具，伪随机，多次仿真可复现

        // 记录每个jetty对应的serviceMode
        std::map<uint32_t, TransactionServiceMode> m_serviceMode;
        // 每个jetty存储wqe的id顺序
        std::map<uint32_t, std::vector<uint32_t>> m_jettyOrderedWqe;
        std::map<std::pair<uint32_t, uint64_t>, uint32_t> m_memoryStore;

        struct OperationRecord
        {
            Ptr<UbWqe> wqe;
            UbLifecycleResourceIdentity wqeIdentity;
            UbLifecycleResourceIdentity operationIdentity;
            UbRtpTransportReference transportReference;
            uint32_t directTpn{UINT32_MAX};
            UbLifecycleLeaseIdentity workLease;
            UbLifecycleLeaseIdentity transportUseLease;
            bool transportUseLeaseHeld{false};
            bool rtpScheduleChainActive{false};
            bool readyQueued{false};
            bool terminalPending{false};
            bool ctp{false};
            UbCtpEntityKey ctpKey;
            struct SegmentRecord
            {
                Ptr<UbWqeSegment> segment;
                UbLifecycleResourceIdentity identity;
                UbLifecycleLeaseIdentity childLease;
                UbLifecycleResourceIdentity resolvedTpIdentity;
                UbLifecycleLeaseIdentity transportWorkLease;
            };
            std::map<uint32_t, SegmentRecord> segments;
        };

        std::map<std::pair<uint32_t, uint32_t>, OperationRecord> m_operations;
        std::map<UbCtpEntityKey, UbCtpTransactionState> m_ctpTransactionStates;
        std::map<AdmissionDomainKey, AdmissionDomainState> m_admissionDomains;
        std::vector<UbLifecycleResourceIdentity> m_suppressedTpgOwnerProgress;
    };

} // namespace ns3

#endif /* UB_TRANSACTION_H */
