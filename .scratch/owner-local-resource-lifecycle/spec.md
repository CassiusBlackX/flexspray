# Remove simulator-global packet lifecycle bookkeeping

Status: ready-for-agent

## Problem Statement

UnifiedBus currently duplicates protocol ownership in a simulator-global lifecycle implementation.
Packets are registered at birth, tagged with lifecycle identity, observed during transfer, resolved
at consume or drop, and reported back to their origin owner. The implementation maintains packet
attempt bitmaps, in-flight counts, leases, coordinator state, callbacks, and cross-owner terminal
messages on paths exercised by local, MTP, MPI, and hybrid simulation.

This work is not required to model packet movement. Packet objects, scheduled events, local queues,
transport sequence state, acknowledgements, retransmission state, and protocol-owned operation
records already model network behavior. The extra global observation costs retired instructions,
adds non-network communication between execution owners, and can disagree with the authoritative
protocol state it duplicates.

The same lifecycle implementation is also used to authorize TP backing dormancy and some explicit
destroy operations. Removing bookkeeping without first migrating those correctness dependencies
would allow premature connection retirement, reject valid late traffic, or reuse a TPN while old
packets can still arrive. The existing delayed-destroy prototype demonstrated these failures.

The branch must therefore remove the global packet lifecycle implementation and make modeled
resource destruction correct from owner-local network state. It is not a TP memory-reclamation
project. Memory measurements only explain why narrow, explicit customer destroy interfaces remain
useful after automatic reclamation is removed.

## Solution

Packets follow the ordinary network and protocol paths without simulator-global lifecycle tags,
birth registration, transfer observation, terminal resolution, origin-return messages, or in-flight
packet accounting. Optional diagnostics record operations locally after they happen and never
participate in protocol behavior or resource destruction.

Modeled resource closure is explicit and entity-specific. A lifetime authority requests destroy at
a declared network, task, collective, connection, group, or management boundary. The owning module
stops new upper-layer admission, continues work already accepted, evaluates the real state it
already owns, and retires the resource only when that state proves closure is safe.

The implementation preserves and repairs three explicit destroy modules:

1. Jetty destroy, owned by the Jetty manager.
2. TP connection destroy, coordinated privately by the TP connection manager across both endpoint
   owners.
3. RTP TPG destroy and member removal, owned by the node's controller and transaction state.

CTP, LDST, transaction, allocator, timer, queue, and packet state are not promoted into generic
lifecycle participants. Existing owner state is either natural finite work or evidence consulted by
one of the three owning modules. No universal lifecycle interface, root-operation identity,
dependency ledger, or manual acquire/release accounting replaces the removed coordinator.

## User Stories

1. As a simulation user, I want lifecycle correctness without per-packet simulator bookkeeping, so
   that disabling observability does not impose unrelated packet-path cost.
2. As a simulation user, I want unchanged network results after the lifecycle implementation is
   replaced, so that performance improvement does not change modeled behavior.
3. As an MTP user, I want packets to cross logical-process owners without terminal messages returning
   to their origin solely for resource accounting.
4. As an MPI user, I want packet serialization and delivery to contain only modeled network effects,
   not hidden resource-lifecycle coordination.
5. As a protocol developer, I want packet handlers to process packets, ACKs, CNPs, retransmissions,
   and drops without understanding resource lifecycle state.
6. As a diagnostics user, I want packet operations logged at the node where they occur, so that I
   can reconstruct behavior without making logging a correctness dependency.
7. As a URMA client, I want Jetty destroy to reject new WQEs while allowing accepted WQEs to finish.
8. As a connection owner, I want TP connection destroy to wait for both endpoint owners and all
   protocol obligations already accepted.
9. As a connection owner, I want valid packets arriving while a connection is closing to continue
   through the normal ingress and transport paths.
10. As a connection owner, I want an exact packet for a retired connection key distinguished from
    an unknown or mismatched packet, so that stale traffic does not hide configuration errors.
11. As a simulation user, I want retired TPNs quarantined for the rest of the run, so that packets
    without a wire-visible generation cannot be delivered to a new connection.
12. As a TPG owner, I want group destroy and member removal to use real transaction and selection
    state instead of global lifecycle leases.
13. As a CTP user, I want CTP transactions and queues to retain their existing completion behavior
    without global packet ownership records.
14. As an LDST user, I want task and segment completion to follow LDST's own maps, queues, ACK state,
    and scheduled actions without global packet counts.
