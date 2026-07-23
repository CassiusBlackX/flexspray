# T44 Task Brief: Define Congestion-Control Dormancy Contracts

Status: complete

## Authority And Scope

- Ticket: `tickets.md`, Task 44.
- Feature contract: `.scratch/rtp-tp-memory-production/PRD.md`.
- Current pre-refactor commit: `4dcff70` on `codex/rtp-tp-memory-compaction`.
- This ticket defines the host congestion-control contract and implements it for CAQM and DCQCN.
  It does not add dormant TP records, manager reclaim/resolve behavior, or switch reclamation.
- Existing dirty application, traffic-generator, prototype, and performance-evidence changes are
  excluded and must not be modified, cleaned, staged, or used as the test oracle.

## Confirmed Public Test Seam

The pre-confirmed seam is the public `UbCongestionControl` dormancy contract exercised through the
existing `unified-bus-congestion-control` module suite. Tests use algorithm callbacks and public
observable decisions (eligibility, snapshot/restore result, admission, generated feedback, rate,
window, and RTT); they do not inspect private containers or dormant TP-manager state.

## Batch Reuse Map

Pre-refactor commit: `4dcff70` (feature behavior baseline remains `aa2b26a`).

Semantic batch: add one uniform fail-closed host congestion-control dormancy contract and implement
it for the two currently supported host algorithms.

Old active symbols: `UbCongestionControl`, `UbHostCaqm`, `UbHostDcqcn`,
`UbHostDcqcn::OnSenderTransportIdle`, CAQM sender/receiver callbacks, DCQCN tracked-event helpers,
and `UbCongestionControlTestSuite`.

Old call paths: TP creation -> `UbCongestionControl::Create(UB_DEVICE)` -> `OnTpAttached`; sender and
receiver TP callbacks -> host algorithm state; TP sender drain -> `OnSenderTransportIdle`; object
dispose -> existing event cancellation and state release.

Continuous code blocks reused: CAQM's current window/RTT/aggregation state and DCQCN's existing
sender-idle reset, tracked-event cancellation, CNP cooldown, rate, alpha, byte recovery, and pacing
state. No congestion-control algorithm or event scheduler is duplicated.

Reuse mode: expose a public algorithm-owned opaque snapshot contract in the existing base class;
copy each algorithm's existing dynamic fields into its private snapshot payload and restore those
fields through the same derived implementation.

Required lifecycle adaptations: none in the TP manager. DCQCN eligibility requires the existing
sender-idle reset and all tracked events to be drained. CAQM eligibility requires its congestion
state reset event to be drained; all continuity-bearing sender and receiver fields are snapshotted.

Preserved ordering and observable behavior: packet admission decisions, ACK/CNP generation, CAQM
window and RTT evolution, DCQCN CNP cooldown, rate/alpha recovery, pacing, switch behavior, and
existing idle-reset timing.

Existing characterization: `UbCaqmHostRttUsesSmoothedEstimateTest`,
`UbCaqmReceiverAggregatesLogicalPsnAcrossWrapTest`, `UbDcqcnReceiverSuppressesBurstCnpTest`,
`UbDcqcnSenderCutsRateOnCnpTest`, `UbDcqcnRecoveryTimerIncreasesRateTest`, and
`UbDcqcnIdleFlowCancelsRecoveryStateTest`.

Explicit behavior changes: host CAQM and DCQCN explicitly report dormancy eligibility and can
capture/restore an opaque continuity snapshot. The base implementation, future algorithms that do
not opt in, and switch algorithms remain ineligible without throwing or mutating state.

Excluded paths: TP backing reclamation/rehydration manager, endpoint storage layout, reservation
state, switch congestion-control reclamation, algorithm tuning, trace schema, workload behavior,
prototype paths, and performance runs.

Focused validation: one targeted `unified-bus-test` build followed by the
`unified-bus-congestion-control` suite. RED evidence is captured from the same suite before the
production contract is added.

Re-approval triggers: the contract requires a TP-manager or transport caller change; an algorithm
needs a new idle-reset rule; a pending event cannot be represented by drain eligibility without
changing current behavior; switch CC must participate; a public case/trace schema changes; an old
CC test fails for an unexplained reason; or the same build/test failure repeats twice.

## Planned Vertical Slices

1. Base contract defaults fail closed, while host CAQM and DCQCN opt in only at their proven event
   boundaries.
2. CAQM snapshot/restore preserves the next admission/header/feedback decision and all characterized
   sender/receiver continuity state.
3. DCQCN snapshot/restore after its existing idle reset preserves the next start/admission decision
   and receiver CNP cooldown; pending tracked events remain ineligible.

## Validation Contract

Run serially, never alongside another build or test. Stage only the intended T44 files before the
cached diff check so the untracked task brief is included:

```text
ninja -C cmake-cache unified-bus-test
DYLD_LIBRARY_PATH="$PWD/build/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
  ./build/utils/ns3.44-test-runner --test-name=unified-bus-congestion-control --verbose
git diff --cached --check -- CONTEXT.md \
  src/unified-bus/model/protocol/ub-congestion-control.h \
  src/unified-bus/model/protocol/ub-congestion-control.cc \
  src/unified-bus/model/protocol/ub-caqm.h src/unified-bus/model/protocol/ub-caqm.cc \
  src/unified-bus/model/protocol/ub-dcqcn.h src/unified-bus/model/protocol/ub-dcqcn.cc \
  src/unified-bus/test/ub-test.cc .scratch/rtp-tp-memory-production/T44-task-brief.md
```

No scale simulation, full build, repository-wide `test.py`, canonical-byte milestone, commit, or
push belongs to T44.

## TDD Evidence

- Base contract RED: the focused suite failed to compile because the dormancy API and state type did
  not exist. GREEN: base eligibility is `INELIGIBLE`, snapshot is null, and restore rejects state.
- CAQM RED: only the new continuity case failed; eligibility remained ineligible and RTT, in-flight,
  and receiver aggregation state were not restored. GREEN: the case preserves sender and receiver
  decisions, rejects snapshots while the state-reset event is pending, becomes eligible after that
  event drains, and preserves nonzero accumulated-hint behavior.
- DCQCN RED: the new case had no algorithm override. GREEN: sender recovery remains ineligible until
  the existing idle reset drains tracked events; receiver cooldown survives restore and retains its
  original CNP deadline.
- Final focused result: all 27 `unified-bus-congestion-control` cases pass with the matching runner
  and `build/lib` dylibs.

CAQM snapshots deliberately retain the complete `m_psnSendTimeMap` because current long-idle reuse
retains it. Compacting or clearing that map requires a separate proof that a fully drained TP cannot
observe the old send-time entries; T44 does not change that behavior.
