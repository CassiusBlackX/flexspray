// SPDX-License-Identifier: GPL-2.0-only
#include "ub-lifecycle.h"

#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/node-list.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(UbPacketLifecycleTag);
NS_OBJECT_ENSURE_REGISTERED(UbTransactionCorrelationTag);
NS_OBJECT_ENSURE_REGISTERED(UbLifecycleCoordinator);

Ptr<Packet>
CopyPacketView(Ptr<const Packet> packet)
{
    NS_ABORT_MSG_IF(packet == nullptr, "Cannot create a parser view of a null packet");
    return packet->Copy();
}

Ptr<Packet>
CopyPacketTransfer(Ptr<const Packet> packet)
{
    NS_ABORT_MSG_IF(packet == nullptr, "Cannot create a transfer copy of a null packet");
    return packet->Copy();
}

namespace
{

void
SerializeResourceIdentity(TagBuffer& buffer, const UbLifecycleResourceIdentity& identity)
{
    buffer.WriteU32(identity.ownerNodeId);
    buffer.WriteU8(static_cast<uint8_t>(identity.kind));
    buffer.WriteU64(identity.localId);
    buffer.WriteU64(identity.generation);
}

UbLifecycleResourceIdentity
DeserializeResourceIdentity(TagBuffer& buffer)
{
    UbLifecycleResourceIdentity identity;
    identity.ownerNodeId = buffer.ReadU32();
    identity.kind = static_cast<UbLifecycleResourceKind>(buffer.ReadU8());
    identity.localId = buffer.ReadU64();
    identity.generation = buffer.ReadU64();
    return identity;
}

} // namespace

std::ostream&
operator<<(std::ostream& os, const UbLifecycleResourceIdentity& identity)
{
    os << "node=" << identity.ownerNodeId << ",kind=" << static_cast<uint32_t>(identity.kind)
       << ",local=" << identity.localId << ",generation=" << identity.generation;
    return os;
}

std::ostream&
operator<<(std::ostream& os, const UbPacketAttemptIdentity& identity)
{
    os << '{' << identity.owner << ",attempt=" << identity.attempt << '}';
    return os;
}

TypeId
UbPacketLifecycleTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UbPacketLifecycleTag")
                            .SetParent<Tag>()
                            .SetGroupName("UnifiedBus")
                            .AddConstructor<UbPacketLifecycleTag>();
    return tid;
}

TypeId
UbPacketLifecycleTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
UbPacketLifecycleTag::GetSerializedSize() const
{
    return sizeof(uint32_t) + sizeof(uint8_t) + 3 * sizeof(uint64_t);
}

void
UbPacketLifecycleTag::Serialize(TagBuffer buffer) const
{
    SerializeResourceIdentity(buffer, m_identity.owner);
    buffer.WriteU64(m_identity.attempt);
}

void
UbPacketLifecycleTag::Deserialize(TagBuffer buffer)
{
    m_identity.owner = DeserializeResourceIdentity(buffer);
    m_identity.attempt = buffer.ReadU64();
}

void
UbPacketLifecycleTag::Print(std::ostream& os) const
{
    os << m_identity;
}

void
UbPacketLifecycleTag::SetIdentity(const UbPacketAttemptIdentity& identity)
{
    m_identity = identity;
}

UbPacketAttemptIdentity
UbPacketLifecycleTag::GetIdentity() const
{
    return m_identity;
}

TypeId
UbTransactionCorrelationTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UbTransactionCorrelationTag")
                            .SetParent<Tag>()
                            .SetGroupName("UnifiedBus")
                            .AddConstructor<UbTransactionCorrelationTag>();
    return tid;
}

TypeId
UbTransactionCorrelationTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
UbTransactionCorrelationTag::GetSerializedSize() const
{
    return 2 * (sizeof(uint32_t) + sizeof(uint8_t) + 2 * sizeof(uint64_t));
}

void
UbTransactionCorrelationTag::Serialize(TagBuffer buffer) const
{
    SerializeResourceIdentity(buffer, m_identity.operation);
    SerializeResourceIdentity(buffer, m_identity.segment);
}