15. As a customer, I want direct Jetty and TP connection destroy interfaces available regardless of
    convenience-policy settings.
16. As a standard-driver user, I want optional task-scoped Jetty and planned TP connection destroy
    disabled by default, so that the driver never invents resource lifetime.
17. As a large multi-phase workload user, I want to opt into planned destroy only for resources the
    standard driver created and can prove have no later declared use.
18. As a maintainer, I want TP dormancy and rehydration removed, so that software memory policy is
    not represented as network behavior.
19. As a maintainer, I want one authoritative owner for each close predicate, so that lifecycle bugs
    are localized rather than spread across packet call sites.
20. As a performance engineer, I want retired instructions measured after byte-level correctness is
    established, so that speedup claims compare identical modeled work.

## Implementation Decisions

### Primary objective and deletion contract

- The primary objective is removal of simulator-global packet lifecycle overhead while preserving
  network behavior and repairing explicit destroy semantics.
- Product packet paths will no longer create or inspect packet lifecycle tags, packet-attempt
  identities, packet-attempt bitmaps, or global in-flight packet counts.
- Link, port, switch, transport, fault, CTP, and LDST paths will no longer report packet transfer or
  packet terminal outcomes to a lifecycle coordinator.
- MTP and MPI will no longer route packet-terminal control records back to a packet's origin.
- TP backing dormancy, backing reclaim, dormant snapshots, and rehydration will be removed.
- Stable owner-local resource identities may remain where they express modeled connection, endpoint,
  Jetty, or TPG identity. Identity does not imply a global lifecycle coordinator.

### Packet ingress and retirement

The delayed-destroy prototype failure implies the required three-result ingress model:

```text
LIVE(endpoint)  - active or closing; process normally
RETIRED_EXACT   - exact full-key quarantine match; classify as stale
INVALID         - unknown or mismatched key; preserve fatal validation
```

- Closing endpoint views remain live ingress targets until final retirement.
- Each endpoint owner atomically replaces its live view with full-key quarantine during retirement.
- A quarantine record has no transport backing and cannot rehydrate.
- Because transport packets carry no connection generation, a retired TPN is never reused during
  the same simulation.

### Jetty destroy

- The existing explicit Jetty destroy interface remains the public seam.
- Destroy closes new WQE admission for the exact Jetty generation.
- Accepted WQEs continue through existing transaction and transport algorithms.
- The Jetty manager derives readiness from its Jetty record, WQE state, transaction operations,
  tracked actions, and bindings already owned by the relevant modules.
- Progress notifications request reevaluation and carry no lifecycle reference count.

### TP connection destroy

- The existing explicit TP connection destroy interface remains the public seam.
- The TP connection manager privately owns cross-owner close ordering, callback aggregation, and
  final reservation retirement.
- Both endpoint owners close new upper-layer admission and continue valid data, ACK, CNP,
  retransmission, timer, queue, allocator, and response work accepted before close.
- Endpoint readiness is transport-mode-specific and based only on owner-local protocol state.
- An endpoint slot is not close-ready merely because transport backing has not been materialized.
  Its owner must still prove that no accepted ingress, response, reservation, or scheduled
  obligation remains and materialize drain-only handling when required by existing traffic.
- Reservation views are not erased until both endpoint owners have completed their close and
  installed quarantine.

### RTP TPG destroy

- Existing explicit TPG destroy and member-removal behavior is preserved.
- New operation selection is frozen before graceful destroy or last-member removal.
- Readiness is derived from the transaction owner's actual operations, admitted segments, current
  selection activity, admission-domain bindings, and member records.
- Global lifecycle use leases and binding leases are removed rather than replaced with another
  generic counter system.
- Destroying a TPG does not itself authorize destruction of its member TP connections.

### Other protocol state

- CTP transaction operations, pending and outstanding segments, delayed completions, queues, and
  congestion state retain their existing protocol ownership and natural completion behavior.
- LDST tasks and segments retain their existing task maps, thread queues, ACK state, and scheduled
  actions as their completion evidence.
- WQEs, transaction operations and segments, allocator work, timers, queue entries, and scheduled
  actions are finite work, not customer-managed resources.
- No new customer destroy interface is introduced for CTP, LDST, WQE, transaction, allocator,
  packet, timer, port, or queue state in this feature.

### Parallel ownership

- One execution owner exclusively mutates each node's Jetty, TP endpoint, TPG, and owner-local
  protocol state.
- Packet ingress resolves only target-owner connection and endpoint views and never takes the
  process-global connection-control lock.
