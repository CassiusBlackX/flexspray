# T28 Task Brief: Final Lifecycle Performance Acceptance

Status: implementation complete; closeout pending

## Scope

Produce the final portable main/current/next semantic and host-performance report for the completed
T23-T27 lifecycle optimizations. T28 changes no Unified Bus production behavior and does not add
automatic TP cleanup.

## Ticket-Defined Acceptance Suite

1. Freeze `main@ccf41b58b0ba3b9711efea595732a2908e3e07cf`, current
   `85c61e8bc4a6a47971f1e7f83e3eced72f47f531`, and
   `next@cf8ee683e00e5b6210661013e93cec7217d662e5`. Record source roots, executable and Unified Bus
   library SHA-256, release build configuration, compiler, Python, macOS/runtime facts, inputs, and
   exact commands. Reuse those artifacts without rebuilding during the matrix.
2. Run p01 no-spray/lifecycle-OFF and p02 routing-compatible/lifecycle-OFF with one warm-up per role
   and five counterbalanced main/current/next measured rounds. Run current p02 OFF/SUMMARY with one
   warm-up per mode and five counterbalanced measured pairs. Execute every run strictly serially.
3. Report monotonic wall, `/usr/bin/time -l` real/user/sys, peak RSS, page faults, block input/output,
   task count, shared trace records/bytes, lifecycle records/bytes, and unique lifecycle resource
   generations where the production SUMMARY parser provides them.
4. Compare shared p01/p02 output and runlog artifacts exactly. Permit only a case/path/hash-bound
   pure same-timestamp TaskTrace reorder after proving equal full-line multisets. Any other mismatch
   is unexplained and fails acceptance.
5. Re-run the frozen complex CTP/RTP matrix against next: CTP windows 1/4/128, shared RTP inflight 1,
   GBN double loss, and TPG loss/pinning. Preserve completion, window response, retransmission,
   selection, queue, and lifecycle balance. Revalidate lifecycle artifacts with the production parser.
6. Bind the already reviewed T05 trace-order, T06 cumulative-ACK descriptor retirement, and T14
   resolved-generation pinning exceptions only to their exact revision, case, path, and hashes. No
   exception may expand to a new path or unexplained byte change.
7. Treat current/main wall or RSS regression above 10 percent as a failed performance gate. Preserve
   negative, flat, exception, and inconclusive results separately; do not average them together.
8. Run `git diff --check`, review the T28 runner/report diff, audit every Task 28 checkbox, and create
   an implementation commit followed by a closeout commit. No repository-wide `./test.py` is required
   because T28 changes no source, public interface, or build configuration.

## Current Diff Scope

- Add this T28 ledger, one final runner, a compact frozen manifest/summary, and the final report.
- Update only Task 28 checkboxes and closeout text in `tickets.md` after evidence exists.
- Raw run trees remain untracked under the T28 evidence directory and are never staged wholesale.
- Exclude every Unified Bus source/header/test file and every pre-existing untracked evidence tree.

## Batch 1 Reuse Map

Pre-refactor commit: `main@ccf41b58b0ba3b9711efea595732a2908e3e07cf`.

Semantic batch: frozen identity, runtime manifest, and reusable serial execution harness.

Old active symbols: `sha256`, `binary_for`, `parse_time_l`, and `run_repetition` in
`run_t24_p01.py` and `run_t23_p02.py`; existing CMake caches and frozen executable/library pairs.

Old call paths: final runner -> `/usr/bin/time -l` -> frozen `ub-quick-example` -> production case
loader and simulator runtime.

Continuous code blocks reused: SHA-256 streaming, `/usr/bin/time -l` field parsing, process-group
timeout handling, stdout/stderr/command/result capture, revision checks, and overwrite refusal.

Reuse mode: copy-and-adjust and test-asset.

Required lifecycle adaptations: bind current to T28 HEAD/library hash; add next to p01; record compiler,
runtime, build cache facts, dynamic library identity, and exact case transforms.

