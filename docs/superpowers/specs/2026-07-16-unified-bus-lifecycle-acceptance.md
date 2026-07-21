# Unified Bus Lifecycle Final Acceptance Report

Date: 2026-07-21

Production source head: `61abfaf` (T39 closeout; production implementation `cbc0567`)

Final frozen evidence: `dc42ac9` (T40 promotion artifacts)

Post-promotion validation: `f08aaec` (T41 fixture implementation; no production model change)

Behavior baseline: `main@ccf41b58`

Protocol calibration baseline: `next@cf8ee683`

## Decision

The lifecycle migration passes the ticket-defined architecture, semantic, artifact, and common-path
performance gates. The final T40 replay adds no new difference path against exact
`next@cf8ee683`: all three CTP window cases are byte-identical, and each RTP role exactly reproduces
its previously reviewed archived tree.

This is not a claim that every workload is performance-neutral. The packet-heavy LDST/p01 path has
an explicit residual described below; a validated typed-tracked prototype was not promoted because
LDST is uncommon and pending redesign. T41 subsequently repaired stale CTP fixtures and the macOS
umbrella test runner now passes. The focused semantic suites and frozen artifact results remain
unchanged.

Automatic TP close/destroy remains disabled and is not credited as a memory or runtime improvement.
Idle TP endpoints and ordinary empty VOQs remain retained by default; only explicit lifecycle
operations use their proven drain gates. The 64 KiB trace chunk, 1 MiB/512 KiB owner high/low
watermarks, and 1024 configured descriptor maximum remain unchanged.

No final acceptance result requires another protocol algorithm, packet format, callback/timer order,
ownership model, or storage-default change.

## Final Next Differential

T40 froze the exact current and next source revisions, executable hashes, Unified Bus library hashes,
six input trees, and six accepted comparison documents before running. All 12 simulations completed
serially. Analysis was then rerun from saved results without rerunning simulation.

| Case | Final result | Accepted boundary |
|---|---|---|
| CTP Jetty window 1 | byte-identical | none |
| CTP Jetty window 4 | byte-identical | none |
| CTP Jetty window 128 | byte-identical | none |
| RTP shared TP, window 1 | controlled | two existing same-timestamp TaskTrace reorders |
| RTP GBN double loss | controlled | exact replay of T06 cumulative-ACK descriptor boundary |
| RTP TPG with loss | controlled | exact replay of T14 generation attribution/pinning boundary |

The CTP completion vectors match next exactly and differ across windows 1, 4, and 128, proving that
the Jetty inflight/backpressure seam is exercised rather than bypassed. For every RTP case, both the
current and next shared artifact trees reproduce their archived hashes exactly and task ID sequences
match. No new path, hash, task, protocol output, or exception was accepted.

## Semantic Matrix

| Area | Production seam | Evidence | Result |
|---|---|---|---|
| RTP packet lifecycle | coordinator, queues, links, endpoint and typed drop terminals | `unified-bus-lifecycle`, minimal and shared-TP cases | PASS |
| WQE and Transaction | Function admission/outcomes, Transaction segment and response progress | `unified-bus-transaction-urma`, lifecycle suite | PASS |
| Direct TP and TPG | canonical TP manager, shared admission/backpressure and generation pinning | TP reservation, transport pipeline and shared-TP case | PASS |
| GBN and selective retransmission | shared descriptor builder, ACK/SACK/RTO retirement | transport retransmission, modulo, OOO and deterministic-loss case | PASS |
| CAQM and DCQCN | existing congestion state and tracked events | congestion-control, DCQCN marking and retransmission-CC suites | PASS |
| CTP | Transaction window plus CTP Entity/key/queue/event owners | CTP transaction/lifecycle and pressure case | PASS |
| LDST | topology worker plus finite operation/ACK/event children | LDST lifecycle suite | PASS |
| Routing | canonical table plus unchanged policy hot path | routing suite and medium cases | PASS |
| NONE/PFC/CBFC | existing flow-control parsing, thresholds and control frames | flow-control and switch-VOQ suites | PASS |
| Teardown and late reuse | endpoint/connection freeze, drain, callback and rematerialization | lifecycle and TP reservation suites | PASS |
| Forced stop and races | typed residue, event cancel and stale/duplicate rejection | lifecycle, CTP, LDST, VOQ and parallel suites | PASS |
| Trace schema/parser | versioned metadata, rank registry and enum validation | lifecycle-trace suite and parser tests | PASS |

