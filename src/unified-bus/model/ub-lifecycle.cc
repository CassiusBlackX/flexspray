// SPDX-License-Identifier: GPL-2.0-only
#include "ub-lifecycle.h"

#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/node-list.h"

#include <limits>
#include <tuple>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(UbPacketLifecycleTag);
NS_OBJECT_ENSURE_REGISTERED(UbLifecycleCoordinator);

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
    buffer.WriteU32(m_identity.owner.ownerNodeId);
    buffer.WriteU8(static_cast<uint8_t>(m_identity.owner.kind));
    buffer.WriteU64(m_identity.owner.localId);
    buffer.WriteU64(m_identity.owner.generation);
    buffer.WriteU64(m_identity.attempt);
}

void
UbPacketLifecycleTag::Deserialize(TagBuffer buffer)
{
    m_identity.owner.ownerNodeId = buffer.ReadU32();
    m_identity.owner.kind = static_cast<UbLifecycleResourceKind>(buffer.ReadU8());
    m_identity.owner.localId = buffer.ReadU64();
    m_identity.owner.generation = buffer.ReadU64();
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

UbLifecycleLeaseIdentity
UbLifecycleCoordinator::AcquireLease(const UbLifecycleResourceIdentity& owner,
                                     UbLifecycleLeaseKind kind,
                                     const UbLifecycleResourceIdentity& related)
{
    ResourceState& state = ValidateMutable(owner);
    Validate(related);
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
    default:
        NS_ABORT_MSG("Unknown lifecycle lease kind");
    }
    state.activeLeases.erase(it);
}

void
UbLifecycleCoordinator::RecordSuccessfulTerminal(const UbLifecycleResourceIdentity& identity)
{
    ResourceState& state = ValidateMutable(identity);
    NS_ABORT_MSG_IF(state.snapshot.successfulTerminals != 0,
                    "Lifecycle resource already has a terminal outcome");
    ++state.snapshot.successfulTerminals;
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
    default:
        NS_ABORT_MSG("Unknown packet lifecycle terminal");
    }
    state.snapshot.lastTerminalSequence = ++state.sequence;
}

} // namespace ns3