- Cross-owner connection close uses low-frequency causal control carrying immutable identifiers,
  never remote mutable pointers.
- Management-plane serialization for explicit connection creation and close remains separate from
  the packet hot path.

### Observability

- Passive logs may record enqueue, dequeue, transmit, receive, consume, or drop after the local
  modeled action.
- Logs are buffered per node, logical process, or rank and may be merged offline using simulation
  time and wire-visible identity.
- Logging cannot change admission, packet handling, close readiness, retirement, or callback order.
- Removed lifecycle trace schemas and coordinator snapshots are not compatibility contracts.

### Customer destroy policy

- Direct explicit Jetty and TP connection destroy interfaces remain available in every policy mode.
- The standard application exposes independent Boolean controls named
  `EnableTaskScopedJettyDestroy` and `EnablePlannedTpConnectionDestroy`; both default to false.
- Task-scoped Jetty destroy applies only to Jetties explicitly created with task-scoped lifetime.
- Planned TP connection destroy applies only to reservations created and owned by the standard
  driver from its complete task plan.
- The driver requests connection destroy only after the last accepted planned task referencing the
  exact connection has completed.
- Preconfigured, externally created, reusable, or customer-managed resources are never destroyed by
  the convenience policy.
- The legacy task-inference switch and its aborting path are removed rather than repurposed.

### Memory evidence

- Memory is not a trigger, implementation objective, blocking dependency, or success criterion.
- The retained-state microprobe remains supporting evidence for keeping narrow explicit destroy
  interfaces. It is not evidence for automatic reclamation and does not measure RSS returned after
  destroy.
- The throwaway microprobe source is not part of the production deliverable.

## Testing Decisions

- The highest acceptance seam is raw-byte comparison of canonical output against baseline commit
  `68bbce1310dad539a0c053cac6cd14ac69dda5cb` for identical mode, seed, case inputs, build type,
  checkpoint, rank, and manifest.
- Byte identity is evaluated independently for local, MTP, MPI, and hybrid execution. Existing
  cross-mode differences are not normalized.
- A delayed-destroy scenario exercises valid ingress while closing, both-endpoint drain, retirement,
  callback completion, exact stale traffic, mismatched traffic, and TPN non-reuse.
- Focused tests use existing production packet parsing and ingress paths rather than a test-only
  packet oracle.
- TPG tests exercise active selection, member removal, graceful destroy, bound operations, empty
  groups, and member TP independence through the existing controller and transaction seams.
- CTP and LDST tests verify their externally visible task, segment, packet, ACK, and completion
  behavior after global packet accounting is removed.
- Convenience policy off and on produce byte-identical canonical network output when the declared
  scoped resources have no later use.
- Structural deletion checks confirm there are no product callers for packet lifecycle registration,
  transfer observation, terminal accounting, origin-return control, or TP dormancy/rehydration.
- Builds and tests run serially with at most eight build jobs.
- After correctness passes, an approximately 30-second workload is measured in reversed order.
  Retired instructions are primary and wall time is supporting evidence.
- The prototype's `3.77%` and `4.01%` retired-instruction reductions, median `3.89%`, are directional
  evidence and not a mandatory threshold.

## Out of Scope

- Automatic resource reclamation based on idleness, time, RSS, memory pressure, reference counts,
  empty containers, last packet activity, or simulator-global observation.
- New packet formats, wire-visible connection generations, or reuse of retired TPNs.
- Changes to ACK, retransmission, PSN, routing, queueing, allocator, congestion-control, transaction
  completion, task completion, or dependency scheduling semantics.
- A universal lifecycle participant interface, root-operation model, lineage ledger, or replacement
  acquire/release framework.
- New customer destroy interfaces for CTP, LDST, transactions, WQEs, allocator work, packets, ports,
  queues, timers, or scheduled actions.
- Implementing currently unmodeled Jetty groups, additional Jetty types, Entity destruction, memory
  registration lifetime, or device/topology teardown.
- Optimizing allocator page return or promising a particular post-destroy RSS reduction.

## Further Notes

The P1 prototype removed packet lifecycle hot-path work while preserving the exercised normal-path
canonical outputs in local, MTP, MPI, and hybrid modes. The P2 prototype demonstrated owner-local
RTP ingress on normal paths and exposed the two correctness gaps that block direct removal: early
reservation-view deletion and unsafe immediate TPN reuse.

The retained-state memory probe measured active empty entities only. Its results remain documented
as secondary evidence and do not influence ticket order or feature acceptance.
