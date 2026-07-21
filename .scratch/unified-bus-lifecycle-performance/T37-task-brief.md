# T37 Task Brief: Restore DCQCN idle reset semantics

## Batch Reuse Map

- Pre-refactor commits: `main@ccf41b58`, behavior reference `next@cf8ee683`.
- Old active symbol: `UbHostDcqcn::OnSenderTransportIdle`.
- Old call path: sender transport drains or TP destruction requests idle transition, releases the
  host active-flow slot, cancels recovery/pacing events, resets sender state, then resets pacing time.
- Reuse mode: copy-and-adjust.
- Reused continuous block: assignments to `m_alpha`, `m_currentRate`, `m_targetRate`,
  `m_timeRecoveryEvents`, `m_byteRecoveryEvents`, and `m_bytesSinceLastIncrease`.
- Lifecycle-only adaptation: retain `CancelTrackedEvent` for rate-increase, alpha, and pacing event
  ownership instead of restoring raw `EventId::Cancel`.
- Preserved ordering: release active flow, cancel tracked events, clear started state, reset scalar
  recovery state, reset next-send time.
- Explicit behavior change: remove the lifecycle branch's undocumented congestion continuity and
  restore main/next idle reset semantics.
- Excluded paths: CNP builder/parser, ECN marking, recovery algorithms, pacing-debt rescaling,
  packet lifecycle, and TP cleanup.

## Characterization and Acceptance

- Existing `UbDcqcnIdleFlowCancelsRecoveryStateTest` is the characterization seam. It continues to
  verify tracked lease cancellation and inactivity after old timer deadlines, and now asserts the
  historical initial-rate reset instead of continuity.
- Targeted build: `ninja -C cmake-cache -j2 unified-bus unified-bus-test test-runner`.
- Focused suites: `unified-bus-congestion-control`, `unified-bus-dcqcn-marking`, and
  `unified-bus-lifecycle` using the current no-suffix test runner.
- Differential: one serial next/current replay of the existing m15 PFC/DCQCN fixture at 16 MiB with
  Task, Port, PFC, and congestion traces enabled.
- Stop conditions: unknown packet/queue/PFC/congestion trace difference, changed task records or
  timestamps, a new lifecycle API, or any change outside the idle reset block.

## Result

- Implementation revision: `1d30887`.
- The targeted build and all three focused suites passed.
- Fifteen of sixteen shared artifacts are byte-identical and parsed task rows match.
- The sole `TaskTrace_node_2.tr` difference is a same-timestamp line order. Sorting the 2,048 records
  produces identical SHA-256 `039f9bb4...e4`; no record, timestamp, TASSN, completion, or protocol
  artifact differs. This is accepted as a narrow pre-existing trace-order exception outside the idle
  active path.
