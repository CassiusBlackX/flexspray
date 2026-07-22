# Specification: Production RTP/TP Memory Compaction

Status: ready-for-agent

Source baseline: `codex/unified-bus-lifecycle-implementation` at `aa2b26a`, with the
default-off RTP/TP reclamation prototype used as feasibility evidence rather than as the
production lifecycle policy.

## Problem Statement

A 1024-node phased AllToAll workload contains 1,047,552 directed transfers. The current RTP path
retains one canonical connection, two materialized TP endpoints, one task-owned Jetty, repeated
connection-index values, and lifecycle state for each completed unique communication relation.
Measured checkpoints show retained connections, endpoints, and Jetties growing with completed
unique pairs even after WQE, transaction-operation, and transaction-segment state has returned to
zero. The resulting host-memory growth can prevent the full simulation from completing.

The prototype proved that phase-bounded RTP state can substantially reduce memory, but it is not a
production solution. It skips the required pre-traffic TP reservation order, closes the connection
generation after workload completion, and delays task-completion visibility until teardown
finishes. Those choices can change TPN and port assignment, RTP session continuity, MTP event
ordering, dependent-phase start time, and canonical output.

The production implementation must reduce retained host memory without changing network results,
task completion semantics, scheduling, routing, flow control, trace contracts, deterministic
runtime behavior, case schemas, or normal user commands.

## Solution

Reduce RTP/TP memory through storage compaction rather than workload-driven connection teardown.
Keep pre-traffic TP reservation, TPN assignment, endpoint identities, reservation generation,
session continuity, and lookup behavior unchanged. Remove repeated connection values from secondary
indexes, compact unmaterialized reservation records, explicitly retire one-shot task Jetties after
their completion is already visible, and move fully drained TP endpoint objects into a dormant
backing-state representation that can restore the same endpoint generation on demand.

A dormant endpoint is still logically present. It retains the protocol state required across idle,
including sequence continuity and algorithm-specific congestion history, while releasing drained
queues, retransmission descriptors, timers, pending work, and reconstructible object graphs. It can
be rehydrated through one manager-owned resolution interface for sender resolution, inbound packet
resolution, direct TP references, and TPG member use.

Reclamation is owner-local and storage-only. It does not call endpoint destroy, paired connection
teardown, lifecycle resource retirement, or TPN release. Workload completion may request or hint at
storage reclamation, but it is never proof of drain and never changes logical lifetime. The existing
owner/generation/lease/packet/event drain predicates remain authoritative.

## User Stories

1. As a simulation user, I want the 1024-node phased AllToAll workload to avoid retaining every
   completed TP endpoint object, so that the simulation can fit within practical host memory.
2. As a simulation user, I want task completion timestamps and dependent-phase start times to remain
   unchanged, so that existing experiment results remain comparable.
3. As a simulation user, I want to keep using the same case directory, CSV files, attributes, and
   run command, so that memory optimization does not require workflow migration.
4. As a simulation user, I want trace and result schemas to remain compatible, so that existing
   analysis tools continue to work.
5. As an MTP user, I want the same pre-traffic reservation order and owner assignment, so that
   parallel execution does not allocate different TPNs or ports.
6. As an MTP user, I want storage reclamation to mutate only owner-local state, so that no worker
   directly modifies a remote LP.
7. As a protocol developer, I want TP reservation and endpoint materialization to remain separate,
   so that an unmaterialized endpoint is not mistaken for an invalid TPN.
8. As a protocol developer, I want dormant endpoint state to preserve the current lifecycle
   identity and generation, so that rehydration is not a new session.
9. As an RTP developer, I want PSN, MSN, receive-window, duplicate-detection, and ordering continuity
   preserved across dormancy, so that later traffic behaves exactly as long-idle reuse does today.
10. As a congestion-control developer, I want each algorithm to define the state retained across
    dormancy, so that rehydration does not silently reset rate-control or CNP history.
11. As a Function-layer developer, I want one-shot task Jetties explicitly retired by their
    lifecycle authority, so that completed task-owned Jetties do not remain resident indefinitely.
