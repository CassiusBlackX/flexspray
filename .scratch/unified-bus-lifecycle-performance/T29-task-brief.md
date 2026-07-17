# T29 Task Brief: Amortize Allocator Event Reclamation

Status: ready-for-closeout

## Objective

Reduce the measured switch-allocator event-bookkeeping cost without changing allocator ownership,
VOQ destroy gates, forced-stop cancellation, pending-event visibility, scheduler order, or modeled
packet behavior.

The red feedback loop is the existing strictly serial p01 minimal-observability diagnosis. At 16 MiB
per flow, pre-T29 current is `0.714114 s` median wall and `0.68 s` median user CPU versus main's
`0.512434 s` and `0.48 s`. Time Profiler attributes 52 current samples to
`UbSwitchAllocator::PruneResolvedEvents`, including 19 direct leaf samples and repeated
`EventId::IsPending` queries. Task completion, throughput, PacketTrace, and TaskTrace are exact
between the measured main and current runs.

## Ticket Acceptance Suite

Run all build, test, and simulation work serially.

1. `git diff --check`.
2. `ninja -C cmake-cache unified-bus unified-bus-test test-runner`.
3. `./build/utils/ns3.44-test-runner-default --test-name=unified-bus-switch-voq`.
4. `./build/utils/ns3.44-test-runner-default --test-name=unified-bus-lifecycle`.
5. Run the existing p01 16 MiB minimal case as one warm-up plus five counterbalanced pre-T29/T29
   pairs. Record wall, user CPU, system CPU, RSS, page faults, and file I/O.
6. Require every T29/pre-T29 paired run to complete the same two tasks at the same simulated times.
   This ordinary ticket does not create a new main frozen-byte milestone.
7. Treat a paired-median T29 wall improvement of at least 5 percent as a useful hot-path gain. A
   smaller or noisy result remains visible and does not authorize a broader scheduler rewrite.

## Batch Reuse Map

Pre-refactor commit: `main@ccf41b58`.

Semantic batch: switch allocator tracked-event reclamation.

Old active symbols: `UbSwitchAllocator::TriggerAllocator`,
`UbRoundRobinAllocator::AllocateNextPacket`, `UbDwrrAllocator::AllocateNextPacket`,
`UbSwitchAllocator::CheckDeadlock`, and the direct `Simulator::Schedule` / `ScheduleNow` call sites.

Old call paths: port or queue readiness schedules the existing allocator callback; the callback
selects the existing ingress candidate, applies the existing egress/flow-control gates, updates the
existing running/retrigger state, and schedules the next existing callback.

Continuous code blocks reused: all main allocator selection, queue movement, flow-control release,
running/retrigger state, port notification, and deadlock callback bodies remain in place. The current
`TrackedEvent` identity fields, pending-count, destroy-gate, and dispose-cancel blocks are retained.

Reuse mode: direct reuse plus a lifecycle-only adjustment to reclamation cadence.

Required lifecycle adaptations: preserve an exact `EventId` for every owned event; reserve a private
generation token before scheduling; attach the returned `EventId`; resolve the token at callback
entry; reuse released vector slots; keep exact `EventId` queries at destroy-gate and disposal
boundaries. Each wrapper immediately invokes the old callback at the same simulation timestamp.

Preserved ordering and observable behavior: event timestamps, callback targets, RR/DWRR selection,
queue order, `m_isRunning` / `m_oneMoreRound` updates, active-ingress ownership, port notification,
deadlock cadence, pending-event results, VOQ destroy eligibility, forced residue, and cancellation
order.

Existing characterization: `unified-bus-switch-voq`, especially
`UbSwitchVoqDestroyGateBlocksAllocatorAndAccountingTest` and
`UbSwitchDisposeCancelsOwnedQueueEventsAndRecordsResidueTest`; `unified-bus-lifecycle`; the existing
p01 minimal case and production task statistics.

Explicit behavior changes: resolved internal records become reusable inactive slots immediately at
callback entry instead of being discovered by a later full-vector scan. They are never reported as
pending and are never canceled. No modeled or traced behavior changes.

Excluded paths: packet lifecycle owner routing, LDST tracked actions, RR/DWRR algorithms, queue and
flow-control semantics, packet builders/parsers, ACK/RTO/retransmission, CTP/RTP, congestion control,
MTP/MPI routing, public configuration, and trace schema.

Focused validation: the ticket acceptance suite above.

Re-approval triggers: an existing pending-event/VOQ test changes expected behavior; callback order or
timestamp changes; a public API or scheduler algorithm must change; storage cannot remain bounded;
the performance result is noisy or unexplained; or two equivalent failures occur.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`.

Old active symbols: the allocator symbols listed above.

Old call path: direct ns-3 scheduling into the old allocator callbacks, with no event bookkeeping.

Code blocks copied/extracted: none. The old active callback bodies and scheduling order are already
present and remain untouched.

Required lifecycle adaptations: retain the event ownership introduced by `e4c69c4`, but replace its
repeated `EventId::IsPending` discovery with owner-private generation-token resolution.

Explicit behavior changes: internal cleanup cadence only.

Preserved ordering/observable behavior: all old scheduling, selection, queue, flow-control, and
notification behavior listed in the Batch Reuse Map.

Characterization evidence: main source comparison, existing VOQ/allocator tests, and p01 semantic
identity.

## Review Checklist

- No main allocator branch, field update, callback target, or schedule timestamp changed.
- No parallel scheduler, queue, parser, builder, lifecycle owner, or cleanup implementation added.
- Every tracked event remains visible to destroy gating and forced-stop disposal while pending.
- Resolved records remain bounded and do not count as pending or residue.
- Delay, backpressure, queue retention, and retrigger remain non-terminal behavior.
- The final review compares the complete allocator diff against `main@ccf41b58` and pre-T29 HEAD.

## Ledger

- 2026-07-17: T29 opened from `db335a0`; `origin/main` is `ccf41b58`.
- 2026-07-17: pre-T29 p01 scale/profile evidence selected allocator resolved-event pruning as the
  first hot path. Packet-owner routing and LDST tracked actions remain separate future batches.
- 2026-07-17: a first bounded-batch compaction attempt preserved exact semantics but produced
  `+0.77%` paired-median wall and flat user CPU. It still queried every resolved `EventId`, so it was
  rejected rather than committed. The batch was narrowed to O(1) self-resolution with reusable
  generation slots; old callback bodies and schedule timestamps remain unchanged.
- 2026-07-17: the generation-slot variant passed the targeted build,
  `unified-bus-switch-voq`, and `unified-bus-lifecycle`. All five p01 pairs remained semantically
  exact, but paired-median wall was only `-0.23%`, user CPU was flat, and RSS was `+0.08%`.
- 2026-07-17: the generation-slot variant was rejected because its roughly 200 lines of wrapper,
  token, and slot state did not produce a repeatable useful gain. Allocator source and the temporary
  slot-reuse test were restored exactly to pre-T29 `db335a0`. The profile's 52 prune samples did not
  translate into reclaimable end-to-end time, so allocator reclamation is no longer the next
  optimization target.
