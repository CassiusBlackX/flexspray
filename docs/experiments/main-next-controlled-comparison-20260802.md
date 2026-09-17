# Main Versus Next Controlled Comparison

## Executive Decision

This report records the controlled comparison used to decide whether `next` can become the
successor to `main`. The result supports `next` as a release candidate and long-term successor:
the frozen matrix found no blocking correctness issue, the common profile reduced CPU work and
runtime in every measured scenario, and the fully optimized profile substantially reduced memory
on dense lifecycle workloads. This is a branch-readiness conclusion, not a release announcement.

The result does **not** support the stronger claim that `next` always uses less memory. With
lifecycle cleanup disabled, `next` used 6-19% more max RSS in the measured P1 cases. Memory savings
appear when the product profile enables task-scoped Jetty cleanup and planned-TP connection cleanup,
and are largest when object lifetimes are exercised by dense, phased traffic.

| Decision question | Result | Evidence-backed interpretation |
| --- | --- | --- |
| Does `next` preserve required functional behavior? | Pass | C1-C3 contain 104 successful records, 52 main/next pairs, and zero blocking pairs. |
| Are non-identical outputs explained? | Pass | Differences are timestamp/order changes, documented route selection changes, or accounted 40-byte CBFC control frames. |
| Are FCT distributions equivalent where equivalence is expected? | Pass | S04, S05, and S07 meet registered bounds; S11 companion meets all registered bounds. |
| Is the large S06 FCT change valid? | Pass, intended improvement | Better path balance and lower queue occupancy explain the improvement without extra DCQCN/PFC evidence. |
| Is branch-only `next` faster? | Yes in the measured P1 set | Instructions fell 9.4-21.0%; wall time fell 5.8-24.6%. |
| Is branch-only `next` smaller in RSS? | No | P1 max RSS rose 6.3-18.7% with lifecycle cleanup disabled. |
| Is fully optimized `next` faster and smaller? | Workload-dependent | S10 and S11 are faster and use 17.9% and 57.0% less max RSS; S06 is faster but uses 5.4% more max RSS. |
| Can `next` succeed `main` now? | Yes, as a staged successor | No blocking correctness finding remains; release timing remains a separate decision. |

## Why This Experiment Was Needed

Earlier ad hoc comparisons mixed branch revisions, defaults, routing representations, trace levels,
and cleanup settings. Some apparent regressions were therefore configuration differences rather
than branch behavior. The final experiment was rebuilt as a controlled matrix with frozen source
revisions, one semantic fixture per scenario, matched build/runtime controls, separate correctness
and performance runs, warm-ups, five formal performance repeats, and paired statistical analysis.

The comparison also had to separate three questions:

| Question | Comparison block | Why it is separate |
| --- | --- | --- |
| Did the branch change simulation behavior incorrectly? | C1-C3 | Correctness needs traces and semantic oracles; trace overhead invalidates performance timing. |
| What is the cost of branch code alone? | P1 | Cleanup and structured-rule compute are disabled or normalized to isolate the branch delta. |
| What will users see with all intended optimizations? | P2 | The product profile intentionally combines branch, cleanup, allocator, and routing changes. |
| Which optimization causes which effect? | A1-A3 | Ablations prevent P2's interaction result from being attributed to one feature. |

## Changes Under Validation

