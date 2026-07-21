# Unified Bus Lifecycle Final Performance Report

Date: 2026-07-21

Promotion branch: `codex/unified-bus-lifecycle-implementation`

Behavior baseline: `main@ccf41b58`

Protocol and performance control: `next@cf8ee683`

## Scope and method

This report consolidates the accepted performance evidence already present in the branch. No build,
simulation, or performance measurement was rerun for this report.

The measurements are host-runtime results from local macOS release binaries, not simulated network
throughput or hardware measurements. Unless a row says otherwise, runs used identical materialized
inputs, `rng-run=1`, disabled legacy and lifecycle trace output, one warm-up per role, strictly serial
counterbalanced pairs, and macOS `/usr/bin/time -l`. Percentages are medians of paired treatment-minus-
control deltas. Positive wall/CPU/RSS values are regressions; negative values are improvements.

Results are bound to their recorded revisions. Later owner-only or documentation commits are not
silently treated as fresh measurements. The report identifies the latest applicable measured
checkpoint for each path and keeps historical diagnosis separate from final production results.

## Final decision matrix

| Path / contract | Control | Latest applicable result | Decision |
|---|---|---|---|
| p01 frozen packet-heavy gate | main | +16.64% wall, +14.86% RSS | FAIL 10% gate |
| p01 frozen packet-heavy gate | next | +7.25% wall, +8.42% RSS | PASS 10% gate |
| corrected LDST-specific calibration | next | +18.12% wall, +19.28% user CPU | residual remains |
| p02 resource-heavy, lifecycle OFF | main | +5.46% wall, +7.83% RSS | PASS |
| p02 resource-heavy, lifecycle OFF | next | +2.04% wall, +5.85% RSS | PASS |
| ordinary RTP, 256 MiB | next | +1.65% wall | common path acceptable |
| RTP GBN/RTO, 512 MiB | next | +2.51% wall | common path acceptable |
| PFC/DCQCN, 512 MiB | next | +0.60% wall | common path acceptable; pre-T37 measurement |
| final CTP, 2 GiB | next | +4.20% wall, +4.05% CPU, -0.74% RSS | PASS |
| final CTP, 8 GiB | next | -0.01% wall, +0.26% CPU, -0.42% RSS | PASS |
| explicit SUMMARY on p02 | lifecycle OFF | +27.11% wall, +1.86% RSS | expected opt-in trace cost |

The branch is suitable as the next-generation `next` candidate with two explicit boundaries:

- packet-heavy LDST remains slower in the corrected production calibration;
- automatic TP cleanup is not implemented and contributes no claimed runtime or memory benefit.

## Frozen promotion gates

T28 used frozen `main@ccf41b58`, current `85c61e8`, and `next@cf8ee683` release binaries. Each role
ran one warm-up and five measured counterbalanced rounds.

### p01 packet-heavy

| Role | Median wall | User | System | Peak RSS |
|---|---:|---:|---:|---:|
| main | 1.474950 s | 1.38 s | 0.06 s | 19,791,872 B |
| current | 1.720335 s | 1.63 s | 0.06 s | 22,806,528 B |
| next | 1.599620 s | 1.50 s | 0.06 s | 21,086,208 B |

Current versus main measured `+16.64%` paired wall and `+14.86%` RSS. Current versus next measured
`+7.25%` paired wall and `+8.42%` RSS. All ten shared-artifact comparisons were byte-identical. Four
of five current/main wall pairs clustered at about 16%-18%; the remaining slower-main round did not
change the paired median.

T24 had already reduced the same main-relative wall regression from `+20.19%` before its change to
`+16.65%`. T28 measured `+16.64%`, so T25-T27 did not remove the remaining always-on packet lifecycle
cost.

### p02 resource-heavy