void
UbTransactionCorrelationTag::Deserialize(TagBuffer buffer)
{
    m_identity.operation = DeserializeResourceIdentity(buffer);
    m_identity.segment = DeserializeResourceIdentity(buffer);
}

void
UbTransactionCorrelationTag::Print(std::ostream& os) const
{
    os << "operation={" << m_identity.operation << "},segment={" << m_identity.segment << '}';
}

void
UbTransactionCorrelationTag::SetIdentity(const UbTransactionCorrelationIdentity& identity)
{
    m_identity = identity;
}

UbTransactionCorrelationIdentity
UbTransactionCorrelationTag::GetIdentity() const
{
    return m_identity;
}

TypeId
UbLifecycleCoordinator::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UbLifecycleCoordinator")
                            .SetParent<Object>()
                            .SetGroupName("UnifiedBus")
                            .AddConstructor<UbLifecycleCoordinator>();
    return tid;
}

Ptr<UbLifecycleCoordinator>
UbLifecycleCoordinator::GetOrCreate(Ptr<Node> node)
{
    NS_ABORT_MSG_IF(node == nullptr, "Lifecycle coordinator requires an owning node");
    Ptr<UbLifecycleCoordinator> coordinator = node->GetObject<UbLifecycleCoordinator>();
    if (coordinator == nullptr)
    {
        coordinator = CreateObject<UbLifecycleCoordinator>();
        coordinator->m_ownerNodeId = node->GetId();
        node->AggregateObject(coordinator);
    }
    NS_ABORT_MSG_IF(coordinator->m_ownerNodeId != node->GetId(),
                    "Lifecycle coordinator is aggregated on the wrong node");
    return coordinator;
}

bool
UbLifecycleCoordinator::ResourceKey::operator<(const ResourceKey& other) const
{
    return std::tie(kind, localId) < std::tie(other.kind, other.localId);
}

UbLifecycleResourceIdentity
UbLifecycleCoordinator::OpenResource(UbLifecycleResourceKind kind, uint64_t localId)
{
    const ResourceKey key{kind, localId};
    ResourceState& state = m_resources[key];
    if (state.generation != 0 && !state.snapshot.retired)
    {
        return {m_ownerNodeId, kind, localId, state.generation};
    }

    NS_ABORT_MSG_IF(state.generation == std::numeric_limits<uint64_t>::max(),
                    "Lifecycle generation exhausted");
    ++state.generation;
    state.nextAttempt = 0;
    state.nextLease = 0;
    state.sequence = 0;
    state.snapshot = {};
    state.activeAttempts.clear();
    state.activeLeases.clear();
    state.stateChangedCallback = {};
    return {m_ownerNodeId, kind, localId, state.generation};
}

void
UbLifecycleCoordinator::RetireResource(const UbLifecycleResourceIdentity& identity)
{
    ResourceState& state = ValidateMutable(identity);
    NS_ABORT_MSG_IF(state.snapshot.inFlightPackets != 0,
                    "Cannot retire lifecycle generation with in-flight packets");
    NS_ABORT_MSG_IF(!state.activeLeases.empty(),
                    "Cannot retire lifecycle generation with active leases");
    state.snapshot.retired = true;
}

bool
UbLifecycleCoordinator::IsResourceActive(const UbLifecycleResourceIdentity& identity) const
{
    if (identity.ownerNodeId != m_ownerNodeId || identity.generation == 0)
    {
        return false;
    }
    const auto resource = m_resources.find({identity.kind, identity.localId});
    return resource != m_resources.end() &&
           resource->second.generation == identity.generation &&
           !resource->second.snapshot.retired;
}

void
UbLifecycleCoordinator::SetResourceStateChangedCallback(
    const UbLifecycleResourceIdentity& identity,
    Callback<void, UbLifecycleResourceIdentity> callback)
{
    ValidateMutable(identity).stateChangedCallback = callback;
}

