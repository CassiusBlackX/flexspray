状态: 已确认

# Allocator event ownership fastpath and reclaim assessment

## Target

This plan extends the Draft architecture-integration PR on
`codex/unified-bus-architecture-integration`. The design baseline is commit `2bf02c8c`; the
diagnostic source changes that disable ownership are evidence only and are not production code.

The work has two required outcomes:

1. preserve allocator event cancellation, forced-stop residue, and reclaim safety without the
   switch-wide linear event scan on every allocator round;
2. centralize TP backing-reclaim and explicit-destroy decisions into one typed assessment that
   reports why reclamation is blocked.

The first outcome must pass correctness and performance acceptance before implementation of the
second outcome begins.

## Evidence baseline

The controlled attribution package is under
`/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution/`
in the detached diagnostic worktree. Against full allocator event ownership, disabling allocator
ownership reduced retired instructions by
`22.5493% / 22.5538% / 22.5658%`; switch in-port ownership did not show a stable effect above
`0.5%`. Each treatment preserved 12,288 canonical events, identities, and timestamps.

This establishes the allocator event-ownership implementation as the production optimization
target. It does not authorize removing ownership because the diagnostic treatment does not retain
forced-stop cancellation or residue reporting.

## Terms

**Allocator event ownership** is the allocator-local responsibility for retaining pending
`EventId` handles, associating scheduling work with an output port, canceling pending callbacks at
forced stop, and reporting residue.

**Scheduling work** means an allocation or retrigger callback that can still select or move work
for an output port. Port notification and deadlock events remain owned and cancellable, but they do
not block TP/VOQ reclamation unless their callback can access the reclaimed object.

**Reclaim assessment** is a typed, read-only result for one TP endpoint and one teardown intent. It
combines lifecycle, transaction, allocator, transport, retransmission, and congestion-control
conditions without replacing their owner-local state.

## Confirmed design

### Allocator-local ownership

Replace the switch-wide `m_trackedEvents` scan with state indexed by `outPortId`:

- `ALLOCATION` uses one pending slot per output port because the allocator running state permits at
  most one pending allocation callback for that port.
- `DEADLOCK` uses one allocator-wide slot.
- `RETRIGGER` and compatibility-mode `PORT_NOTIFICATION` retain multiple handles until uniqueness
  or coalescing equivalence is independently proven.
- Each scheduled callback removes its own pending ownership at callback entry. Normal completion
  must not require a global prune.
- Each output-port state maintains an O(1) scheduling-work count and its active ingress queue.
- `HasPendingOrActiveWork(queue)` preserves the current conservative rule: any pending allocation
  or retrigger for the queue's output port blocks reclaim, while active work blocks only the active
  queue.
- `DoDispose()` cancels all still-pending handles and captures the same event/active-work residue
  represented by the current implementation.

The first production version must not coalesce, reorder, reschedule, or delete callbacks. Event
coalescing is a separate optimization requiring its own prediction and canonical controls.

The allocator must not use `UbTrackedEventSet`. That module binds events to lifecycle resource
identities and leases and supports cancel cleanup for protocol owners. Allocator events are
switch-local, output-port-indexed scheduler work and do not need that cost or ownership model.

### Typed reclaim assessment

After allocator acceptance, `UbTransportChannelManager` becomes the single place that evaluates TP
quiescence. Both backing reclaim and explicit destroy call one method shaped like:

```cpp
UbReclaimAssessment AssessTpQuiescence(const EndpointRecord& endpoint,
                                        UbTeardownIntent intent) const;
```

`UbTeardownIntent` distinguishes at least backing reclaim from explicit destroy. Forced stop may be
represented only if it uses the same read-only assessment; forced cancellation remains an
owner-specific operation.

`UbReclaimAssessment` contains a typed blocker set and diagnostic counts. Required blockers cover:

- inactive or wrong-generation lifecycle owner;
- in-flight packets and each active lifecycle lease class;
- transaction references and pending transport work;
- allocator scheduling or active queue work;
- transport queues, inbound/remote work, and unacknowledged PSN state;
- retransmission timer or outstanding retransmission state;
- congestion-control dormancy eligibility where the intent requires it;
- execution-owner and endpoint-state restrictions.