| Change area | Intended behavior | Risk that required evidence | Validation used |
| --- | --- | --- | --- |
| Packet hot-path speedup | Reduce allocator and packet-processing CPU work | A speedup could change packet choice, completion, or event timing | C1-C3 semantic checks, P1 branch-only timing, and A1 direct-completion ablation |
| DWRR flow-control rollback | If a preselected grant becomes FC-limited during allocation and egress is also full, avoid clearing deficit before quantum rollback and prevent unsigned underflow | An incorrect condition could accumulate deficit or alter later packet service | Focused allocator tests plus S03 in local, MTP2, MTP4, P1, and A1 |
| DWRR readability cleanup | Rename the local `vlanEmpty` state to `vlHasNoEligibleQueue` without changing the production condition | A naming cleanup must not reintroduce the earlier incorrect review condition | Diff review and focused tests; production remains `vlHasNoEligibleQueue && !egressBlocked` |
| Traffic dependency visibility | Use an explicit user-facing latency with a nonzero default, separate from allocator and physical-link delay | Single-thread and MTP could otherwise expose dependencies at inconsistent times | Resolved-config preflight and S09 local/MTP comparisons |
| Reverse planned-TP accounting | Count reverse tasks that reuse an existing bidirectional connection before destroying planned TP state | Premature destroy caused a dense AllToAll completion plateau | Focused reverse-direction regression, lifecycle suite, and all A2 S10/S11 profiles |
| Lifecycle cleanup | Release task-scoped Jetty and planned-TP state when no longer needed | Cleanup could create stale references or change protocol completion | A2 2x2 factorial, P2 dense cases, lifecycle tests, and S11 correctness companion |
| Structured routing runtime | Compute from structured rules instead of relying only on legacy/compressed tables | Route lowering or representation could change path semantics and hide offline cost | C2 path evidence, A3 representation ablation, P2 product profile, and separate offline timing |

The performance matrix is frozen at `7d700ed5`. Later commits on the staged `next` branch add and
refresh lifecycle compatibility coverage; they do not retroactively change the recorded P1/P2
numbers. Before an actual release, the matrix must be refreshed against the final release SHA.

## Frozen Provenance And Controls

| Item | Main | Next / shared value |
| --- | --- | --- |
| Source revision | `c044d5bf1016da47ddc66ac8573eff44d228a143` | `7d700ed5704d58b46dfba954fe98c742ed695036` |
| Shared tools revision | `29009175f1778f76a22d929063c3a0821cb91e56` | Same |
| Compiler/platform | Apple clang 21.0.0, macOS 26.5.1 arm64 | Same |
| Build | Release, Unified Bus, MTP on, MPI off, Ninja | Same |
| Link rate and delay | 400 Gbps, 100 ns | Same |
| Allocation time | 5 ns | Same |
| Traffic dependency visibility | 100 ns | Same |
| Forwarding delay | 0 ns | Same |
| Default comparison flow control | CBFC | Same |
| Input generation | One semantic fixture, branch-specific syntax lowering only | Same input hashes checked in preflight |
| Performance execution | Serial, one warm-up, five formal repeats | Same paired order and host |
| CPU evidence | Retired instructions and cycles | Primary metrics |
| Time evidence | Runtime total, simulation-only time, wall time | Wall time treated as secondary host-noise-sensitive evidence |
| Memory evidence | Process max RSS and runtime peak memory | Host-process memory, not modeled switch SRAM |

The main matrix contains 320 successful current records: 284 formal records and 36 warm-ups.
Superseded runs are retained as diagnostic evidence but excluded from every final statistic.

## Scenario Matrix

| ID | Topology and traffic | What it stresses | Where used | Functional conclusion | Performance conclusion |
| --- | --- | --- | --- | --- | --- |
| S01 | 2 hosts, one RTP write | No-contention byte baseline | C1 | Byte-exact canonical output and raw payload | Not a performance target |
| S02 | 8 hosts, multi-TP and multi-priority | TP/VL/RR behavior | C1 | Same events and raw payload; canonical order differs | Not measured |
| S03 | DWRR contention with FC-blocked grant | FC transition and deficit rollback | C1, C3, P1, A1 | Timestamp-only difference; events/payload match in local and MTP | P1 wall -15.9%, RSS +8.4%; direct completion reduces instructions by about 8.6% |
| S04 | 32-host Clos incast | Dynamic PFC and DCQCN | C2, P1 | Ten seeds preserve completion/bytes; FCT ratios are 1.0000 | P1 wall -24.6%, RSS +10.2% |
| S05 | 32-host Clos packet spray | Per-packet routing and path distribution | C2, P1, A3 | Payload exact; valid path allocation differs under selector lowering | P1 wall -9.2%, RSS +6.3%; structured rules reduce instructions about 2.9% |
| S06 | 32-host Clos adaptive hotspot | Adaptive routing and FCT distribution | C2, P2 | Intended FCT improvement explained by balanced paths and smaller queues | P2 wall -20.7%, RSS +5.4% |
| S07 | 4x4 full mesh RTP multipath AllToAll | TP multiplicity and path balance | C2, P1 | Completion/bytes exact and FCT within bounds | P1 wall -5.9%, RSS +18.7% |
| S08 | 32-host Clos CTP windowed traffic | CTP entities, windows, and ACKs | C1 | Event identity and total bytes match; per-port allocation differs as documented | Not measured |
| S09 | 4x4 full-mesh dependency DAG and broadcast | Dependency visibility and makespan | C1, C3, P1 | Timestamp-only difference; events/payload match in local and MTP | P1 wall -8.3%, RSS +8.6% |
| S10 | 64-host Clos dense AllToAll | Allocator CPU, MTP, and object pressure | C3, P1, A1-A3, P2 | MTP task/path evidence matches; byte delta is fully accounted CBFC control traffic | P1 wall -8.5%, RSS +16.1%; P2 wall -11.6%, RSS -17.9% |
| S11 | 128-host Clos, four sequential 32-rank AllToAll waves | Lifecycle release and peak RSS | P1, A2, P2, correctness companion | 3968 task identities and phase ordering match; all FCT bounds pass | P1 wall -5.8%, RSS +16.6%; P2 wall -7.0%, RSS -57.0% |
| S12 | Bounded selective retransmission fault | Loss, timer, and retransmission state | C1 | Timestamp-only difference; events and raw payload match | Not measured |