| Role | Median wall | User | System | Peak RSS |
|---|---:|---:|---:|---:|
| main | 0.159608 s | 0.12 s | 0.03 s | 58,802,176 B |
| current | 0.168380 s | 0.12 s | 0.03 s | 63,422,464 B |
| next | 0.163624 s | 0.12 s | 0.03 s | 59,916,288 B |

Current versus main measured `+5.46%` paired wall and `+7.83%` RSS. Current versus next measured
`+2.04%` paired wall and `+5.85%` RSS. One retained host outlier reached about +47%-52% wall; the
predeclared five-round paired median was used without deleting that run. The only non-byte-identical
files were eight previously approved same-timestamp TaskTrace line reorderings.

T23's earlier p02 checkpoint measured `+6.02%` wall versus main and `+1.46%` versus next. T28 is the
later frozen gate and supersedes those exact percentages while preserving the same pass conclusion.

## Lifecycle trace cost

The final p02 OFF/SUMMARY block used one warm-up and five counterbalanced pairs on the same current
binary and input.

| Mode | Median wall | Peak RSS | Records | Resources | Lifecycle bytes |
|---|---:|---:|---:|---:|---:|
| OFF | 0.166396 s | 63,471,616 B | 0 | 0 | 0 |
| SUMMARY | 0.212819 s | 64,651,264 B | 68,816 | 3,128 | 5,641,986 B |

SUMMARY costs `+27.11%` paired wall and `+1.86%` RSS over OFF. Every task, throughput, and shared
non-lifecycle artifact remains byte-identical. This is explicit observability cost, not always-on
lifecycle correctness cost. The production `minimal` preset therefore keeps lifecycle output OFF.

A separate bounded trace-level run measured the writer itself:

| Level | Run wall | Total wall | Peak RSS | Records | Bytes | Writes / flushes |
|---|---:|---:|---:|---:|---:|---:|
| OFF | 3.584 ms | 54.924 ms | 28,000,256 B | 0 | 0 | 10 / 10 |
| SUMMARY | 7.420 ms | 58.449 ms | 28,196,864 B | 198 | 18,055 B | 13 / 13 |
| EVENT | 5.513 ms | 56.533 ms | 28,082,176 B | 213 | 18,579 B | 13 / 13 |
| DETAIL | 5.487 ms | 55.847 ms | 28,147,712 B | 246 | 19,770 B | 13 / 13 |

These are single regression-scale runs, not statistically powered comparisons. They establish
bounded output and byte-independent modeled results.

The T19 CTP writer chunk experiment recorded:

| Chunk | Writes / flushes | Pressure flushes | Total wall | Peak RSS |
|---|---:|---:|---:|---:|
| 16 KiB | 885 / 885 | 0 | 393.081 ms | 18,268,160 B |
| 64 KiB | 252 / 252 | 0 | 358.437 ms | 18,153,472 B |
| 256 KiB | 205 / 205 | 23 | 435.237 ms | 18,366,464 B |

The retained default is 64 KiB because it had the lowest measured total wall without pressure
flushes.

## Module-level performance

T35 separated common modules using `next@cf8ee683` and lifecycle `51c60c5`, with three pairs per
ordinary module case.

| Workload | Payload | Next median wall | Current median wall | Paired wall |
|---|---:|---:|---:|---:|
| ordinary RTP, 8 flows | about 256 MiB | 1.512 s | 1.547 s | +1.65% |
| ordinary CTP, 4 flows | 512 MiB | 1.561 s | 1.786 s | +14.73% |
| RTP GBN/RTO | 512 MiB | 1.612 s | 1.652 s | +2.51% |
| PFC/DCQCN, 2 flows | 512 MiB | 1.254 s | 1.266 s | +0.60% |

The PFC/DCQCN number predates T37's restoration of idle sender reset semantics. T37 preserved the
pre-refactor reset block and ran correctness and bounded differential validation, but did not repeat
this host-performance block. The `+0.60%` figure is therefore evidence for the pre-T37 checkpoint,
not a fresh final-HEAD measurement.

### CTP before optimization

