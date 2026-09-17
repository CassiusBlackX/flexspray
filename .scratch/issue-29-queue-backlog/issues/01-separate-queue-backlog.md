# Issue 29: separate buffer occupancy from queue backlog

Status: in progress
Source: https://gitcode.com/open-usim/ns-3-ub/issues/29
Branch: fix/issue-29-queue-backlog (from origin/next)

## Approved behavior

- Buffer Occupancy: processing + VOQ + egress queued bytes, per output port.
- Queue Backlog: VOQ + egress queued bytes, per output port.
- DCQCN and CAQM use Queue Backlog for congestion feedback.
- Ingress admission, PFC, adaptive routing, processing delay, allocator ordering,
  and existing trace field meanings retain their existing behavior.
- Reuse existing counters and the actual egress queue; no parallel accounting.

## Batch Reuse Map

Pre-refactor commit: d7bd987 (origin/next)
Semantic batch: separate processing residency from congestion queue backlog.
Old active symbols: GetTotalOutPortBufferUsed, GetOutPortBacklogBytes, ResetLocalCc,
OnSwitchPostDequeue, PushToInPortProcessing, MoveInPortProcessingToVoq.
Old call paths: forwarded packet -> processing -> VOQ -> allocator -> egress;
DCQCN post-enqueue and CAQM periodic/dequeue callbacks inspect output-port load.
Continuous code blocks reused: existing VOQ totals, processing totals and egress GetCurrentBytes.
Reuse mode: compose two explicit QueueManager queries and migrate CC callers.
Required lifecycle adaptations: none.
Preserved ordering and observable behavior: packet movement, admission, PFC, routing,
legacy queries and trace field values.
Existing characterization: in-port processing accounting, allocator visibility,
DCQCN marking and congestion-control fixtures in ub-test.cc.
Explicit behavior changes: processing bytes no longer contribute to DCQCN/CAQM queue input.
Excluded paths: congestion algorithms/thresholds, transport, routing decisions, trace schemas.
Focused validation: existing public QueueManager occupancy interfaces, real switch SendPacket,
allocator/port transitions and CC packet-header callbacks; run unified-bus-flow-control,
unified-bus-congestion-control, unified-bus-switch-voq and node-delay mapping suites.
Full acceptance: configured full build and unified-bus unit suite after focused checks.
Re-approval triggers: changes outside the preceding behavior and ownership boundaries.

## Evidence

- Initial tracked diff: clean. Original atomic worktree kept separate.
- Toolchain: Python 3.12, AppleClang, CMake/Ninja; release, UB + dependencies,
  tests enabled, MPI/MTP disabled. No Python dependencies needed for C++ tests.
- Native link configuration: `cmake -S . -B cmake-cache -DNS3_FAST_LINKERS=OFF`.
  Default LLD failed on libc++ symbols; original workspace also uses fast linkers OFF.
- DCQCN red: original code marks a 128-byte payload queued alongside a 4096-byte
  processing packet with Kmin=1024/Kmax=2048; regression fails at IsMarked(ready).
- DCQCN green: after query split and caller migration, all 25 congestion-control tests pass.
- CAQM red: 4000 processing bytes incorrectly suppress a 4096-byte increase request;
  test fails at the public post-dequeue congestion-header result.
- CAQM green: real queued bytes still mark, processing-only bytes do not.
- PASS: unified-bus-congestion-control (27 cases), unified-bus-flow-control (34),
  unified-bus-switch-voq (32), unified-bus-create-node-delay-mapping (4).
- Occupancy tests cover admission pressure during processing, processing-to-VOQ,
  VOQ-to-egress, port isolation, and release at transmission start. DCQCN also
  marks against a backlog residing entirely in egress.
- Diff is stable for read-only Standards/Spec review against d7bd987.
- Full configured build and unified-bus unit suite pending.