The focused suites were built and run serially. Expected `NS_FATAL` output came only from isolated
negative fixtures. The lifecycle baseline harness passed 38 tests, including rejection of stale,
forged, incomplete, broad, and unclassified exception evidence.

T41 localized the umbrella status 139 to a stale CTP fixture, not the expected child-abort tests.
The fixture built a sender request through the receiver service with stale endpoint IDs, so no TAACK
was queued. Its nonfatal null assertion then continued into `RemoveHeader` and caused `SIGSEGV`.
After migrating the affected fixtures to the current sender and Transaction-owned active paths, the
full `--test-name=unified-bus` runner reports PASS and exits zero. Expected death-test `NS_FATAL`
stderr remains visible and is collected normally by the parent runner. The remaining lambda-after-
`fork()` helper has a separate POSIX/macOS portability risk and should eventually become a fresh
spawn/re-exec test process; it is not the root cause of this resolved 139.

## Runtime Matrix

The original parallel matrix was frozen at T20 source `a782858`. T41 additionally rebuilt the
post-T39 production source in the existing local-MTP and parallel-all configurations and replayed
the frozen m18 cross-owner input under local-MTP, MPI-single, and hybrid. All build, test, and
simulation processes ran serially.

| Runtime | Medium cross-owner result | Lifecycle records | Managed-tree relationship |
|---|---:|---:|---|
| local-single | 1/1 task | 198 | identical to T20 current candidate |
| local-MTP, 2 threads | 1/1 task | 198 | byte-identical to accepted m18 lifecycle role |
| MPI-single, 2 ranks | 1/1 task | 264 | byte-identical to accepted m18 lifecycle role |
| hybrid, 2 ranks x 2 threads | 1/1 task | 264 | byte-identical to accepted m18 lifecycle role |

MPI causal ordering, hybrid causal ordering, dependency visibility, MPI and hybrid lifecycle
control, remote TP, and MPI/hybrid quick-example suites all pass. Current MPI-single and hybrid
managed trees are byte-identical to the accepted T17 trees, proving that T20's contract cleanup did
not change their runtime artifacts.

These three rows now include binaries built from the post-T39 production source. T40 changed the
`minimal` trace preset to lifecycle `OFF`, so T41 selected `SUMMARY` explicitly in temporary input
copies to restore the accepted lifecycle evidence seam without changing modeled behavior.

Owner-local lifecycle sequences parse monotonically, and each rank's schema registry is complete and
consistent. A separate check over the production parser's decoded SUMMARY records finds zero
in-flight packets and zero active work, child, transport-use, tracked-event and transport-work
leases. The only nonzero active counter is the persistent binding lease: one in minimal/cross-owner
cases and four in the shared-TP, retransmission and CTP cases. Those bindings match the accepted
idle-endpoint retention policy and are not finite work or packet residue.

## Frozen Artifacts

The immutable root verifies 369 files. All T00 reference outputs are raw byte-stable; no artifact
uses canonical-only comparison.

| Case/runtime | Managed | Byte-identical | Approved differences |
|---|---:|---:|---:|
| minimal single RTP WRITE / local-single | 19 | 18 | 1 |
| shared TP concurrency / local-single | 25 | 23 | 2 |
| multi-TP retransmission / local-single | 34 | 18 | 16 |
| CTP Entity/VL pressure / local-single | 44 | 5 | 39 |
| cross-owner / local-single | 18 | 17 | 1 |
| cross-owner / local-MTP | 18 | 17 | 1 |
| cross-owner / MPI-single | 19 | 18 | 1 |
| cross-owner / hybrid | 19 | 18 | 1 |