| Payload | Pairs | Next wall | Current wall | Wall delta | Next RSS | Current RSS |
|---|---:|---:|---:|---:|---:|---:|
| 512 MiB | 3 | 1.561 s | 1.786 s | +14.73% | 15.5 MB | 25.9 MB |
| 2 GiB | 3 | 6.149 s | 7.074 s | +15.04% | 15.4 MB | 57.7 MB |
| 4 GiB | 2 | 12.270 s | 14.174 s | +15.52% | 15.4 MB | 99.9 MB |

Profiles attributed the regression to receiver-only packets repeatedly constructing, zeroing, and
retiring a send-side CTP ACK-window context. Packet transfer observation with lifecycle OFF measured
only about 0.06%-0.11%; packet birth/terminal bookkeeping was generally about 1%-3%; tracked events
were 0.32% for ordinary RTP, 1.39% for GBN/RTO, and 1.67% for DCQCN. These were not the primary CTP
problem.

### Final CTP result

T36 removed receiver-only send-window allocation, treated each CTP TP Packet as one complete
transaction operation, retained pre-refactor queue allocator identity, and compacted trace-OFF
retired history. All CTP bytes and task rows remained identical to next.

| Payload | Pairs | Wall delta | User CPU | RSS delta | Current RSS | Next RSS |
|---|---:|---:|---:|---:|---:|---:|
| 512 MiB | 1 | -0.67% | -0.58% | -0.74% | 15,433,728 B | 15,548,416 B |
| 2 GiB | 5 | +4.20% | +4.05% | -0.74% | 15,335,424 B | 15,499,264 B |
| 8 GiB | 5 | -0.01% | +0.26% | -0.42% | 15,384,576 B | 15,417,344 B |

At 2 GiB the wall gap recovered 10.84 percentage points, passing the required eight-point recovery,
5% final wall-gap, and 10% RSS-gap gates. The independent five-pair 8 GiB replay supersedes an
earlier noisy three-pair `+8.08%` wall block. It measured effectively neutral wall and kept RSS at
next scale instead of growing with payload.

## LDST-specific residual

T34 corrected the macOS dynamic-library harness so each role loaded its frozen dylibs after
`/usr/bin/time`. The homogeneous build control measured only `+0.32%` wall, confirming the corrected
harness itself was neutral.

In the stronger six-block three-role calibration:

| Comparison | Wall | User CPU | Semantic artifacts |
|---|---:|---:|---|
| production LDST vs next | +18.12% | +19.28% | exact 6/6 |
| typed-tracked prototype vs next | +4.75% | +4.98% | exact 6/6 |
| typed-tracked prototype vs production | -9.22% | -9.76% | exact 6/6 |

The prototype preserves tracked-event cancellation and exact artifacts but is not in this branch.
LDST is expected to be redesigned, so the branch retains the measured production residual instead
of adding prototype-specific scheduling complexity.

The older five-pair homogeneous block measured production `+29.27%` wall, `+31.48%` user CPU, and
`+5.26%` RSS versus next. Host performance state changed between blocks; paired medians from separate
blocks are not algebraically composable. The six-block three-role calibration is the stronger current
LDST estimate, while T28 remains the immutable promotion gate.

## Storage and rejected optimizations

| Ticket / experiment | Result | Production decision |
|---|---|---|
| T25 retire 100,000 WQEs | RSS 59,441,152 B -> 28,491,776 B (-52.07%) | retained |
| T25 p02 pre/post | -1.87% wall, -0.05% RSS | retained; effectively flat p02 |
| T26 RTP resolution count | direct/TPG 4 -> 1; m04 median 0.07 s vs 0.07 s | retained; no scenario gain claimed |
| T27 retransmission timer loop | 5,262 us -> 4,305 us (-18.19%) | retained seam-level fast path |
| T29 allocator batch prune | +0.77% wall | rejected |
| T29 allocator generation slots | -0.23% wall, +0.08% RSS | rejected as noisy/sub-percent |
| T30 TypeId coordinator lookup | -0.82% wall, 3/5 non-regressing, +0.33% RSS | rejected; gate failed |
| T31 fused owner resolution | +0.50% wall, +1.54% CPU, 0/5 non-regressing | rejected |
| T32 OFF transfer fast path | -0.97% wall, -1.64% CPU, -0.08% RSS | retained |
| T33 OFF startup setup | exact artifacts; no repeated timing claim | retained cleanup |