Preserved ordering and observable behavior: all runs remain local-single, release, `rng-run=1`, and
strictly serial. No simulator parameter changes beyond the already characterized p01 no-spray/OFF and
p02 HASH/OFF/SUMMARY compatibility transforms.

Existing characterization: T23 p02 manifest/runner, T24 p01 manifest/runner, and their retained raw
timings and artifact hashes.

Explicit behavior changes: none in the simulator; the final runner records more evidence fields.

Excluded paths: source compilation, protocol implementation, automatic TP cleanup, MTP, MPI, and
parallel runs.

Focused validation: `python3.12 run_t28_acceptance.py freeze` and manifest self-check.

Re-approval triggers: missing or mismatched frozen revision/hash, incompatible build configuration,
unexpected dynamic-library resolution, or need to rebuild more than one missing artifact.

## Batch 2 Reuse Map

Pre-refactor commit: `main@ccf41b58b0ba3b9711efea595732a2908e3e07cf`.

Semantic batch: final p01/p02 host-performance and byte-contract matrix.

Old active symbols: `copy_clean_input`, `artifact_hashes`, `median_summary`, `semantic_comparison`,
and counterbalanced order tables in `run_t24_p01.py` and `run_t23_p02.py`; production lifecycle
`parse_case` in `scratch/ns-3-ub-tools/trace_analysis/lifecycle_trace.py`.

Old call paths: frozen case input -> production `ub-quick-example` -> output/runlog -> existing
hash comparison and lifecycle parser.

Continuous code blocks reused: p01 packet-spray disable and current OFF override; p02 ADAPTIVE-to-HASH
compatibility transform; median/paired delta calculations; artifact hashing; lifecycle volume parsing.

Reuse mode: copy-and-adjust, direct parser use, and test-asset.

Required lifecycle adaptations: use final HEAD, include next in p01, collect production-derived task,
trace, lifecycle-record, and unique-resource counts, and bind any TaskTrace reorder exception to hashes.

Preserved ordering and observable behavior: identical inputs within each role-compatible comparison;
unchanged production packet/task/throughput/trace semantics; counterbalanced orders and serial runs.

Existing characterization: T23 p02 V5 and T24 p01 after-v2 evidence.

Explicit behavior changes: none.

Excluded paths: packet spray, lifecycle DETAIL/EVENT, routing-policy comparisons, protocol edits, and
automatic cleanup.

Focused validation: p01/p02 runner schema checks, one warm-up plus five measured rounds, exact shared
artifact comparison, and production parser validation for current SUMMARY.

Re-approval triggers: unexplained artifact difference, performance regression above 10 percent,
unknown failure, or any input transform outside this map.

## Batch 3 Reuse Map

Pre-refactor commit: `main@ccf41b58b0ba3b9711efea595732a2908e3e07cf`; protocol calibration
control: `next@cf8ee683e00e5b6210661013e93cec7217d662e5`.

Semantic batch: complex CTP window/backpressure and RTP retransmission/selection acceptance.

Old active symbols: materialized inputs and `shared_files`, `read_tasks`, and `analyze` from
`scratch/20260717-next-lifecycle-ctp-rtp-complex-differential/scripts/experiment.py`; production
lifecycle parser; accepted comparison report for exact T05/T06/T14 boundaries.

Old call paths: frozen complex inputs -> next/current binaries -> production task/packet/ACK/port/queue
traces -> exact comparator and lifecycle parser.

Continuous code blocks reused: all six immutable input trees, shared-file hashing, task CSV parsing,
CTP window-effect extraction, serial execution, and lifecycle-only artifact exclusion from shared bytes.

Reuse mode: test-asset, direct parser use, and copy-and-adjust comparator.

Required lifecycle adaptations: replace the earlier lifecycle revision with final HEAD and bind every
accepted exception to the newly observed hashes without broadening its semantic or path boundary.

Preserved ordering and observable behavior: input bytes, `rng-run=1`, next control, completion set,
Jetty window effect, ACK/RTO/GBN state behavior, TPG member pinning, and trace interpretation.