Negative percentages mean `next` used less CPU, time, or memory than `main`; positive RSS values
mean `next` used more memory.

## Correctness Results

| Block | Design | Records / pairs | Result | What non-exact output means |
| --- | --- | ---: | --- | --- |
| C1 | Deterministic local cases, normalized main vs next | 12 / 6 | 1 matched, 5 partially matched, 0 blocking | S01 is byte-exact. Other pairs preserve event identities and payload while timestamps, ordering, or documented port choice changes. |
| C2 | Four routed/contention cases, ten paired seeds each | 80 / 40 | 40 partially matched, 0 blocking | Packet/task bytes and completion match. Routing changes can alter path shares; registered FCT bounds and path evidence decide validity. |
| C3 | S03/S09/S10 under MTP2 and MTP4 | 12 / 6 | 6 partially matched, 0 blocking | S03/S09 differ only in timestamps. S10's port-byte delta equals accounted 40-byte CBFC frames, with credit send/restore accounting closed. |

`partially_matched` does not mean an unexplained regression. It means byte-for-byte canonical text
was not identical, but the stronger semantic checks required for that scenario passed and the
difference was classified before the final verdict.

### FCT Statistical Comparison

Ratios are `next / main`; 1.0 means equal, values below 1.0 mean shorter FCT on `next`.

| Scenario | Median | p95 | p99 | Makespan | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| S04 Clos incast | 1.0000 | 1.0000 | 1.0000 | 1.0000 | Statistically equivalent |
| S05 packet spray | 0.9987 | 0.9963 | 0.9965 | 0.9966 | Statistically equivalent |
| S06 adaptive hotspot | 0.8522 | 0.7842 | 0.7843 | 0.7843 | Intended improvement, not equivalence |
| S07 full-mesh multipath | 1.0003 | 1.0139 | 1.0264 | 1.0271 | Within registered equivalence bounds |
| S11 P1 companion | 1.005500 | 1.006305 | 1.006431 | 1.006614 | Within registered equivalence bounds |
| S11 P2 companion | 1.005500 | 1.006305 | 1.006431 | 1.006614 | Within registered equivalence bounds |

S06 is the only deliberately non-equivalent FCT result. Its path/branch Jain balance improves from
about 0.13 on `main` to about 1.00 on `next`; maximum queue occupancy falls from about 2.3-2.4 MB
to about 0.64-0.68 MB, without increased DCQCN/PFC evidence. The lower FCT is therefore the expected
effect of corrected adaptive tie handling.

The focused S11 companion executes 3968 tasks and 1,040,187,392 input bytes per run. Main and next
have the same event identity multiset and valid four-wave phase ordering. Canonical text differs in
timestamps/order, but every registered FCT bound passes. On `next`, P1 and P2 differ for only one of
3968 task FCTs, by at most five simulation time units; cleanup and structured routing therefore do
not explain the main/next FCT delta.

