// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_LIFECYCLE_H
#define UB_LIFECYCLE_H

#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/tag.h"

#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <vector>

namespace ns3
{

class Node;

enum class UbLifecycleResourceKind : uint8_t
{
    RTP_ENDPOINT = 1,
    WQE = 2,
    TRANSACTION_OPERATION = 3,
    TRANSACTION_SEGMENT = 4,
    JETTY = 5,
    RTP_TPG = 6,
};

enum class UbLifecycleLeaseKind : uint8_t
{
    WORK = 1,
    CHILD = 2,
    TRANSPORT_USE = 3,
    BINDING = 4,
    TRACKED_EVENT = 5,
    TRANSPORT_WORK = 6,
};

enum class UbWorkOutcome : uint8_t
{
    COMPLETED = 0,
    FAILED = 1,
    CANCELED = 2,
    FLUSHED = 3,
};

enum class UbPacketTransfer : uint8_t
{
    EGRESS_QUEUE_ENQUEUE = 1,
    EGRESS_QUEUE_DEQUEUE = 2,
    LOCAL_LINK = 3,
};

enum class UbPacketTerminal : uint8_t
{
    ENDPOINT_CONSUMED = 1,
    ROUTE_NOT_FOUND = 2,
    EGRESS_QUEUE_FULL = 3,
    INGRESS_QUEUE_FULL = 4,
    FAULT_DROP = 5,
    CHANNEL_FAILURE = 6,
    MALFORMED_PACKET = 7,
};

struct UbLifecycleResourceIdentity
{
    uint32_t ownerNodeId{0};
    UbLifecycleResourceKind kind{UbLifecycleResourceKind::RTP_ENDPOINT};
    uint64_t localId{0};
    uint64_t generation{0};

    bool operator==(const UbLifecycleResourceIdentity&) const = default;
};

enum class UbDestroyReason : uint8_t
{
    EXPLICIT = 0,
    PROTOCOL_COMPLETE = 1,
    ERROR = 2,
    SIMULATION_STOP = 3,
};

enum class UbDestroyStatus : uint8_t
{
    COMPLETED = 0,
    PENDING = 1,
    NOT_FOUND = 2,
    REJECTED = 3,
};

using UbDestroyCallback = Callback<void, UbLifecycleResourceIdentity, UbDestroyStatus>;

struct UbPacketAttemptIdentity
{
    UbLifecycleResourceIdentity owner;
    uint64_t attempt{0};

    bool operator==(const UbPacketAttemptIdentity&) const = default;
};

struct UbLifecycleLeaseIdentity
{
    UbLifecycleResourceIdentity owner;
    UbLifecycleResourceIdentity related;
    UbLifecycleLeaseKind kind{UbLifecycleLeaseKind::WORK};
    uint64_t lease{0};

    bool operator==(const UbLifecycleLeaseIdentity&) const = default;
};

struct UbTransactionCorrelationIdentity
{
    UbLifecycleResourceIdentity operation;
    UbLifecycleResourceIdentity segment;

    bool operator==(const UbTransactionCorrelationIdentity&) const = default;
};

std::ostream& operator<<(std::ostream& os, const UbLifecycleResourceIdentity& identity);
std::ostream& operator<<(std::ostream& os, const UbPacketAttemptIdentity& identity);
Ptr<Packet> CopyPacketView(Ptr<const Packet> packet);
Ptr<Packet> CopyPacketTransfer(Ptr<const Packet> packet);

class UbPacketLifecycleTag : public Tag
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer buffer) const override;
    void Deserialize(TagBuffer buffer) override;
    void Print(std::ostream& os) const override;

    void SetIdentity(const UbPacketAttemptIdentity& identity);
    UbPacketAttemptIdentity GetIdentity() const;

  private:
    UbPacketAttemptIdentity m_identity;
};

class UbTransactionCorrelationTag : public Tag
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer buffer) const override;
    void Deserialize(TagBuffer buffer) override;
    void Print(std::ostream& os) const override;

    void SetIdentity(const UbTransactionCorrelationIdentity& identity);
    UbTransactionCorrelationIdentity GetIdentity() const;

  private:
    UbTransactionCorrelationIdentity m_identity;
};

struct UbLifecycleResourceSnapshot
{
    uint64_t inFlightPackets{0};
    uint64_t registeredPackets{0};
    uint64_t transferObservations{0};
    uint64_t endpointConsumed{0};
    uint64_t routeNotFoundDrops{0};
    uint64_t egressQueueFullDrops{0};
    uint64_t ingressQueueFullDrops{0};
    uint64_t faultDrops{0};
    uint64_t channelFailureDrops{0};
    uint64_t malformedPacketDrops{0};
    uint64_t lastBirthSequence{0};
    uint64_t lastTransferSequence{0};
    uint64_t lastTerminalSequence{0};
    uint64_t activeWorkLeases{0};
    uint64_t activeChildLeases{0};
    uint64_t activeTransportUseLeases{0};
    uint64_t activeBindingLeases{0};
    uint64_t activeTrackedEventLeases{0};
    uint64_t activeTransportWorkLeases{0};
    uint64_t successfulTerminals{0};
    uint64_t completedTerminals{0};
    uint64_t failedTerminals{0};
    uint64_t canceledTerminals{0};
    uint64_t flushedTerminals{0};
    bool retired{false};
};

