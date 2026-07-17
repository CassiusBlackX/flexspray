# T27 Task Brief: Avoid Redundant Selective Retransmission Scans

Status: complete

Branch: `codex/unified-bus-lifecycle-implementation`

Starting revision: `55610d1`

## Ticket Acceptance

1. A large retained selective window records `StartTimerIfNeeded` calls, outstanding-state scan
   calls, visited entries, timer state, schedule/cancel/restart counts, and focused host time.
2. The first eligible call scans outstanding state and schedules exactly one timer. Calls while that
   timer remains running perform no additional outstanding-state scan and do not schedule, cancel,
   or restart an event.
3. An expired timer still scans before scheduling, and an empty SELECTIVE or GBN state never starts
   a timer.
4. SELECTIVE ACK, TPSACK/SACK, timeout, fast retry, descriptor retirement, retry queue, completion,
   and GBN behavior retain their current order and outputs.
5. Targeted builds, the focused T27 suite, `unified-bus-transport-retrans`,
   `unified-bus-lifecycle`, and the existing selective-loss MarkPSN scenario pass serially. The
   scenario's pre/post task, packet, port, TP-debug, and derived outputs have no unexplained change.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`

Old active symbols:

- `UbRetransController::StartTimerIfNeeded`
- `UbRetransController::RestartTimerAfterAckProgress`
- `UbRetransController::ScheduleTimeout`
- `UbRetransController::CancelTimer`
- `UbTransportChannel::AdvanceNewDataSendState`

Old call path:

`UbTransportChannel::SendNewDataPacket -> OnNewDataPacketSent -> AdvanceNewDataSendState ->
UbRetransController::StartTimerIfNeeded -> EventId::IsExpired -> ScheduleTimeout`

Code blocks copied/extracted:

- Restore the pre-refactor timer-first guard in `StartTimerIfNeeded`.
- Retain the current descriptor-backed `HasOutstandingRetransmissionState` check after the timer
  guard so an expired timer with no GBN/SELECTIVE state remains stopped.
- Reuse the existing `ScheduleTimeout`, tracked-event lease, ACK restart, timeout, and cancellation
  blocks unchanged.

Required lifecycle adaptations:

- Keep the lifecycle-era tracked-event scheduling path and owner lease unchanged.
- Keep descriptor-backed GBN and SELECTIVE state as the outstanding-state source of truth.
- Add default-disabled test counters only at the existing controller/strategy seams.

Explicit behavior changes:

- A running retransmission timer returns before querying outstanding state.
- No timer deadline, retry decision, schedule, cancel, reset, packet, or protocol output changes.

Preserved ordering/observable behavior:

- New DATA descriptors are retained before the timer decision.
- The first eligible packet resets `m_rto` to `m_baseRto` and schedules through the same tracked or
  untracked path.
- Real ACK progress still resets attempts, cancels the current timer, tests remaining state, and
  schedules a replacement in the same order.
- RTO still schedules the next timeout before invoking the selected GBN/SELECTIVE timeout strategy.
- Packet/header/tag construction, ACK/TPSACK parsing, retry queue order, descriptor retirement,
  congestion accounting, callbacks, traces, and completion are unchanged.

Characterization evidence:

- `UbSelectiveSenderRtoEnqueuesOutstandingPsnsTest` is the RTO/retry-set oracle.
- The existing selective sender ACK, TPSACK, queue-priority, wire-wrap, congestion, stale cleanup,
  and GBN fixtures in `src/unified-bus/test/ub-test.cc` remain the protocol oracle.
- `scratch/retrans/SELE_fast/non_first_tp_packet_loss_markpsn` is the bounded real selective-loss,
  MarkPSN, fast-retry scenario.
- A T27 controller-level fixture will retain 4,096 SELECTIVE descriptors and invoke the existing
  timer entry repeatedly. The map invariant normally makes each pre-change query visit its first
  outstanding entry; the ticket claims removal of repeated queries, not a full-map traversal on
  every packet.

## Semantic Batches

### Batch 1: Counter-backed timer characterization

- Add a default-disabled timer/scan counter snapshot at `UbRetransController`.
- Count SELECTIVE outstanding queries and visited entries inside the existing
  `HasOutstandingPackets` loop.
- Add a focused T27 suite using the public retransmission-controller seam, including a GBN control.
- Build and run RED before changing guard order; preserve the pre-change focused binary and timing.

Stop if the counter requires a new production owner/API, changes normal execution while disabled,
or the retained-state fixture cannot reproduce repeated queries.

### Batch 2: Pre-refactor timer-first guard