## Performance Results

### P1: Branch-Only Common Profile

P1 disables lifecycle cleanup and uses legacy CSV routing on both branches. It measures the branch
implementation delta without claiming product-profile memory savings.

| Scenario | Instructions | Cycles | Wall time | Max RSS | Runtime total |
| --- | ---: | ---: | ---: | ---: | ---: |
| S03 DWRR/FC | -14.3% | -16.1% | -15.9% | +8.4% | -19.4% |
| S04 Clos incast | -21.0% | -22.1% | -24.6% | +10.2% | -24.7% |
| S05 packet spray | -11.0% | -8.8% | -9.2% | +6.3% | -8.9% |
| S07 full-mesh multipath | -9.4% | -5.9% | -5.9% | +18.7% | -6.2% |
| S09 dependency DAG | -11.4% | -6.9% | -8.3% | +8.6% | -8.9% |
| S10 dense AllToAll | -14.8% | -8.2% | -8.5% | +16.1% | -8.5% |
| S11 phased AllToAll | -10.7% | -5.1% | -5.8% | +16.6% | -5.9% |

P1 supports a robust CPU-efficiency claim: every measured case retires fewer instructions and runs
faster. It rejects a branch-only memory-saving claim: every measured case has higher max RSS.

### P2: Fully Optimized Product Profile

P2 compares main's best common profile against `next` with direct completion, both lifecycle
cleanup policies, and structured-rule compute. It is an interaction study, so A1-A3 are needed for
attribution. Offline materialization is timed separately from runtime and simulation-only time.

| Scenario | Instructions | Cycles | Wall time | Max RSS | Runtime peak memory | Runtime total | Offline materialization |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| S06 adaptive hotspot | -24.6% | -20.9% | -20.7% | +5.4% | +2.2% | -21.7% | -12.2% |
| S10 dense AllToAll | -17.3% | -11.7% | -11.6% | -17.9% | -19.3% | -11.4% | -5.1% |
| S11 phased AllToAll | -13.6% | -7.0% | -7.0% | -57.0% | -60.7% | -7.0% | -0.3% |

The product profile is both faster and smaller on S10/S11. S06 is much faster but slightly larger,
because its workload does not create the long-lived object population needed for cleanup benefits
to outweigh baseline branch overhead.

### Optimization Attribution

| Block | Controlled change | Scenarios | Result and interpretation |
| --- | --- | --- | --- |
| A1 | Direct allocator completion off vs on | S03, S10 | Lowers instructions and runtime; RSS is essentially unchanged. S03 instructions fall about 8.6%. |
| A2 | Task-scoped Jetty cleanup and planned-TP cleanup, 2x2 factorial | S10, S11 | All 48 warm-up/formal records complete. Cleanup lowers peak memory; planned-TP cleanup alone lowers S10 max RSS about 12.6%, with the strongest combined effect on phased S11. |
| A3 | Compressed-table lookup vs structured-rule compute | S05, S10 | S05 instructions fall about 2.9%; S10 runtime rises in this sample. Structured rules are not a universal speedup. |
| P2 | All intended optimizations together | S06, S10, S11 | Interaction result: faster in all three; memory benefit appears on dense lifecycle workloads. |

## Problems Found And Corrections Made