T25 releases heavy active state after valid retirement but preserves compact generation history. It
does not reclaim idle TP endpoints. T26 reduces repeated RTP resolution and retained TPG vector
construction; its three-pair m04 case was flat at the available 10 ms wall resolution. T27 reduces
1,000,001 SELECTIVE outstanding queries to one in the focused million-call loop. Its bounded fault
scenario varied from 5.466 ms pre-change to 8.483 ms after change and is explicitly retained as a
noisy negative observation, not an end-to-end gain. T29-T31 are negative results: their code was
reverted and their percentages must not be counted as branch gains. T32 is the retained OFF-path
optimization.

Additional allocation evidence remains bounded but is not expressed as a branch-wide speedup:

- 100,000 route hot-path lookups allocate no temporary candidate or best-port containers;
- small packet queues use inline storage and release overflow capacity after drain;
- descriptor-backed retransmission retains protocol descriptors rather than full wire packets;
- ordinary empty VOQs and idle TP endpoints remain retained by policy.

## Parallel runtime smoke cost

The cross-owner runtime measurements are semantic smoke-scale absolute costs, not a scalability
benchmark and not comparable across process counts as one aggregate wall number.

| Runtime | Per-process total wall | Peak RSS |
|---|---:|---:|
| local-single | 54.300 ms | not resampled after the local variant switch |
| local-MTP, two threads | 54.960 ms | 18,006,016 B |
| MPI, two ranks | 58.076-58.119 ms | 21,970,944-22,003,712 B per rank |
| hybrid, two ranks x two threads | 58.522-58.584 ms | 21,905,408-21,987,328 B per rank |

T41 later replayed local-MTP, MPI, and hybrid after T39. Those runs were correctness and exact-byte
gates, not performance samples; they completed with zero finite lifecycle residue and byte-identical
accepted m18 trees.

## Evidence index

Authoritative human-readable reports:

- `T28-report.md`: frozen p01/p02 gates, SUMMARY cost, and complex semantic checks.
- `T26-report.md`: RTP resolution counts and the flat bounded m04 result.
- `T27-report.md`: retransmission timer microbenchmark and noisy scenario observation.
- `T34-report.md`: corrected LDST calibration and typed-tracked prototype.
- `T35-report.md`: module split, pre-fix CTP scaling, and profiles.
- `T36-report.md`: final CTP correctness, 2 GiB and 8 GiB performance.
- `T40-report.md` and `T41-report.md`: promotion and post-T39 closure.

Authoritative structured summaries:

- `T28-summary.json`
- `T34-summary.json`
- `T35-summary.json`
- `T36-summary.json`
- `T25-summary.json`
- `evidence/t29-allocator/runs-v2/summary.json`
- `evidence/t30-coordinator-lookup/runs-v1/summary.json`
- `evidence/t31-owner-resolution/runs-v1/summary.json`
- `evidence/t32-off-transfer/runs-v1/summary.json`

Invalid or non-authoritative measurements remain excluded:

- T34 cross-library `runs-v1`, where macOS SIP stripped `DYLD_LIBRARY_PATH` through
  `/usr/bin/time`;
- early homogeneous v1 attempts before role-specific library loading was corrected;
- `homogeneous-next-vs-t20-v3`, where lifecycle trace output was accidentally enabled;
- T36's earlier noisy three-pair 8 GiB block, superseded by the independent five-pair replay.