The minimal exception is bound by
`tests/unified_bus_lifecycle/exceptions/t21-minimal-same-timestamp-order.json`. At 114.480 ns,
`WQE Segment Sends` precedes `WQE Segment Completes`; both event pairs and timestamps remain present.
Only `runlog/TaskTrace_node_1.tr` differs, at byte offset 25. The other 18 artifacts are hash-equal.

The inherited shared-TP and cross-owner exceptions have the same T05 same-timestamp cause. The
retransmission exception remains the T06 descriptor-backed packet-rebuild change. The CTP exception
remains the user-approved T14 strict packet-spray round robin. Each inherited manifest is restricted
to its recorded runtime, roots, artifact set, hashes, offsets and structured cause evidence. Current
T21 medium trees are byte-identical to the T20/T17 candidate trees to which those manifests bind.

## Trace Cost

One parallel-all binary ran the same minimal case with only
`UB_LIFECYCLE_TRACE_LEVEL` changed. Network output and every non-lifecycle trace are byte-identical
across `OFF`, `SUMMARY`, `EVENT`, and `DETAIL`.

| Level | Run wall | Total wall | Peak RSS | Lifecycle records | Lifecycle bytes | Writes/flushes | Peak buffered |
|---|---:|---:|---:|---:|---:|---:|---:|
| OFF | 3.584 ms | 54.924 ms | 28,000,256 B | 0 | 0 | 10/10 | 3,523 B |
| SUMMARY | 7.420 ms | 58.449 ms | 28,196,864 B | 198 | 18,055 B | 13/13 | 21,578 B |
| EVENT | 5.513 ms | 56.533 ms | 28,082,176 B | 213 | 18,579 B | 13/13 | 22,102 B |
| DETAIL | 5.487 ms | 55.847 ms | 28,147,712 B | 246 | 19,770 B | 13/13 | 23,293 B |

These are single bounded runs for regression-scale cost, not statistically powered performance
claims. They prove output independence, bounded bytes/descriptors and parser validity. The current
`minimal` preset remains lifecycle `OFF`; `SUMMARY` is selected explicitly when balance evidence is
required.

Correctness accounting is mandatory and has no valid runtime-off mode; disabling it would invalidate
the lifecycle semantics being accepted. Its absolute behavior is covered by coordinator-focused
tests and the record/balance counts above, but the report does not mislabel the whole pre/post
refactor runtime difference as isolated accounting overhead. Likewise, no separate production
`deep verification` runtime control exists. Deep stale/underflow/duplicate/drain checks are measured
through the focused invariant suites, not presented as an independently switchable runtime plane.

## Writer And Storage

The T19 CTP benchmark reused one binary and one managed tree for all chunk sizes:

| Chunk | Writes/flushes | Pressure flushes | Total wall | Peak RSS |
|---|---:|---:|---:|---:|
| 16 KiB | 885/885 | 0 | 393.081 ms | 18,268,160 B |
| 64 KiB | 252/252 | 0 | 358.437 ms | 18,153,472 B |
| 256 KiB | 205/205 | 23 | 435.237 ms | 18,366,464 B |

The 64 KiB default remains the best measured total wall time without pressure flushing. The writer
keeps its 1 MiB/512 KiB owner budget, effective descriptor cap, append reopen, fatal I/O handling,
and unchanged filenames/parser inputs.

Storage and allocation evidence is intentionally module-specific:

- 100,000 route hot-path lookups allocate zero temporary candidate/best-port containers.
- Small packet queues use inline storage for bounded traffic and release overflow capacity after
  drain; descriptor-backed retransmission retains protocol descriptors rather than wire packets.
- CTP empty transmit queue shells retain their pre-refactor allocator identity after the last packet;
  backpressure retains the packet and service disposal unregisters every retained queue.
- Ordinary empty VOQs remain retained. Explicit/test destruction uses the same owner-private gate
  and rejects packets, delayed work, allocator work or nonzero accounting before and after unregister.