| Problem encountered | Why it invalidated or distorted evidence | Correction and final status |
| --- | --- | --- |
| A stale local `next` predated speedup integration | Branch labels alone did not identify tested code | Freeze exact remote SHAs and binary hashes; all final records use the frozen revisions above. |
| Branch-native defaults differed | Scheduler, latency, routing, and lifecycle defaults became hidden variables | Generate one semantic fixture and compare resolved configs after branch-specific syntax lowering. |
| Allocation and dependency latency were conflated | Earlier 10 ns allocator setting did not represent traffic dependency visibility | Pin `AllocationTime=5ns`, link delay 100 ns, and traffic dependency visibility 100 ns separately. |
| `EnableMultiPath` was omitted in one normalized mapping | TP multiplicity changed, creating a false branch difference | Fix mapping and rerun affected comparisons. |
| Obsolete automatic-reclaim setting was assumed | Current `next` has different lifecycle controls | Use the actual Jetty/planned-TP cleanup 2x2 factorial. |
| Correctness and performance traces were mixed | Trace generation materially changes CPU, wall, and memory | Use traced correctness companions and untraced performance runs. |
| Parser revisions differed | Derived interval rows changed despite matching raw bytes | Freeze one tool revision and use raw packet/port evidence as authoritative. |
| S03 CBFC credit configuration deadlocked | Initial credit was exhausted before forced credit return; a 13.9 GB progress log resulted | Pin common credit to 6553 cells and threshold to 1024; classify the old run as invalid fixture evidence. |
| Next transport route type was not lowered consistently | Application and transport routing validation disagreed | Write the same semantic routing type to both next attributes and rerun C2. |
| HASH and HASH64 selected different valid paths | Per-port totals differed even with identical payload | Compare packet counts, histograms, total bytes, per-task bytes, and path evidence. |
| Adaptive tie-breaking changed FCT materially | A large improvement could be mistaken for a regression or uncontrolled difference | Require Jain balance, max queue, and DCQCN/PFC evidence before accepting S06. |
| Planned-TP cleanup ignored reverse-direction reuse | S10 completion plateaued and emitted `No task completed` | Fix reverse planned-TP accounting; all current A2 records and focused regression tests pass. |
| P2 initially used legacy CSV on both branches | The supposed product profile did not include structured-rule compute | Preserve old records as superseded, fix runner to consume matrix route mode, and rerun all P2 rows. |
| A1/A2 initially used legacy CSV routing | Attribution profile did not match the frozen matrix | Supersede 72 old records and rerun with compressed-table lookup. |
| Watchdog watched formatted simulation time only | Simulation time advanced while task completion was stalled | Detect explicit no-completion plateaus across ns/us/ms/s units and record termination reason. |
| Focused test helper could not find `ub-quick-example` | Building only test-runner made the helper search the wrong parent path | Build the example in the same worktree; runtime-storage and lifecycle suites then pass. |
| Ledger and artifact contract drifted after reruns | Displayed timing and declared standalone files could disagree with current records | Reconcile timing from `record.json`; retain missing standalone artifacts as a non-blocking reproducibility gap. |

## Focused Regression Tests

| Suite | Result | Coverage |
| --- | --- | --- |
| `unified-bus-runtime-storage-snapshot` | Pass | Three cases, including reverse-direction planned-TP destroy accounting |
| `unified-bus-lifecycle` | Pass | Lifecycle invariants and expected death-test behavior |
| S11 correctness companion P1/P2 | Pass | 4 runs, 3968 tasks per run, phase ordering and registered FCT bounds |

Expected `NS_FATAL` output from lifecycle death tests is not a test failure; the suite exited 0.

## Evidence Limits

| Limitation | Consequence |
| --- | --- |
| Results are simulator and host-process measurements | They do not prove physical-switch throughput or hardware SRAM use. |
| Performance runs are deliberately untraced | Their FCT oracle comes from C1-C3 and the focused S11 companion, not the timing records themselves. |
| P2 changes several factors together | Its total delta cannot be assigned to one optimization without A1-A3. |
| Offline generation happened before the matrix | Reported `offline_seconds` is per-run materialization/copy time, not total one-time route compilation cost. |
| Wall time is host-noise-sensitive | Retired instructions/cycles are primary; five-repeat paired medians and intervals support wall results. |
| Some declared standalone evidence files were not emitted | Equivalent data remains in records/traces/aggregate analysis, but the artifact contract is not fully complete. |
| Dirty-state capture is absent from per-run provenance | This is a reproducibility gap; no source, tool, build, or binary revision drift was observed. |

## Final Interpretation

`next` is a technically credible successor to `main`, with no blocking correctness finding in the
frozen matrix. Its strongest general claim is lower CPU work and shorter runtime. Its memory claim
must be qualified: lifecycle cleanup produces substantial savings for dense workloads with object
turnover, while small or non-lifecycle-heavy cases can still use slightly more RSS.

Accordingly, the safe branch policy is to keep `next` staged for integration and additional user
exposure, without tagging or publishing a release yet. A future release decision should refresh the
same frozen matrix against the then-current candidate SHA rather than treating this 2026-08-02 run
as permanently current.