class UbLifecycleCoordinator : public Object
{
  public:
    static TypeId GetTypeId();

    static Ptr<UbLifecycleCoordinator> GetOrCreate(Ptr<Node> node);

    UbLifecycleResourceIdentity OpenResource(UbLifecycleResourceKind kind, uint64_t localId);
    void RetireResource(const UbLifecycleResourceIdentity& identity);
    UbLifecycleLeaseIdentity AcquireLease(const UbLifecycleResourceIdentity& owner,
                                          UbLifecycleLeaseKind kind,
                                          const UbLifecycleResourceIdentity& related);
    void ReleaseLease(const UbLifecycleLeaseIdentity& lease);
    void RecordSuccessfulTerminal(const UbLifecycleResourceIdentity& identity);
    void RecordWorkTerminal(const UbLifecycleResourceIdentity& identity, UbWorkOutcome outcome);
    bool IsResourceActive(const UbLifecycleResourceIdentity& identity) const;

    UbLifecycleResourceIdentity OpenPacketOwner(UbLifecycleResourceKind kind, uint64_t localId);
    void RetirePacketOwner(const UbLifecycleResourceIdentity& identity);
    UbPacketAttemptIdentity RegisterPacketAttempt(const UbLifecycleResourceIdentity& owner,
                                                  Ptr<Packet> packet);
    UbLifecycleResourceSnapshot GetSnapshot(const UbLifecycleResourceIdentity& owner) const;

    static void ObservePacketTransfer(Ptr<Packet> packet, UbPacketTransfer transfer);
    static void TerminatePacket(Ptr<Packet> packet, UbPacketTerminal terminal);

  private:
    struct ResourceKey
    {
        UbLifecycleResourceKind kind;
        uint64_t localId;

        bool operator<(const ResourceKey& other) const;
    };

    struct ResourceState
    {
        uint64_t generation{0};
        uint64_t nextAttempt{0};
        uint64_t nextLease{0};
        uint64_t sequence{0};
        UbLifecycleResourceSnapshot snapshot;
        std::set<uint64_t> activeAttempts;
        std::map<uint64_t, UbLifecycleLeaseIdentity> activeLeases;
    };

    static bool TryReadIdentity(Ptr<const Packet> packet, UbPacketAttemptIdentity& identity);
    static Ptr<UbLifecycleCoordinator> Resolve(const UbPacketAttemptIdentity& identity);

    ResourceState& ValidateMutable(const UbLifecycleResourceIdentity& identity);
    const ResourceState& Validate(const UbLifecycleResourceIdentity& identity) const;
    void ObservePacketTransfer(const UbPacketAttemptIdentity& identity, UbPacketTransfer transfer);
    void TerminatePacket(const UbPacketAttemptIdentity& identity, UbPacketTerminal terminal);

    uint32_t m_ownerNodeId{0};
    std::map<ResourceKey, ResourceState> m_resources;
};

class UbTrackedEventSet
{
  public:
    void Init(Ptr<UbLifecycleCoordinator> coordinator);
    uint64_t Track(const UbLifecycleResourceIdentity& owner);
    void Attach(const UbLifecycleResourceIdentity& owner,
                uint64_t token,
                EventId event,
                Callback<void> cancelCleanup);
    bool Begin(const UbLifecycleResourceIdentity& owner, uint64_t token);
    void Resolve(const UbLifecycleResourceIdentity& owner, uint64_t token);
    bool HasEvents(const UbLifecycleResourceIdentity& owner) const;
    std::vector<Callback<void>> CancelPending(const UbLifecycleResourceIdentity& owner);
    bool IsEmpty() const;
    void Clear();

  private:
    enum class Phase : uint8_t
    {
        PENDING = 0,
        RUNNING = 1,
    };

    struct Record
    {
        UbLifecycleResourceIdentity owner;
        UbLifecycleLeaseIdentity lease;
        EventId event;
        Callback<void> cancelCleanup;
        Phase phase{Phase::PENDING};
    };

    Ptr<UbLifecycleCoordinator> m_coordinator;
    uint64_t m_nextToken{0};
    std::map<uint64_t, Record> m_records;
};

} // namespace ns3

#endif // UB_LIFECYCLE_H