UbLifecycleLeaseIdentity
UbLifecycleCoordinator::AcquireLease(const UbLifecycleResourceIdentity& owner,
                                     UbLifecycleLeaseKind kind,
                                     const UbLifecycleResourceIdentity& related)
{
    ResourceState& state = ValidateMutable(owner);
    const ResourceState& relatedState = Validate(related);
    NS_ABORT_MSG_IF(relatedState.snapshot.retired,
                    "Lifecycle lease cannot reference a retired resource");
    NS_ABORT_MSG_IF(state.nextLease == std::numeric_limits<uint64_t>::max(),
                    "Lifecycle lease identity exhausted");
    UbLifecycleLeaseIdentity lease{owner, related, kind, ++state.nextLease};
    const bool inserted = state.activeLeases.emplace(lease.lease, lease).second;
    NS_ABORT_MSG_IF(!inserted, "Duplicate lifecycle lease identity");
    switch (kind)
    {
    case UbLifecycleLeaseKind::WORK:
        ++state.snapshot.activeWorkLeases;
        break;
    case UbLifecycleLeaseKind::CHILD:
        ++state.snapshot.activeChildLeases;
        break;
    case UbLifecycleLeaseKind::TRANSPORT_USE:
        ++state.snapshot.activeTransportUseLeases;
        break;
    case UbLifecycleLeaseKind::BINDING:
        ++state.snapshot.activeBindingLeases;
        break;
    case UbLifecycleLeaseKind::TRACKED_EVENT:
        ++state.snapshot.activeTrackedEventLeases;
        break;
    case UbLifecycleLeaseKind::TRANSPORT_WORK:
        ++state.snapshot.activeTransportWorkLeases;
        break;
    default:
        NS_ABORT_MSG("Unknown lifecycle lease kind");
    }
    return lease;
}

void
UbLifecycleCoordinator::ReleaseLease(const UbLifecycleLeaseIdentity& lease)
{
    ResourceState& state = ValidateMutable(lease.owner);
    const auto it = state.activeLeases.find(lease.lease);
    NS_ABORT_MSG_IF(it == state.activeLeases.end(),
                    "Lifecycle lease release would underflow the active balance");
    NS_ABORT_MSG_IF(it->second != lease, "Lifecycle lease identity mismatch");
    switch (lease.kind)
    {
    case UbLifecycleLeaseKind::WORK:
        NS_ABORT_MSG_IF(state.snapshot.activeWorkLeases == 0,
                        "Lifecycle work lease counter underflow");
        --state.snapshot.activeWorkLeases;
        break;
    case UbLifecycleLeaseKind::CHILD:
        NS_ABORT_MSG_IF(state.snapshot.activeChildLeases == 0,
                        "Lifecycle child lease counter underflow");
        --state.snapshot.activeChildLeases;
        break;
    case UbLifecycleLeaseKind::TRANSPORT_USE:
        NS_ABORT_MSG_IF(state.snapshot.activeTransportUseLeases == 0,
                        "Lifecycle transport-use lease counter underflow");
        --state.snapshot.activeTransportUseLeases;
        break;
    case UbLifecycleLeaseKind::BINDING:
        NS_ABORT_MSG_IF(state.snapshot.activeBindingLeases == 0,
                        "Lifecycle binding lease counter underflow");
        --state.snapshot.activeBindingLeases;
        break;
    case UbLifecycleLeaseKind::TRACKED_EVENT:
        NS_ABORT_MSG_IF(state.snapshot.activeTrackedEventLeases == 0,
                        "Lifecycle tracked-event lease counter underflow");
        --state.snapshot.activeTrackedEventLeases;
        break;
    case UbLifecycleLeaseKind::TRANSPORT_WORK:
        NS_ABORT_MSG_IF(state.snapshot.activeTransportWorkLeases == 0,
                        "Lifecycle transport-work lease counter underflow");
        --state.snapshot.activeTransportWorkLeases;
        break;
    default:
        NS_ABORT_MSG("Unknown lifecycle lease kind");
    }
    state.activeLeases.erase(it);
    const auto callback = state.stateChangedCallback;
    if (!callback.IsNull())
    {
        callback(lease.owner);
    }
}

void
UbLifecycleCoordinator::RecordSuccessfulTerminal(const UbLifecycleResourceIdentity& identity)
{
    RecordWorkTerminal(identity, UbWorkOutcome::COMPLETED);
}

