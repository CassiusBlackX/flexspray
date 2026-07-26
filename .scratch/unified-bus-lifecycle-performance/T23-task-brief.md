# T23 Task Brief: Fast Minimal Lifecycle Observability

Status: complete

Branch: `codex/unified-bus-lifecycle-implementation`

Starting revision: `3d1f3632069952808329e6fbbf52e93d1999db3c`

## Ticket Acceptance

1. `minimal` without an explicit lifecycle override resolves to `OFF`; `balanced` resolves to
   `SUMMARY`; `detailed` resolves to `EVENT`; explicit `OFF`, `SUMMARY`, `EVENT`, and `DETAIL`
   override every preset.
2. Explicit `SUMMARY` preserves lifecycle schema metadata, filename construction, header fields,
   record order, parser input, and complete output bytes.
3. Lifecycle output continues through the existing T19 bounded `UbUtils` writer. No second buffer,
   writer, flush policy, descriptor policy, or asynchronous path is introduced.
4. `OFF` and explicit `SUMMARY` preserve lifecycle accounting, completion, task statistics,
   throughput, protocol traces, and simulated time.
5. The production observability preset generator, trace reference, and helper tests publish the new
   minimal contract.
6. The p02 gate pins current `HEAD`, `origin/main@ccf41b58`, and
   `origin/next@cf8ee683`, hashes one reused binary for each revision, and runs one warm-up plus five
   measured counterbalanced pairs serially. It records wall, user CPU, system CPU, peak RSS, file
   I/O, lifecycle record count/bytes, and binary/input hashes.