Existing characterization: the complete 2026-07-17 complex differential package and Chinese report.

Explicit behavior changes: none.

Excluded paths: PFC/DCQCN plus RTO interaction, unplanned cases, protocol rewrites, and performance
claims from the one-shot complex semantic runs.

Focused validation: six next/current pairs strictly serial, shared artifact classification, exact task
outcomes, non-degenerate CTP window response, accepted RTP exception checks, and production lifecycle
parser balance/schema validation.

Re-approval triggers: any new changed path, changed task completion set, absent CTP window response,
retransmission migration, lifecycle parser failure, or exception not matching the approved cause.

## Batch 4 Reuse Map

Pre-refactor commit: `main@ccf41b58b0ba3b9711efea595732a2908e3e07cf`.

Semantic batch: final report, independent review, ticket audit, and closeout.

Old active symbols: T23-T27 reports/manifests and Task 28 acceptance bullets.

Old call paths: raw evidence -> compact JSON summary -> human-readable report -> ticket closeout.

Continuous code blocks reused: absolute/percentage result tables, negative-result retention, byte
classification vocabulary, and implementation/closeout two-commit workflow.

Reuse mode: test-asset and copy-and-adjust documentation structure.

Required lifecycle adaptations: aggregate without hiding per-case failures; identify automatic TP
cleanup as excluded; bind revisions/hashes/runtime; state whether the branch passes or fails acceptance.

Preserved ordering and observable behavior: no source edits; reports reflect raw evidence exactly.

Existing characterization: T23-T27 closeout reports and the complex differential report.

Explicit behavior changes: none.

Excluded paths: new optimization, source cleanup, baseline rebuild, and deleting raw evidence.

Focused validation: summary-to-raw spot checks, `git diff --check`, Task 28 requirement audit, and
review against the pre-refactor/next behavior evidence.

Re-approval triggers: evidence cannot prove a checkbox, result conflicts with an earlier approved
boundary, or a production change would be required.

## Ledger

- 2026-07-17: T28 started at current `85c61e8`, main `ccf41b58`, and next `cf8ee683`.
- 2026-07-17: all three frozen executables and Unified Bus libraries exist. Main and next hashes
  match the T23 manifest. Current executable remains `7756e587...`; its T27 library is
  `0968fced...` and will be frozen by the T28 manifest.
- 2026-07-17: no build, test, or simulator process was running at batch start.
- 2026-07-17: first complex replay preserved all shared semantic bytes but emitted no lifecycle
  artifacts because T23 intentionally changed the `minimal` preset from SUMMARY to OFF. Preserve
  that run under `complex/`; the corrected v2 replay explicitly selects current SUMMARY solely to
  exercise the parser and compare the old SUMMARY byte contract. No protocol parameter changes.
- 2026-07-17: p01 final gate FAIL against main at `+16.64%` paired wall and `+14.86%` paired RSS;
  PASS against next at `+7.25%` wall and `+8.42%` RSS. The T24-to-final wall result is effectively
  flat (`+16.65% -> +16.64%`), so the blocker remains visible.
- 2026-07-17: p02 OFF PASS against main (`+5.46%` wall, `+7.83%` RSS) and next (`+2.04%` wall,
  `+5.85%` RSS). One 0.242489 s current outlier remains in raw evidence and does not change the
  pre-agreed paired median.
- 2026-07-17: p02 SUMMARY adds `68,816` records for `3,128` resource generations and costs
  `+27.11%` paired wall / `+1.86%` RSS versus OFF. Shared artifacts are exact.
- 2026-07-17: complex v2 PASS. CTP windows 1/4/128 exactly match next with non-degenerate window
  response. RTP shared/GBN/TPG reproduce the exact archived T05/T06/T14 path and hash boundaries;
  production parser validation passes. No new difference or automatic TP cleanup.
- 2026-07-17: independent runner review tightened TaskTrace approval to require equal full-line
  multisets and identical per-position timestamp sequences. All 80 exercised controlled comparisons
  pass the stronger check. Final semantic status PASS; final performance status FAIL.