void
UbLifecycleCoordinator::RecordWorkTerminal(const UbLifecycleResourceIdentity& identity,
                                           UbWorkOutcome outcome)
{
    ResourceState& state = ValidateMutable(identity);
    const uint64_t terminalCount = state.snapshot.completedTerminals +
                                   state.snapshot.failedTerminals +
                                   state.snapshot.canceledTerminals +
                                   state.snapshot.flushedTerminals;
    NS_ABORT_MSG_IF(terminalCount != 0,
                    "Lifecycle resource already has a terminal outcome");
    switch (outcome)
    {
    case UbWorkOutcome::COMPLETED:
        ++state.snapshot.completedTerminals;
        ++state.snapshot.successfulTerminals;
        break;
    case UbWorkOutcome::FAILED:
        ++state.snapshot.failedTerminals;
        break;
    case UbWorkOutcome::CANCELED:
        ++state.snapshot.canceledTerminals;
        break;
    case UbWorkOutcome::FLUSHED:
        ++state.snapshot.flushedTerminals;
        break;
    default:
        NS_ABORT_MSG("Unknown work terminal outcome");
    }
}

UbLifecycleResourceIdentity
UbLifecycleCoordinator::OpenPacketOwner(UbLifecycleResourceKind kind, uint64_t localId)
{
    return OpenResource(kind, localId);
}

void
UbLifecycleCoordinator::RetirePacketOwner(const UbLifecycleResourceIdentity& identity)
{
    RetireResource(identity);
}

UbPacketAttemptIdentity
UbLifecycleCoordinator::RegisterPacketAttempt(const UbLifecycleResourceIdentity& owner,
                                              Ptr<Packet> packet)
{
    NS_ABORT_MSG_IF(packet == nullptr, "Cannot register a null packet attempt");
    UbPacketLifecycleTag existingTag;
    NS_ABORT_MSG_IF(packet->PeekPacketTag(existingTag),
                    "Packet attempt already has lifecycle identity");

    ResourceState& state = ValidateMutable(owner);
    NS_ABORT_MSG_IF(state.nextAttempt == std::numeric_limits<uint64_t>::max(),
                    "Packet attempt identity exhausted");
    const uint64_t attempt = ++state.nextAttempt;
    const bool inserted = state.activeAttempts.insert(attempt).second;
    NS_ABORT_MSG_IF(!inserted, "Duplicate packet attempt identity");

    ++state.snapshot.inFlightPackets;
    ++state.snapshot.registeredPackets;
    state.snapshot.lastBirthSequence = ++state.sequence;

    UbPacketAttemptIdentity identity{owner, attempt};
    UbPacketLifecycleTag tag;
    tag.SetIdentity(identity);
    packet->AddPacketTag(tag);
    return identity;
}

UbLifecycleResourceSnapshot
UbLifecycleCoordinator::GetSnapshot(const UbLifecycleResourceIdentity& owner) const
{
    return Validate(owner).snapshot;
}

bool
UbLifecycleCoordinator::TryReadIdentity(Ptr<const Packet> packet,
                                        UbPacketAttemptIdentity& identity)
{
    if (packet == nullptr)
    {
        return false;
    }
    UbPacketLifecycleTag tag;
    if (!packet->PeekPacketTag(tag))
    {
        return false;
    }
    identity = tag.GetIdentity();
    return true;
}

Ptr<UbLifecycleCoordinator>
UbLifecycleCoordinator::Resolve(const UbPacketAttemptIdentity& identity)
{
    NS_ABORT_MSG_IF(identity.owner.ownerNodeId >= NodeList::GetNNodes(),
                    "Packet lifecycle identity references an unknown owner node");
    Ptr<Node> node = NodeList::GetNode(identity.owner.ownerNodeId);
    Ptr<UbLifecycleCoordinator> coordinator = node->GetObject<UbLifecycleCoordinator>();
    NS_ABORT_MSG_IF(coordinator == nullptr,
                    "Packet lifecycle identity has no node-scoped coordinator");
    return coordinator;
}

void
UbLifecycleCoordinator::ObservePacketTransfer(Ptr<Packet> packet, UbPacketTransfer transfer)
{
    UbPacketAttemptIdentity identity;
    if (!TryReadIdentity(packet, identity))
    {
        return;
    }
    Resolve(identity)->ObservePacketTransfer(identity, transfer);
}