7. Default p02 current-versus-main wall regression must be no more than 10 percent. A larger or
   unexplained result stops T23 without widening its implementation scope.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`

Old active symbols:

- `UbUtils::GetTraceFile`
- `UbUtils::FlushTraceFile`
- `UbUtils::PrintTraceInfoNoTs`
- `UbUtils::Destroy`

Old call path:

`UbUtils` trace producer -> `GetTraceFile` -> owner/file pending bytes -> threshold flush ->
`Destroy` final flush/close.

Current reused path:

`UbUtils::CreateTraceDir` -> `UbLifecycleTrace::Configure` -> resolved
`UbUtils::PrintTraceInfoNoTs` sink -> T19 bounded owner chunks/file registry -> writer flush/close.

Code blocks copied/extracted: none. The active writer and resolved sink are called directly. The
SUMMARY record construction remains the existing `MakeTraceRecord` plus `EncodeRecord` path.

Required lifecycle adaptations:

- Change only the default `minimal` level selection.
- Add a lifecycle-layer batch entry point that holds the lifecycle trace mutex and resolves the
  owner sink once for an ordered record range.
- Route the existing teardown SUMMARY loop through that batch entry point.

Explicit behavior changes:

- `minimal` changes from lifecycle `SUMMARY` to lifecycle `OFF` when no explicit lifecycle level is
  provided.

Preserved ordering and observable behavior:

- Explicit SUMMARY resource iteration order, per-resource event order, owner sequence increments,
  filename, schema preamble, CSV header, record encoding, newline append, and writer flush order.
- All correctness ledger mutations and validation remain always enabled.
- No packet builder/parser, ACK/RTO, queue/scheduler, congestion-control, callback, or timer path is
  changed.

Characterization evidence:

- `UbLifecycleTraceLevelAndSummaryTest` already proves OFF retains accounting and SUMMARY reads the
  existing ledger without mutating it.
- `UbLifecycleTraceEncodingTest` and the lifecycle parser suite define the production schema and
  encoding contract.
- Existing T19 writer tests define chunk, pressure, and descriptor behavior.
- The archived p02 package isolates SUMMARY overhead and supplies the case inputs.

## Batch 1 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: observability preset policy.

Old active symbols: no pre-refactor lifecycle preset existed. Reuse the current production
`UbLifecycleTrace::ResolveLevel` selection function and OpenUSim `observability_preset` table.

Old call paths: `UbUtils::CreateTraceDir` -> `ResolveLevel`; experiment generation ->
`observability_preset` -> `network_attribute.txt`.

Continuous code blocks reused: existing explicit override parsing and existing balanced/detailed
branches, unchanged.

Reuse mode: direct plus one constant-result adjustment.

Required lifecycle adaptations: split the combined `MINIMAL || BALANCED` branch and publish `OFF`
for minimal in the generator.

Preserved ordering and observable behavior: explicit override precedence and validation remain
before preset resolution; balanced, detailed, custom, error paths, and correctness accounting do not
change.

Existing characterization: `UbLifecycleTraceLevelAndSummaryTest` and
`OpenUSimRunExperimentHelpersTest.test_observability_presets_publish_lifecycle_levels`.

Explicit behavior changes: minimal/no-override `SUMMARY -> OFF` only.

Excluded paths: trace writer, lifecycle ledger, parser, protocol traces, task/throughput logic.

Focused validation:

- `ninja -C cmake-cache unified-bus-test test-runner`
- `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle-trace`
- `python3.12 -m unittest tests.openusim_skills.test_run_experiment_helpers`
- `python3.12 -m unittest tests.openusim_skills.test_skill_docs`

Re-approval triggers: override precedence changes, a new public setting, accounting changes, or an
unexpected failure outside the changed preset assertions.

## Batch 2 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: explicit SUMMARY lifecycle dispatch.

Old active symbols: `UbUtils::GetTraceFile`, `UbUtils::PrintTraceInfoNoTs`, `UbUtils::Destroy`.

Old call paths: trace producer -> resolved owner/file sink -> buffered append -> bounded flush and
close. Current SUMMARY path is `UbUtils::Destroy` -> coordinator `EmitLifecycleSummary` -> repeated
`UbLifecycleTrace::Emit` -> the same resolved sink.

Continuous code blocks reused: current `UbLifecycleTrace::Emit` sink lookup/header initialization/
`EncodeRecord` block and the existing coordinator SUMMARY event sequence.

Reuse mode: minimally extract the existing single-record body into a lock-held helper, keep the old
`Emit` caller using it, characterize unchanged sequential bytes, then add `EmitBatch` and migrate the
SUMMARY caller.

Required lifecycle adaptations: one mutex acquisition and one owner sink resolution for the ordered
SUMMARY record range.

Preserved ordering and observable behavior: identical schema/header initialization, per-record
`EncodeRecord`, sink invocation order, newline behavior, owner sequences, and downstream writer.

Existing characterization: lifecycle trace level/summary test, trace encoding test, lifecycle parser
test, and T19 bounded writer tests.

Explicit behavior changes: none for any explicitly selected lifecycle level.

Excluded paths: EVENT/DETAIL production call sites, metadata encoding, writer buffering, flush/FD
management, lifecycle accounting, and all protocol algorithms.

Focused validation:

- Byte-for-byte in-memory comparison of sequential `Emit` and `EmitBatch` output.
- `ninja -C cmake-cache unified-bus-test test-runner`
- `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle-trace`
- `python3.12 -m unittest scratch.ns-3-ub-tools.trace_analysis.test_lifecycle_trace`

Re-approval triggers: mixed-owner batching, changed bytes/order, writer changes, public ownership
changes, or any unclear test failure.

## Ticket Full Acceptance Suite

Run serially, and do not rerun an unaffected suite after a later documentation-only diff:

1. Targeted build:
   `ninja -C cmake-cache unified-bus-test test-runner ub-quick-example`
2. C++ lifecycle trace suite:
   `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle-trace`
3. Lifecycle parser:
   `python3.12 scratch/ns-3-ub-tools/trace_analysis/test_lifecycle_trace.py`
4. OpenUSim helper and documentation contracts:
   `python3.12 -m unittest tests.openusim_skills.test_run_experiment_helpers`
   and `python3.12 -m unittest tests.openusim_skills.test_skill_docs`.
5. Simple semantic case: run one copied minimal single-RTP WRITE input with default minimal and with
   explicit SUMMARY. Compare task statistics, throughput, non-lifecycle protocol traces, completion,
   and simulated time. Require no lifecycle files for default minimal and a parser-valid tree for
   explicit SUMMARY.
6. SUMMARY milestone: run the same explicit-SUMMARY input with the pre-T23 binary and post-T23
   binary, then compare the complete lifecycle schema and trace trees byte for byte.
7. p02 performance gate: reuse the archived p02 input and frozen revision binaries, execute one
   warm-up plus five counterbalanced pairs for current/main and current/next, separately measuring
   default minimal/OFF and explicit SUMMARY. Preserve all raw timing and I/O records plus hashes.

## Current Diff Scope

Allowed production files:

- `src/unified-bus/model/ub-lifecycle-trace.h`
- `src/unified-bus/model/ub-lifecycle-trace.cc`
- `src/unified-bus/model/ub-lifecycle.cc`

Allowed characterization and contract files:

- `src/unified-bus/test/ub-test.cc`
- `.codex/skills/openusim-run-experiment/scripts/openusim_run_experiment/network_attribute_writer.py`
- `.codex/skills/openusim-references/trace-observability.md`
- `tests/openusim_skills/test_run_experiment_helpers.py`
- `tests/openusim_skills/test_skill_docs.py` only if its existing contract needs an assertion update
- T23-local performance/byte-comparison scripts and evidence under
  `.scratch/unified-bus-lifecycle-performance/`

Explicitly excluded: the pre-existing untracked
`scratch/20260716-gitcode-main-lifecycle-differential/` package, except as read-only source evidence.

## Validation Ledger

- 2026-07-17: task brief and both reuse maps recorded before production edits.
- 2026-07-17: current CMake profile emits the unsuffixed test runner; the stale `-default`
  executable does not load the current test library. Acceptance commands use the built unsuffixed
  runner.
- 2026-07-17: first p02 warm-up stopped after main succeeded because current correctly rejected the
  old input's UbApp `PER_FLOW_SHORTEST_PATHS` declaration with global `ADAPTIVE`. The failed artifacts
  remain under `evidence/p02-performance`. V2 tentatively added the same UbApp packet-spray line to
  every revision before the active transport path disproved that producer-specific approach.
- 2026-07-17: v2 then exposed the same incompatibility on the active RTP transport path, so the
  producer-specific packet-spray adjustment was rejected rather than expanded. Its failed artifacts
  remain under `evidence/p02-performance-v2`. V3 instead changes the shared performance input's one
  global routing line from `ADAPTIVE` to `HASH`, preserving per-flow UbApp/TP behavior on all three
  revisions while retaining p02's resource scale. No production routing code is changed.
- 2026-07-17: targeted build PASS; focused lifecycle trace suite PASS; parser 8 PASS; OpenUSim helper
  8 PASS; documentation 20 PASS.
- 2026-07-17: simple default-minimal and explicit-SUMMARY task/throughput/non-lifecycle artifacts are
  byte-identical; default emits no lifecycle files; SUMMARY has 198 parser-valid records.
- 2026-07-17: pre/post complete explicit-SUMMARY runlog trees are byte-identical without exception.
- 2026-07-17: accepted p02 V3 has current/main wall `+5.26%`, current/next wall `0%`, current/main RSS
  `+8.68%`, and current/next RSS `+6.54%`. Explicit SUMMARY costs `+35%` wall and emits 68,816
  parser-valid records / 5,641,986 bytes.
- 2026-07-17: V4 pinned the final implementation commit and runtime-library hashes, but its primary
  wall metric accidentally used macOS `/usr/bin/time`'s 10 ms rounded value. At 0.17-0.21 s per run,
  that quantization produced an apparent `+11.76%` median. V5 restores the old harness's monotonic
  high-resolution wall metric while retaining `time -l` for CPU, RSS, faults, and I/O.
- 2026-07-17: final V5 paired-median wall is current/main `+6.02%` and current/next `+1.46%`;
  current/main RSS is `+8.78%` and current/next RSS is `+6.61%`. Explicit SUMMARY costs `+28.71%`
  wall, `+1.87%` RSS, and emits 68,816 parser-valid records / 5,641,986 bytes. Final-commit SUMMARY
  output remains byte-identical to pre-T23.
- Implementation commit: `17cf695785a98ac5697d957a639c7ec71ef7efb3`.
- Standards review: PASS. No parallel writer, builder/parser, state machine, ACK/RTO, queue/scheduler,
  or congestion implementation was added; old writer and record encoder are directly reused.
- Spec review: PASS. The only default behavior change is recorded minimal `SUMMARY -> OFF`; explicit
  SUMMARY bytes/order and lifecycle accounting are preserved. The p02 same-timestamp TaskTrace order
  difference is inherited from T05 and canonicalizes identically.
