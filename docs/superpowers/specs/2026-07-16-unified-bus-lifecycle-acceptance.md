# Unified Bus Lifecycle Final Acceptance Report

Date: 2026-07-16

Implementation head: `a782858` (`refactor(unified-bus): contract lifecycle ownership APIs`)

Behavior baseline: `main@ccf41b58`

## Decision

The lifecycle migration passes the ticket-defined semantic, parallel-runtime, artifact, and bounded
performance acceptance matrix. Automatic TP close/destroy remains disabled. Idle TP endpoints and
ordinary empty VOQs remain retained by default; only explicit lifecycle operations use their proven
drain gates. The 64 KiB trace chunk, 1 MiB/512 KiB owner high/low watermarks, and 1024 configured
descriptor maximum remain unchanged.

No acceptance result requires a protocol algorithm, packet format, callback/timer order, ownership
model, or storage default change.

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

An additional non-ticket umbrella run, `--test-name=unified-bus`, is not classified PASS because the
runner exits with status 139 on this macOS host. Removing the
`UbAppCtpTrafficRecordRejectsInvalidPriorityTest` child-abort fixture did not remove the crash, so
that fixture is not the root cause and the actual crash point remains unlocalized. Production
invalid-priority validation is unchanged, and the passing ticket-defined lifecycle/CTP focused
suites remain the accepted macOS validation path.

## Runtime Matrix

The exact current source was built for local single-thread and parallel-all. The exact local-MTP
candidate from T20 is also at implementation head `a782858`. No build, test, or simulation process
overlapped another.

| Runtime | Medium cross-owner result | Lifecycle records | Managed-tree relationship |
|---|---:|---:|---|
| local-single | 1/1 task | 198 | identical to T20 current candidate |
| local-MTP, 2 threads | 1/1 task | previously validated at T20 | exact T20 candidate |
| MPI-single, 2 ranks | 1/1 task | 264 | identical to T17 accepted candidate |
| hybrid, 2 ranks x 2 threads | 1/1 task | 264 | identical to T17 accepted candidate |

MPI causal ordering, hybrid causal ordering, dependency visibility, MPI and hybrid lifecycle
control, remote TP, and MPI/hybrid quick-example suites all pass. Current MPI-single and hybrid
managed trees are byte-identical to the accepted T17 trees, proving that T20's contract cleanup did
not change their runtime artifacts.

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
claims. They prove output independence, bounded bytes/descriptors and parser validity. They do not
justify changing the current minimal/SUMMARY default.

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
- CTP empty transmit queues unregister and retire after their last packet; backpressure retains the
  packet, queue and allocator registration.
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

## Final Policies

- Keep correctness accounting always enabled and trace-independent.
- Keep lifecycle trace default at the existing minimal/SUMMARY behavior; use EVENT or DETAIL only
  when their causal evidence is needed.
- Keep 64 KiB chunks, 1 MiB/512 KiB owner pressure bounds and the 1024 configured FD maximum.
- Keep ordinary empty VOQs and idle TP endpoints retained; use only explicit proven drain gates.
- Keep automatic TP close/destroy and legacy task-driven removal disabled.
- Do not introduce a storage reclamation default until a representative long-workload experiment
  measures object counts, RSS and semantic continuity under an explicit candidate policy.