void
UbLifecycleCoordinator::TerminatePacket(Ptr<Packet> packet, UbPacketTerminal terminal)
{
    UbPacketAttemptIdentity identity;
    if (!TryReadIdentity(packet, identity))
    {
        return;
    }
    Resolve(identity)->TerminatePacket(identity, terminal);
}

UbLifecycleCoordinator::ResourceState&
UbLifecycleCoordinator::ValidateMutable(const UbLifecycleResourceIdentity& identity)
{
    NS_ABORT_MSG_IF(identity.ownerNodeId != m_ownerNodeId,
                    "Lifecycle mutation routed to the wrong node coordinator");
    const auto it = m_resources.find({identity.kind, identity.localId});
    NS_ABORT_MSG_IF(it == m_resources.end(), "Lifecycle mutation references an unknown resource");
    NS_ABORT_MSG_IF(identity.generation != it->second.generation,
                    "Lifecycle mutation references a stale generation");
    NS_ABORT_MSG_IF(it->second.snapshot.retired,
                    "Lifecycle mutation after resource retirement");
    return it->second;
}

const UbLifecycleCoordinator::ResourceState&
UbLifecycleCoordinator::Validate(const UbLifecycleResourceIdentity& identity) const
{
    NS_ABORT_MSG_IF(identity.ownerNodeId != m_ownerNodeId,
                    "Lifecycle query routed to the wrong node coordinator");
    const auto it = m_resources.find({identity.kind, identity.localId});
    NS_ABORT_MSG_IF(it == m_resources.end(), "Lifecycle query references an unknown resource");
    NS_ABORT_MSG_IF(identity.generation != it->second.generation,
                    "Lifecycle query references a stale generation");
    return it->second;
}

void
UbLifecycleCoordinator::ObservePacketTransfer(const UbPacketAttemptIdentity& identity,
                                              UbPacketTransfer)
{
    ResourceState& state = ValidateMutable(identity.owner);
    NS_ABORT_MSG_IF(state.activeAttempts.find(identity.attempt) == state.activeAttempts.end(),
                    "Packet transfer references an inactive attempt");
    ++state.snapshot.transferObservations;
    state.snapshot.lastTransferSequence = ++state.sequence;
}

void
UbLifecycleCoordinator::TerminatePacket(const UbPacketAttemptIdentity& identity,
                                        UbPacketTerminal terminal)
{
    ResourceState& state = ValidateMutable(identity.owner);
    const auto attempt = state.activeAttempts.find(identity.attempt);
    NS_ABORT_MSG_IF(attempt == state.activeAttempts.end(),
                    "Packet lifecycle terminal would underflow the active-attempt balance");
    NS_ABORT_MSG_IF(state.snapshot.inFlightPackets == 0,
                    "Packet lifecycle terminal would underflow the in-flight counter");

    state.activeAttempts.erase(attempt);
    --state.snapshot.inFlightPackets;
    switch (terminal)
    {
    case UbPacketTerminal::ENDPOINT_CONSUMED:
        ++state.snapshot.endpointConsumed;
        break;
    case UbPacketTerminal::ROUTE_NOT_FOUND:
        ++state.snapshot.routeNotFoundDrops;
        break;
    case UbPacketTerminal::EGRESS_QUEUE_FULL:
        ++state.snapshot.egressQueueFullDrops;
        break;
    case UbPacketTerminal::INGRESS_QUEUE_FULL:
        ++state.snapshot.ingressQueueFullDrops;
        break;
    case UbPacketTerminal::FAULT_DROP:
        ++state.snapshot.faultDrops;
        break;
    case UbPacketTerminal::CHANNEL_FAILURE:
        ++state.snapshot.channelFailureDrops;
        break;
    case UbPacketTerminal::MALFORMED_PACKET:
        ++state.snapshot.malformedPacketDrops;
        break;
    default:
        NS_ABORT_MSG("Unknown packet lifecycle terminal");
    }
    state.snapshot.lastTerminalSequence = ++state.sequence;
    const auto callback = state.stateChangedCallback;
    if (!callback.IsNull())
    {
        callback(identity.owner);
    }
}