12. As a workload developer, I want completion to become visible before any storage cleanup starts,
    so that cleanup never becomes a dependency barrier.
13. As a TP reservation owner, I want both endpoint slots and local lookup views to remain valid
    while backing objects are dormant, so that inbound validation continues to reject invalid TPNs.
14. As a TPG developer, I want member identities, membership leases, member order, and selection
    cursor semantics preserved, so that dormancy does not change member selection.
15. As a direct-TP user, I want generation-aware references to resolve a dormant endpoint through the
    same manager interface, so that a missing backing object is not reported as a stale binding.
16. As a receiver, I want a valid packet for a dormant reserved endpoint to rehydrate that endpoint
    locally before processing, so that no cross-LP zero-delay creation path is introduced.
17. As a maintainer, I want secondary connection indexes to store compact stable references rather
    than full connection values, so that indexes do not multiply reservation memory.
18. As a maintainer, I want index query order and public manager APIs preserved, so that the storage
    refactor does not change path selection or callers.
19. As a maintainer, I want one resolution seam for live and dormant TP endpoints, so that sender,
    receiver, direct, and TPG paths cannot implement inconsistent reconstruction rules.
20. As a diagnostics user, I want runtime storage snapshots to distinguish reservations, live TP
    backing objects, dormant endpoint records, and peak live backing objects, so that memory claims
    can be attributed.
21. As a test author, I want canonical output compared byte-for-byte between baseline and optimized
    runs, so that a memory win cannot hide task-timing drift.
22. As a test author, I want long-idle reuse, repeated pair use, bidirectional traffic, multipath,
    retransmission, and congestion-control cases, so that the unique-pair AllToAll shape is not the
    only correctness evidence.
23. As a performance engineer, I want RSS, wall time, event count, and runtime-state counts reported
    separately, so that storage reduction is not confused with runtime acceleration.
24. As a contributor, I want each implementation ticket to fit one fresh context and declare its
    blockers and stop conditions, so that a bounded implementation agent does not invent lifecycle
    policy.

## Implementation Decisions

- The production optimization is TP runtime-state reclamation, not workload-driven TP endpoint
  destroy or TP connection teardown.
- Pre-traffic TP reservation remains mandatory and continues in the existing traffic-record order.
  Reservation remains bounded by unique TP channel keys rather than traffic-row count.
- TPN, selected ports, priority, path identity, reservation owner, reservation generation, and the
  two endpoint slots remain valid for the entire logical connection lifetime.
- Secondary connection indexes store compact stable local references. They preserve ordered
  containers and insertion order. They do not store pointers, references, or iterators into a
  relocatable or erasable connection container.
- The canonical reservation record and each local endpoint view have one authoritative value.
  Existing query APIs keep their current behavior and reconstruct value results only when required
  by an existing caller.
- Unmaterialized reservations use a compact representation. Callback collections and active
  endpoint-only state are allocated only when required.
- A dormant TP endpoint retains its active lifecycle identity and generation. Dormancy never calls
  endpoint deinitialization that retires the packet owner, never releases its TPN, and never erases
  the reservation view.
- The dormant snapshot preserves PSN/MSN allocation and acknowledgement continuity, receive-next
  and maximum-received state, receive-window or bitmap state, inbound ordering references, exact TP
  and TPG binding identities, and congestion-control state required by the selected algorithm's
  idle contract.
- Drained WQE, ACK, CNP, remote-request, inbound-unit, out-of-order, and retransmission queues;
  retransmission timers; pacing timers; and reconstructible endpoint object graphs may be released
  without snapshotting only after the existing drain predicate proves that they are empty or
  inactive.
- Congestion-control dormancy uses an explicit per-algorithm snapshot/restore contract. Sender
  dynamic state may reset only where the existing algorithm already defines that idle reset.
  Receiver-side history that affects future control emission must be retained.
