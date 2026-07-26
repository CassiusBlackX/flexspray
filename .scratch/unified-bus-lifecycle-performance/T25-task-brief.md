# T25 Task Brief: Compact Retired Lifecycle Resource Storage

Status: complete

Branch: `codex/unified-bus-lifecycle-implementation`

Starting revision: `dfcfcb7`

## Ticket Acceptance

1. A serial, focused churn fixture opens, terminalizes, and retires many distinct resource IDs and
   reports resource count, active-state count, retained-summary count, wall time, and peak RSS before
   and after the layout change.
2. Retirement validates zero in-flight packets and zero active leases before releasing attempt,
   lease, callback, sequence, and other active-only state.
3. Reopening a local ID increments generation; stale mutation is rejected and cannot alias the
   replacement.
4. Explicit SUMMARY preserves the complete pre-T25 summary bytes. OFF retains only compact
   generation and terminal-outcome history after retirement, not evidence-only packet/lease totals.
5. Failed, canceled, flushed, forced-stop, and unresolved active work remain distinguishable;
   unresolved state cannot retire successfully.
6. The focused churn and p02 OFF RSS results are reported as absolute and percentage values. No TP
   teardown, packet, protocol, scheduling, or cleanup policy changes are allowed.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`

Old active symbols: no lifecycle coordinator or retained lifecycle resource map exists at the
pre-refactor baseline. The reusable protocol behavior remains in the old packet, transaction,
transport, TP, queue, scheduler, callback, and cleanup paths, all excluded from this ticket.

Old call path: protocol owners allocate and erase their own active state directly; no historical
lifecycle container is retained by main.

Current active symbols:

- `UbLifecycleCoordinator::OpenResource`
- `UbLifecycleCoordinator::RetireResource`
- `UbLifecycleCoordinator::AcquireLease`
- `UbLifecycleCoordinator::ReleaseLease`
- `UbLifecycleCoordinator::RecordWorkTerminal`
- `UbLifecycleCoordinator::RegisterPacketAttempt`
- `UbLifecycleCoordinator::ObservePacketTransfer`
- `UbLifecycleCoordinator::TerminatePacket`
- `UbLifecycleCoordinator::GetSnapshot`
- `UbLifecycleCoordinator::EmitLifecycleSummary`
- `UbLifecycleCoordinator::ValidateMutable`
- `UbLifecycleCoordinator::Validate`

Code blocks copied/extracted: the existing resource open, mutation, retirement validation, trace,
summary iteration, and generation checks remain in place. Only storage ownership and state access
are minimally adjusted.

Required lifecycle adaptations:

- Move snapshot, attempt/lease counters, exact active containers, sequence, and callback into one
  active state allocated only for an open generation.
- At valid retirement, optionally copy the final snapshot only when lifecycle evidence is enabled,
  retain compact generation and terminal outcome, then release the complete active state.
- Reconstruct the compact retired snapshot for OFF queries without retaining evidence-only totals.

Explicit behavior changes:

- With lifecycle trace OFF, a retired resource snapshot keeps `retired` and its terminal outcome but
  no longer retains packet/lease evidence totals.
- SUMMARY/EVENT/DETAIL final summary values and bytes remain unchanged.

Preserved ordering and observable behavior: balance checks precede retirement; RESOURCE_RETIRE trace
precedes active-state release; mutation counters, packet tags, callbacks, traces, and terminal order
remain unchanged; generation increment and stale/retired error precedence remain unchanged.

Characterization evidence: existing lifecycle packet accounting, trace-level summary, lease release,
stale generation, forced stop, TP/Jetty cleanup, and WQE outcome tests. The new churn fixture reuses
the coordinator production API and no simplified protocol oracle.

## Batch 1 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: pre-layout sequential churn characterization and storage counters.

Old active symbols and call paths: current coordinator `OpenResource -> RecordWorkTerminal ->
RetireResource`, repeated over distinct local IDs with lifecycle trace OFF.

Continuous code blocks reused: production resource APIs and the existing lifecycle test suite setup.

Reuse mode: test-asset / direct.

Required lifecycle adaptations: add bounded test-only storage counters; do not alter mutation or
retirement behavior in this batch.

Preserved ordering and observable behavior: identical open, terminal, retire sequence for every
resource; strictly serial execution.

Existing characterization: `UbLifecyclePacketAccountingTest`,
`UbLifecycleTraceLevelAndSummaryTest`, and stale/retired fail-fast cases.

Explicit behavior changes: none.

Excluded paths: protocol objects, automatic cleanup, packet builders/parsers, transport, ACK/RTO,
queues, schedulers, routing, and congestion control.

Focused validation: build `unified-bus-test` and `test-runner`; run only
`unified-bus-lifecycle-storage` under `/usr/bin/time -l`.

Re-approval triggers: the fixture requires a protocol rewrite, baseline runtime is unbounded, or
storage cannot be measured without a production-facing API.

## Batch 2 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: release active-only state and retain compact generation history.

Old active symbols and call paths: current `ResourceState` and every coordinator mutation listed in
the pre-refactor reuse map.

Continuous code blocks reused: all existing validation, counters, switches, trace construction,
callback-before-trace ordering, and retirement checks. Field access is mechanically redirected to
the active state.

Reuse mode: minimal extraction / storage move.

Required lifecycle adaptations: allocate active state on open, preserve an evidence snapshot only
when trace is non-OFF, record compact terminal outcome, release active state after retire trace.

Preserved ordering and observable behavior: exact membership, lease identity, counter updates,
callbacks, trace sequence, terminal uniqueness, generation increment, and stale/retired failures.

Existing characterization: lifecycle suite plus Batch 1 churn fixture.

Explicit behavior changes: OFF retired snapshots drop evidence-only counters; trace-enabled retired
snapshots remain exact.

Excluded paths: all protocol algorithms and lifecycle caller seams.

Focused validation: targeted build; `unified-bus-lifecycle-storage` and
`unified-bus-lifecycle --verbose`.

Re-approval triggers: summary bytes change, unresolved state can retire, stale error precedence
changes, or an active caller needs a new semantic.

## Batch 3 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: SUMMARY byte milestone and p02 OFF storage measurement.

Old active symbols and call paths: T23 frozen SUMMARY case, T23 p02 input, and serial timing/hash
harnesses.

Continuous code blocks reused: frozen input trees, T23 summary parser/byte comparison, and p02
counterbalanced runner logic.

Reuse mode: test-asset / copy-and-adjust.

Required lifecycle adaptations: compare the pre-T25 and final T25 binaries; run p02 OFF only for
storage impact.

Preserved ordering and observable behavior: identical inputs, trace level, binary reuse, run order,
hashing, and parser boundary.

Existing characterization: T23 SUMMARY byte milestone and p02 V5.

Explicit behavior changes: none under SUMMARY; OFF storage only.

Excluded paths: production source changes.

Focused validation: exact complete SUMMARY tree comparison; one warm-up and five serial
counterbalanced pre/post p02 pairs with RSS and wall reporting.

Re-approval triggers: any SUMMARY byte mismatch, scenario failure, or unexplained semantic output
difference.

## Ticket Full Acceptance Suite

Run serially:

1. `ninja -C cmake-cache -j2 unified-bus-test test-runner scratch_ub-quick-example`
2. `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle-storage --verbose`
3. `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle --verbose`
4. Pre/post explicit-SUMMARY complete tree byte comparison using the frozen T23 simple case.
5. One warm-up plus five counterbalanced pre/post p02 OFF pairs, strictly serial.

## Current Diff Scope

Allowed production files:

- `src/unified-bus/model/ub-lifecycle.h`
- `src/unified-bus/model/ub-lifecycle.cc`

Allowed characterization and evidence:

- `src/unified-bus/test/ub-test.cc`
- `.scratch/unified-bus-lifecycle-performance/T25-*`
- T25-local raw evidence under `.scratch/unified-bus-lifecycle-performance/evidence/`

All packet, protocol, transaction, transport, queue, scheduler, routing, congestion-control, and TP
cleanup sources are excluded.

## Validation Ledger

- 2026-07-17: acceptance suite, pre-refactor map, three semantic batches, explicit OFF retired-state
  behavior change, and allowed diff recorded before T25 production edits.
- 2026-07-17: pre-layout focused churn opened, completed, and retired 100,000 distinct WQE local
  IDs with lifecycle trace OFF. It retained 100,000 active-state shells and 100,000 full snapshots;
  focused runner peak RSS was `59,441,152 B`.
- 2026-07-17: post-layout focused churn PASS with the same 100,000 generation-history entries, zero
  active states, zero retained summaries, zero attempt words, and zero leases. Peak RSS was
  `28,491,776 B`, a `-52.07%` change. Local-ID zero reopened at generation two without adding a
  resource slot.
- 2026-07-17: targeted build PASS; `unified-bus-lifecycle-storage`,
  `unified-bus-lifecycle --verbose`, and `unified-bus-lifecycle-trace --verbose` PASS. Existing
  fail-fast children preserve active-lease retirement rejection, duplicate terminal rejection,
  stale generation, and retired mutation failures. Failed/canceled/flushed and forced-stop outcomes
  remain visible through compact snapshots.
- 2026-07-17: frozen pre/post explicit-SUMMARY complete output/runlog artifact trees are byte
  identical. No exception is required.
- 2026-07-17: p02 OFF used one warm-up and five strictly serial counterbalanced pre/post pairs.
  Paired-median wall is `-1.87%`; median RSS changes from `63,356,928 B` to `63,324,160 B`
  (`-0.05%`). This case is effectively flat because it does not create enough retired distinct
  resources for the churn layout to dominate.
- 2026-07-17: independent review against `main@ccf41b58` and the current pre-T25 retirement path
  PASS. No packet builder/parser, protocol state machine, ACK/RTO, TP teardown, queue/scheduler,
  routing, congestion-control, callback, cleanup, or trace order changed. Implementation commit:
  `1afd398`.
