# T32 Task Brief: Remove OFF packet-transfer evidence work

Status: implementation reviewed; awaiting implementation commit

## Reuse Plan

- Pre-refactor commit: `main@ccf41b58`
- Old active symbols: `UbEgressQueue::DoEnqueue`, `UbEgressQueue::DoDequeue`,
  `UbLink::TransmitStart`
- Old active path: queue and link operations updated their existing state, invoked existing trace
  sources, and scheduled packet delivery without lifecycle tag parsing or owner resolution
- Reuse mode: direct guard around the current implementation and reuse of existing test/performance
  assets
- Required delta: while lifecycle trace is OFF, return before lifecycle tag parsing and before the
  trace-owner mutex
- Preserved behavior: packet birth/terminal balance, generation, leases, cleanup, retirement,
  callback/timer order, queue/link order, packet bytes, ACK/RTO, CTP, and RTP

## Batch Reuse Map

- Semantic batch: default-OFF lifecycle evidence fast path
- Current call path: `UbEgressQueue::DoEnqueue` / `UbEgressQueue::DoDequeue` /
  `UbLink::TransmitStart` -> packet `ObservePacketTransfer` -> lifecycle tag read -> owner routing ->
  active-attempt lookup -> evidence update
- Continuous code reused: the full non-OFF transfer-observation body and `PrepareOwner` sink setup
  remain unchanged
- Explicit behavior change: OFF does not update `transferObservations` or
  `lastTransferSequence`, validate transfer-only stale/inactive observations, or send cross-owner
  transfer-only control; OFF already emits no lifecycle evidence
- Excluded: registration, terminal, resource/work/lease state, retirement, parallel terminal and
  cleanup control, builders, parsers, queues, schedulers, congestion control, ACK/RTO, CTP, RTP
- Re-approval triggers: any non-OFF artifact change, protocol artifact change, terminal/balance
  change, excluded-path edit, or unexplained failure

## Acceptance Suite

- targeted `unified-bus` and `unified-bus-test` build
- `unified-bus-lifecycle`
- `unified-bus-switch-voq`
- p01 OFF: one warm-up and five serial counterbalanced pre-T32/T32 pairs
- explicit SUMMARY: complete pre-T32/T32 managed artifact hash comparison
- `git diff --check`

## Evidence

- Targeted build: PASS
- `unified-bus-lifecycle`: PASS; expected child-process fail-fast abort text is visible
- `unified-bus-switch-voq`: PASS
- p01 OFF semantic artifacts: exact for all five pairs
- p01 OFF paired median: wall `-0.97%`, user CPU `-1.64%`, RSS `-0.08%`
- p01 OFF non-regressing wall pairs: `3/5`; retention is based on non-regressing paired median and
  exact semantics, not a speculative minimum-gain threshold
- explicit SUMMARY: exact, including schema and 16 node lifecycle trace files; no missing, added,
  or changed managed artifact
- `git diff --check`: PASS
- residual processes: none

## Review

- `main@ccf41b58` queue/link state updates, trace callbacks, and delivery scheduling remain intact
- non-OFF tag parsing, owner routing, active-attempt validation, counters, sequence, and trace bytes
  remain intact
- no packet builder/parser/state machine, ACK/RTO, queue/scheduler, congestion, callback, timer,
  terminal, lease, cleanup, or retirement logic changed
- all `MakeTraceRecord`, `SelectPacket`, and `SelectResource` paths were already gated
- remaining startup-only OFF trace setup and public active-snapshot counters are not part of this
  hot-path batch and require separate semantics before removal

## Test Note

The intended RED was masked by a stale no-suffix test dylib that already contained an experimental
OFF fast path while source and the frozen performance dylib did not. The final targeted production
and test libraries were rebuilt from source before GREEN and performance validation.