- Move the existing running-timer check before `HasOutstandingRetransmissionState`.
- Preserve the empty-state check and all schedule/restart/cancel/timeout bodies.
- Run the focused T27 suite GREEN and record pre/post counters and host time.

Stop if schedule/cancel/restart counts, timer state, ACK/RTO order, or GBN behavior changes.

### Batch 3: Protocol and scenario acceptance

- Run one targeted build followed by the focused T27, transport retransmission, and lifecycle suites.
- Run the copied selective-loss MarkPSN case with the quick-example binary and compare it with the
  pre-change characterization tree.
- Review every changed block against `main@ccf41b58`, `6ec09e6`, and pre-T27 HEAD.

Stop on an unexplained test/output difference, an ACK/TPSACK/RTO/retry-order change, or a packet,
parser, queue, scheduler, routing, or congestion-control diff.

## Allowed Diff

- `.scratch/unified-bus-lifecycle-performance/T27-task-brief.md`
- `.scratch/unified-bus-lifecycle-performance/T27-report.md`
- `.scratch/unified-bus-lifecycle-performance/T27-summary.json`
- `tickets.md`
- `src/unified-bus/model/protocol/ub-retrans.cc`
- `src/unified-bus/model/protocol/ub-retrans.h`
- `src/unified-bus/test/ub-test.cc`
- generated T27 evidence under `.scratch/unified-bus-lifecycle-performance/evidence/`

## Validation Ledger

- 2026-07-17: ticket acceptance, pre-refactor reuse map, three semantic batches, stop conditions,
  and allowed diff recorded before production edits.
- 2026-07-17: `main@ccf41b58` uses a timer-first `StartTimerIfNeeded`; `6ec09e6` added the
  outstanding descriptor check before that guard. Current ACK restart, tracked-event scheduling,
  cancellation, timeout, GBN/SELECTIVE strategy, builder, parser, retry, and congestion paths are
  excluded from implementation changes.
- 2026-07-17: active-path review found that a live SELECTIVE map normally begins at an outstanding
  PSN, so repeated checks often visit one entry rather than the whole retained window. T27 is scoped
  to removing the proven repeated query and does not claim a linear full-window saving per packet.
- 2026-07-17: Batch 1 RED reproduced with 4,096 retained SELECTIVE descriptors and 1,000,000
  running-timer checks. The current guard issued 1,000,001 scan calls, visited 1,000,001 entries,
  scheduled once, and never canceled or restarted. The counted loop took 6,359 us; the test failed
  only the expected no-extra-scan assertions. The RED runner and Unified Bus library were preserved
  under `evidence/t27-timer-scan/pre-bin/`.
- 2026-07-17: the existing selective-loss MarkPSN scenario completed before production edits in
  5.466 ms reported total simulation host time (`0.05 s` process wall, 23,871,488 B peak RSS). Its
  copied input and complete runlog are preserved under `evidence/t27-selective-fault/pre/`.
- 2026-07-17: Batch 2 GREEN restores the pre-refactor timer-first guard while retaining the
  descriptor-state check for expired timers. The focused suite reports one scan/entry and one
  schedule across 1,000,001 starts, with zero cancel/restart; the GBN empty/start/running control
  also passes. Five counterbalanced in-process loops have pre median 5,262 us and post median
  4,305 us (`-18.19%`) for 1,000,000 running-timer calls. This is a seam-level result only.
- 2026-07-17: final targeted `unified-bus-test`, `test-runner`, and quick-example build passes.
  `unified-bus-retrans-timer-scan`, `unified-bus-transport-retrans`, and
  `unified-bus-lifecycle` pass serially. Expected lifecycle fail-fast child aborts were collected
  by the passing suite.
- 2026-07-17: the final selective-loss MarkPSN case completes and its complete input/runlog tree is
  byte-identical to the pre-change characterization. Its one-shot reported simulator host total
  varies from 5.466 ms pre to 8.483 ms final, so T27 makes no scenario-level performance claim.
- 2026-07-17: separate review against `main@ccf41b58`, `6ec09e6`, and pre-T27 HEAD passes. The
  pre-refactor timer-first guard is restored while the descriptor-era empty-state check remains.
  Schedule, tracked-event lease, ACK restart, cancel, timeout, GBN/SELECTIVE strategy, descriptor
  retirement, retry order, builder/parser, queue/scheduler, congestion, trace, and completion blocks
  are unchanged. An accidental whole-file format diff found during review was fully removed before
  final rebuild and validation.
- Implementation commit: `be9aa76f`.