void
UbTrackedEventSet::Init(Ptr<UbLifecycleCoordinator> coordinator)
{
    NS_ABORT_MSG_IF(coordinator == nullptr, "Tracked events require a lifecycle coordinator");
    NS_ABORT_MSG_IF(m_coordinator != nullptr && m_coordinator != coordinator,
                    "Tracked events cannot replace their lifecycle coordinator");
    m_coordinator = coordinator;
}

uint64_t
UbTrackedEventSet::Track(const UbLifecycleResourceIdentity& owner)
{
    NS_ABORT_MSG_IF(m_coordinator == nullptr, "Tracked events have no lifecycle coordinator");
    NS_ABORT_MSG_IF(m_nextToken == std::numeric_limits<uint64_t>::max(),
                    "Tracked-event token exhausted");
    const uint64_t token = ++m_nextToken;
    Record record;
    record.owner = owner;
    record.lease = m_coordinator->AcquireLease(owner,
                                               UbLifecycleLeaseKind::TRACKED_EVENT,
                                               owner);
    NS_ABORT_MSG_IF(!m_records.emplace(token, record).second,
                    "Duplicate tracked-event token");
    return token;
}

void
UbTrackedEventSet::Attach(const UbLifecycleResourceIdentity& owner,
                          uint64_t token,
                          EventId event,
                          Callback<void> cancelCleanup)
{
    auto tracked = m_records.find(token);
    NS_ABORT_MSG_IF(tracked == m_records.end() || tracked->second.owner != owner,
                    "Cannot attach an unknown tracked event");
    NS_ABORT_MSG_IF(tracked->second.event.IsPending(),
                    "Tracked event already has an attached EventId");
    NS_ABORT_MSG_IF(!event.IsPending(), "Tracked event must attach a pending EventId");
    tracked->second.event = event;
    tracked->second.cancelCleanup = cancelCleanup;
}

bool
UbTrackedEventSet::Begin(const UbLifecycleResourceIdentity& owner, uint64_t token)
{
    auto tracked = m_records.find(token);
    if (tracked == m_records.end() || tracked->second.owner != owner)
    {
        return false;
    }
    NS_ABORT_MSG_IF(tracked->second.phase != Phase::PENDING,
                    "Tracked event began more than once");
    tracked->second.phase = Phase::RUNNING;
    return true;
}

void
UbTrackedEventSet::Resolve(const UbLifecycleResourceIdentity& owner, uint64_t token)
{
    const auto event = m_records.find(token);
    NS_ABORT_MSG_IF(event == m_records.end() || event->second.owner != owner,
                    "Tracked event release would underflow");
    NS_ABORT_MSG_IF(event->second.phase != Phase::RUNNING,
                    "Tracked event resolved before its handler began");
    m_coordinator->ReleaseLease(event->second.lease);
    m_records.erase(event);
}

bool
UbTrackedEventSet::HasEvents(const UbLifecycleResourceIdentity& owner) const
{
    return std::any_of(m_records.begin(),
                       m_records.end(),
                       [&owner](const auto& entry) { return entry.second.owner == owner; });
}

std::vector<Callback<void>>
UbTrackedEventSet::CancelPending(const UbLifecycleResourceIdentity& owner)
{
    std::vector<Callback<void>> cancelCleanups;
    for (auto tracked = m_records.begin(); tracked != m_records.end();)
    {
        if (tracked->second.owner != owner || tracked->second.phase == Phase::RUNNING)
        {
            ++tracked;
            continue;
        }
        if (tracked->second.event.IsPending())
        {
            tracked->second.event.Cancel();
        }
        Callback<void> cancelCleanup = tracked->second.cancelCleanup;
        tracked->second.cancelCleanup = {};
        if (!cancelCleanup.IsNull())
        {
            cancelCleanups.push_back(cancelCleanup);
        }
        m_coordinator->ReleaseLease(tracked->second.lease);
        tracked = m_records.erase(tracked);
    }
    return cancelCleanups;
}

bool
UbTrackedEventSet::IsEmpty() const
{
    return m_records.empty();
}

void
UbTrackedEventSet::Clear()
{
    NS_ABORT_MSG_IF(!m_records.empty(), "Cannot clear active tracked events");
    m_coordinator = nullptr;
}

} // namespace ns3