- Idle TP endpoints retain reservation, TPN, generation, PSN and congestion continuity. Explicit
  endpoint/connection teardown is available, but no workload, timeout, RSS or allocator signal calls it.

## Runtime Cost Scope

The bounded cross-owner runs are semantic/runtime smoke measurements, not scaling benchmarks:

| Runtime | Per-process total wall | Peak RSS |
|---|---:|---:|
| local-single | 54.300 ms | covered by local T19 measurements, not re-sampled after variant switch |
| local-MTP, 2 threads | 54.960 ms | 18,006,016 B |
| MPI-single, 2 ranks | 58.076-58.119 ms | 21,970,944-22,003,712 B per rank |
| hybrid, 2 ranks x 2 threads | 58.522-58.584 ms | 21,905,408-21,987,328 B per rank |

The exact parallel-all binary is not a substitute for local-single or local-MTP. A direct probe
reproduced the frozen-contract `UbRemoteLink::TransmitStart` failure when MPI was not initialized;
that failed probe is excluded from performance conclusions.

## Promotion Performance

Performance results remain module-specific; they are not averaged into one branch-wide number.

| Workload / control | Paired wall | Peak RSS | Interpretation |
|---|---:|---:|---|
| p01 packet-heavy vs main, T28 frozen | +16.64% | +14.86% | FAIL against 10% gate |
| p01 packet-heavy vs next, T28 frozen | +7.25% | +8.42% | PASS in that frozen block |
| p02 resource-heavy vs main, T28 frozen | +5.46% | +7.83% | PASS |
| p02 resource-heavy vs next, T28 frozen | +2.04% | +5.85% | PASS |
| ordinary RTP vs next, T35 | +1.65% | not used as a gate | common-path control |
| RTP GBN/RTO vs next, T35 | +2.51% | not used as a gate | common-path control |
| PFC/DCQCN vs next, T35 | +0.60% | not used as a gate | common-path control |
| CTP 2 GiB vs next, T36 final | +4.20% | -0.74% | PASS |
| CTP 8 GiB vs next, T36 final | -0.01% | -0.42% | PASS |

The CTP fix is a measured improvement, not a small-case artifact. Before T36, the 2 GiB case was
`+15.04%` wall with 57.7 MB current RSS versus 15.4 MB next. The final 2 GiB result recovers 10.84
percentage points and returns RSS to next scale. The independent 8 GiB block remains effectively
wall-neutral and has no payload-scaled retired-history growth.

LDST remains separate. In the corrected T34 three-role block, formal lifecycle HEAD was `+18.12%`
wall and `+19.28%` user CPU versus next. The typed-tracked prototype reduced those gaps to `+4.75%`
and `+4.98%`, with exact artifacts and forced-stop coverage, but that prototype is not in production.
This residual is accepted as explicit follow-up debt because LDST is not a common path and is expected
to be redesigned; it must not be described as solved or generalized to CTP/RTP/DCQCN.

## Final Policies

- Keep correctness accounting always enabled and trace-independent.
- Keep the `minimal` preset at lifecycle `OFF`; select SUMMARY, EVENT, or DETAIL explicitly when
  their balance or causal evidence is needed.
- Keep 64 KiB chunks, 1 MiB/512 KiB owner pressure bounds and the 1024 configured FD maximum.
- Keep ordinary empty VOQs and idle TP endpoints retained; use only explicit proven drain gates.
- Keep automatic TP close/destroy and legacy task-driven removal disabled.
- Keep the LDST typed-tracked optimization as separate follow-up work; do not imply it is present in
  this branch.
- Keep the umbrella suite in the macOS validation boundary. Track the remaining lambda-after-`fork()`
  death-test portability debt separately and prefer a spawn/re-exec selector if that harness is
  redesigned.
- Do not introduce a storage reclamation default until a representative long-workload experiment
  measures object counts, RSS and semantic continuity under an explicit candidate policy.