- Congestion-control dormancy is fail-closed per endpoint and applies uniformly to every supported
  host algorithm. If the selected algorithm has no proven snapshot/restore contract, the manager
  retains the live backing object and reports that the endpoint was ineligible; it must not reset
  state, partially reclaim the endpoint, or abort the simulation. DCQCN still requires an explicit
  contract, but its sender-idle reset and tracked-event behavior are already characterized. CAQM's
  remaining endpoint-local state must be characterized before transparent reclamation can be
  enabled for CAQM endpoints. A future algorithm is ineligible until it implements the same contract
  and passes continuity tests.
- The TP channel manager owns `ReclaimBackingState` and `ResolveLiveTp` behavior. All sender,
  inbound, direct-reference, and TPG-member paths resolve through the same manager seam.
- `ReclaimBackingState` is called only by the endpoint's manager on its owner LP, in response to an
  owner-local idle or terminal state transition. The manager evaluates the complete drain and
  congestion-control eligibility predicates and changes live backing state to dormant state as one
  owner-local operation at the end of the currently executing owner event. It schedules no new
  simulator event and sends no remote lifecycle message.
- Duplicate reclamation attempts are idempotent. A failed drain or eligibility check leaves the
  endpoint live and unchanged. It is retried only when a later owner-local state transition makes
  the endpoint a candidate again; there is no polling timer and no same-transition retry loop.
- Rehydration reconstructs a backing object from the unchanged reservation plus dormant snapshot,
  binds it to the same endpoint lifecycle identity, and restores continuity before admitting work
  or consuming a packet.
- `ResolveLiveTp` performs rehydration synchronously on the endpoint owner. Inbound packet handling
  resolves before packet consumption, while sender, direct-TP, and TPG paths resolve before work
  admission. A caller that is not executing on the endpoint owner must use the existing owner
  delivery path; it cannot borrow a remote LP or directly rehydrate remote mutable state.
- Reclamation requests are owner-local storage operations. Cross-owner lifecycle control remains
  reserved for modeled destroy and teardown; dormant backing reclamation must not introduce remote
  mutable-object access or a new wire protocol.
- Workload task completion remains visible at the existing point and timestamp. Cleanup completion
  is not awaited and cannot gate dependent work.
- Jetty creation carries an explicit lifetime classification: `TASK_SCOPED` or `REUSABLE`.
  Existing application-created per-task Jetties are marked `TASK_SCOPED`; default, shared, and
  externally managed creation paths remain `REUSABLE`. A task-scoped Jetty is explicitly destroyed
  by its lifecycle authority only after task completion has been published. Reusable Jetties are
  never inferred to be task-scoped from temporary idleness or current reference count.
- The legacy task-driven `RemoveUselessTp` path remains unsupported and fail-fast. The prototype
  reclamation flag is not promoted to a supported user option.
- Automatic storage reclamation does not reuse or renumber TPNs and does not allocate a new
  reservation or endpoint generation.
- Production enablement is transparent after acceptance. Any temporary rollout switch is internal
  or test-only and is removed or left default-off until all equivalence gates pass.
- Runtime storage evidence adds counts for reserved endpoint slots, live backing objects, dormant
  endpoint records, rehydrations, and peak live backing objects without changing existing row
  meanings or the versioned CSV shape.
- The implementation must preserve exact network outcomes and canonical task events. Performance
  evidence is evaluated separately and cannot waive correctness failures.

## Testing Decisions

- The highest test seam is the public `ub-quick-example` executable using canonical output and the
  versioned runtime storage snapshot. Tests assert user-visible behavior and manager-visible storage
  outcomes rather than private container layout.
- Ordinary tickets use focused RED/GREEN tests and the named module suites for their behavior seam;
  they do not each claim whole-run byte equivalence. The integration milestone compares canonical
  output byte-for-byte against a frozen `aa2b26a` feature baseline using an immutable input,
  configuration, command, binary/source revision, trace-preset, environment, and artifact manifest.
  Task `START` and `COMPLETE_VISIBLE` timestamps and ordering must match at that milestone.
- Runtime storage system tests assert completed-task balance and distinguish reservation count,
  live TP backing objects, dormant endpoint records, rehydration count, and peak live backing
  objects. RSS is not a correctness oracle.