The result may expose `IsAllowed()`, but a boolean-only `IsAlive()` or `HasWork()` provider is not an
acceptable interface. The manager explicitly calls typed collaborators; those collaborators do not
implement a universal provider interface and do not migrate to a shared storage container.

Dormant-state snapshot materialization remains a fail-closed execution step after a successful
backing-reclaim assessment. The read-only assessment must not allocate, retain, or transfer the
snapshot merely to answer whether the endpoint is quiescent.

### Other liveness ledgers

The current PR must profile and classify other liveness-related ledgers but must not optimize them
without evidence. Candidate investigations are:

- switch in-port delayed-event indexing;
- owner indexing for `UbTrackedEventSet::HasEvents()` and `CancelPending()`;
- reverse TP reference counts for `UbTransaction::IsTpInUse()`.

Lifecycle lease identities, retransmission descriptors, TPG membership, connection reservation,
and dormant backing are correctness state machines, not counter-only ownership ledgers. They remain
out of scope unless a separate profile and design prove a safe seam.

## Invariants

- Preserve RR and DWRR packet selection, allocation delay, and same-time callback ordering.
- Preserve allocator direct and scheduled completion modes.
- Preserve allocator pending-work gates for TP backing reclaim and VOQ destruction.
- Preserve pending-versus-running cancellation semantics and forced-stop residue.
- Preserve lifecycle owner/generation checks and MTP execution-owner routing.
- Preserve retransmission, congestion-control, fault, dependency-release, and owner-cleanup
  behavior.
- Preserve ordinary CSV, structured, compressed-table, CLI, and trace compatibility.
- Do not change the already accepted allocator same-time ordering, FC-NONE stale-retransmission
  cleanup, or TPG task-sticky semantics.

## Performance acceptance

Retired instructions are the primary metric; cycles and `Simulator::Run()` time are supporting
metrics, and RSS is a regression guard.

Use three paired process samples for each 20-30 second semantic workload. Compare these five named
states where available:

- main baseline behavior;
- next baseline behavior;
- current PR with complete event ownership;
- current PR with allocator ownership disabled for diagnosis;
- current PR with output-port-local event ownership.

In each allocator-dense primary workload where the complete-versus-disabled ownership gap is stable
and at least 5%, the production candidate must recover at least 80% of that retired-instruction gap.
Coupled control workloads with a smaller ownership gap are regression controls rather than recovery
ratio gates: the candidate must not add a consistent retired-instruction regression above 2% versus
complete ownership. Canonical packet/task outputs must be identical or have a recorded, predeclared
explanation. A consistent RSS increase above 2% is a failed regression guard. Wall-time results must
be reported but do not override stable instruction evidence.

The remaining PR/main instruction difference must be measured and attributed; this work does not
require eliminating costs that implement retained lifecycle correctness.

## Correctness acceptance

- Focused lifecycle, allocator, VOQ, TP reservation, CTP lifecycle, retransmission, and aggregate
  Unified Bus suites pass.
- RR and DWRR cover one and multiple pending retriggers on the same output port.
- Direct and scheduled port notification modes pass.
- Callback-entry reclaim queries observe the correct pending/running state.
- TP backing reclaim and VOQ destruction remain blocked while allocator work exists and become
  eligible after work resolves.
- Forced stop cancels pending allocation, retrigger, notification, and deadlock callbacks and
  reports exact residue.
- Local and MTP owner-cleanup controls pass.
- Reclaim assessment produces the expected typed blocker for each focused negative case and does
  not change successful reclaim/destroy behavior.

## Delivery boundaries

The production changes are delivered as separate reviewable commits:

1. tests and owner-local allocator event ownership;
2. correctness and performance evidence package updates;
3. typed reclaim assessment and duplicate-gate removal;
4. assessment-focused tests and other-ledger disposition report.

No task may commit diagnostic ownership-disable switches as production behavior. No other ledger
optimization is included merely because it resembles the allocator implementation.
