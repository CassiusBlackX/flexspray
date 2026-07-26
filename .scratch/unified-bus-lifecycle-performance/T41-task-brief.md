# T41 Task Brief: Close post-promotion validation residuals

## Batch Reuse Map

- Pre-refactor commit: `main@ccf41b58`; protocol control: `next@cf8ee683`.
- Semantic batch: repair stale CTP test fixtures and replay the post-T39 parallel acceptance seam.
- Old active symbols: `UbFunction::SubmitCtpWqe`, `UbCtpTransportService::StartJetty`,
  `UbCtpTransportService::BuildDataPacket`, `BuildLocalTpTopology`, `RunInChildProcess`, and the
  existing m18 lifecycle parser/comparator.
- Old call paths: sender Function submission -> Transaction-owned CTP segmentation -> Jetty drain;
  sender service packet builder -> receiver switch sink -> TAACK queue; frozen m18 input -> local-MTP,
  MPI, or hybrid runtime -> production trace parser.
- Continuous code blocks reused: existing CTP fixture builders and assertions, the current production
  submission path, frozen m18 input trees, and `compare_pairs.py::validate_lifecycle`.
- Reuse mode: direct production-call-path reuse plus test-asset adjustment.
- Required lifecycle adaptations: none in production. Fixtures bind dynamic sender/receiver IDs, use
  the CTP segment ceiling, and submit through the Transaction owner introduced by the accepted
  lifecycle architecture.
- Preserved ordering and observable behavior: packet header parsing, ACK/window state, Jetty
  completion, trace timing, queue admission, task output, and all frozen m18 artifacts.
- Existing characterization: CTP local TAACK, wire TAACK, out-of-order ACK, packet trace, switch
  forwarding, duplicate rejection, umbrella `unified-bus`, and m18 local-MTP/MPI/hybrid evidence.
- Explicit behavior changes: a normal duplicate-rejection test no longer forks; failed null setup
  now returns after recording the assertion instead of dereferencing null and crashing the runner.
- Excluded paths: production model code, protocol builders/parsers/state machines, automatic TP
  cleanup, LDST optimization, and any new lifecycle semantics.
- Focused validation: targeted test build if required, umbrella `unified-bus`, explicit-SUMMARY m18
  local-MTP/MPI/hybrid runs, lifecycle balance, and byte comparison with the frozen lifecycle role.
- Re-approval triggers: production source change, unexplained test failure, nonempty finite residue,
  new artifact difference, or a need to redesign the fork death-test harness.

## Ticket-Defined Acceptance Suite

1. `./build/utils/ns3.44-test-runner --test-name=unified-bus` exits zero. Expected isolated
   death-test `NS_FATAL` stderr is allowed only when the parent runner reports PASS.
2. No file under `src/unified-bus/model` changes.
3. local-MTP (`--mtp-threads=2`), MPI (`-np 2`, MTP off), and hybrid (`-np 2`, two MTP threads)
   each complete the frozen m18 task with explicit lifecycle `SUMMARY` output.
4. The production lifecycle parser reports zero finite active residue in all three modes.
5. Each post-T39 `output/` and `runlog/` tree is byte-identical to its frozen m18 lifecycle tree.
6. Review distinguishes the confirmed fixture null dereference from the independent macOS
   post-`fork()` safety limitation and records the latter without attributing this 139 to it.
7. `git diff --check` passes and staging excludes unrelated performance scripts and raw evidence.

## Ledger

- 2026-07-21: umbrella exit 139 localized to `UbCtpLocalDataPacketGeneratesTaAckTest`: stale sender
  identity prevented TAACK creation, the nonfatal assertion recorded null, and the fixture then
  dereferenced the null packet.
- 2026-07-21: fixture audit found the same topology/active-path drift in packet trace, wire ACK, and
  switch forwarding tests. All corrections reuse production builders and the current CTP submission
  path; no protocol oracle or implementation was added.
- 2026-07-21: umbrella `unified-bus` passes with exit zero. Expected child-abort diagnostics remain
  visible on stderr but are collected successfully by the parent runner.
- 2026-07-21: post-T39 local-MTP, MPI, and hybrid runs completed serially. Because T40 changed the
  `minimal` preset to lifecycle `OFF`, the temporary validation copies explicitly selected
  `SUMMARY`; all lifecycle invariants and complete output/runlog byte comparisons pass.