- Connection-index tests exercise every existing lookup rule, full-criteria lookup, neighbor and
  node queries, ordered multipath results, erase, and reservation reuse through public manager APIs.
- Dormancy tests cover idle drained reclaim, reclaim rejection with packets, leases, tracked events,
  retransmission state, pending remote work, or transaction use, and exactly-once rehydration of the
  same identity and generation.
- RTP continuity tests cover PSN/MSN progression, duplicate detection, receive-window state,
  cumulative ACK, selective ACK, loss, retransmission, and long-idle reuse across reclaim/rehydrate.
- Congestion-control tests characterize and preserve both DCQCN and CAQM behavior across dormancy.
  A congestion-control algorithm without a proven snapshot/idle contract blocks transparent
  reclamation for that endpoint rather than falling back to a reset.
- Binding tests cover direct TP references, TPG members, member order, selection cursor, shared
  direct-plus-TPG use, and bidirectional endpoint views.
- Jetty tests prove that task completion is delivered exactly once before cleanup, active child work
  blocks destroy, cleanup completion does not release dependencies, and reusable Jetties are not
  automatically destroyed.
- MTP tests verify owner-local mutation, unchanged canonical output, same reservation digest, and no
  additional remote lifecycle teardown flow for dormant backing reclamation.
- Existing lifecycle, lifecycle-storage, CTP transaction, and CTP lifecycle suites remain green.
- Guarded performance checkpoints run serially with fixed inputs, binary hashes, RSS and wall-time
  limits, and fresh output directories. The p001 checkpoint gates p004; p004 gates p016; larger
  checkpoints require explicit approval.
- The strongest same-configuration memory comparison is reported separately from trace-confounded
  comparisons. A full 1,047,552-transfer estimate is not presented as a measured peak.
- Each ordinary implementation ticket ends with a focused build, its named tests, and a
  worktree-status audit. Integration tickets additionally produce canonical differential evidence
  from artifacts admitted by the frozen manifest. Results with different inputs, trace settings,
  executable/source revision, MTP settings, or environment are retained as diagnostics but are not
  eligible for the byte-stable acceptance comparison. No ticket may stage unrelated scratch data.

## Out of Scope

- Automatically closing an RTP session generation because a workload task completed, a channel was
  idle, host RSS increased, or a C++ reference count reached zero.
- Releasing TP reservations, TPNs, endpoint identities, connection views, or required protocol
  continuity as part of backing-state reclamation.
- Defining a UB wire-level TP close, reset, teardown, or re-establishment protocol.
- Changing routing, path selection, multipath policy, flow control, congestion-control algorithms,
  packetization, retransmission semantics, or dependency scheduling.
- Changing case schemas, required attributes, default commands, trace file names, or parser input
  contracts.
- Compressing or truncating trace output, changing trace presets, or solving the 400 GB log-volume
  problem in the same implementation series.
- Packet/event aggregation or other event-count reductions that could change modeled queueing or
  timing.
- Reclaiming topology-scoped nodes, switches, ports, links, routing tables, allocators, queue
  managers, or base flow-control objects.
- Treating the prototype's low-single-digit-GiB full-workload estimate as a completed full-scale
  measurement.

## Further Notes

- The p004 prototype checkpoint is the strongest same-case evidence: peak RSS fell from
  498,827,264 bytes to 273,170,432 bytes while completing the exact task count. It proves leverage,
  not production semantic equivalence.
- The p016 prototype checkpoint remained near 246 MB, but its baseline used different trace settings;
  the 84.3 percent delta is therefore partially confounded.
- The current retained-state attribution shows connections, TP endpoints, and Jetties scaling with
  completed unique pairs while WQE, operation, and segment counts return to zero.
- Both current algorithms require an explicit snapshot/restore eligibility implementation. DCQCN's
  sender idle reset, tracked timers, pacing, and receiver CNP history are sufficiently characterized
  for implementation planning. CAQM's mutable endpoint-local state and idle/reset boundary still
  require a focused characterization ticket before its snapshot implementation is delegated. All
  other identified reservation, identity, PSN, and binding boundaries are sufficiently defined.
