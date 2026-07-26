# T41: Post-promotion validation closure

## Outcome

T41 closes both T40 validation residuals. The macOS umbrella `unified-bus` runner passes with exit
zero, and post-T39 local-MTP, MPI, and hybrid runs complete with finite lifecycle balance and exact
frozen m18 treatment artifacts. No production Unified Bus model file changes.

The final branch is a strict descendant of `origin/next@cf8ee683` with no missing or divergent
remote-next commit. It is closed as the next-generation `next` candidate; later work should use a
new scoped branch unless it is an integration or review fix for this promotion.

## Exit 139 root cause

The runner was not crashing while collecting an expected child `SIGABRT`. A stale CTP fixture built
a request packet with the receiver service and hard-coded endpoint IDs. The receiver therefore did
not enqueue the expected TAACK. `NS_TEST_ASSERT_MSG_NE` recorded the null packet but did not return,
and the fixture immediately called `RemoveHeader` through the null pointer, producing `SIGSEGV` and
the shell status 139.

The fixture repair reuses the real production boundaries:

- sender-side packet construction with topology-derived source/destination IDs;
- `UbFunction::SubmitCtpWqe` followed by `UbCtpTransportService::StartJetty` for wire-ACK setup;
- `UbCtpTransportConstraints::maxSegmentBytes` for two-transaction CTP cases;
- inline assertions for normal duplicate rejection instead of an unnecessary child process; and
- an explicit return after a failed null setup assertion so one failure cannot become a runner
  crash.

The audit also corrected equivalent stale IDs in the packet-trace and switch-forwarding fixtures.
No packet builder, parser, ACK/window algorithm, queue, callback, timer, trace, or production state
machine was changed.

The final command completed in 0.225 seconds and reported `PASS unified-bus`; expected isolated
negative tests still print their `NS_FATAL` diagnostics, but the parent runner collects them and
returns zero.

## macOS fork boundary

The network research does identify a separate reliability debt in the remaining death-test helper,
but it does not explain this confirmed null dereference:

- POSIX specifies that after a multi-threaded process calls `fork()`, the child may execute only
  async-signal-safe operations until a successful `exec`. The current helper directly invokes a
  C++ test lambda in the child, which is outside that portable guarantee. See the
  [POSIX fork specification](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fork.html).
- Apple's Objective-C runtime documents and implements additional post-fork protection because a
  child can otherwise wait on initialization owned by a parent thread that no longer exists. See
  Apple's [`objc-initialize.mm`](https://github.com/apple-oss-distributions/objc4/blob/main/runtime/objc-initialize.mm#L614-L672)
  and [`objc-runtime-new.mm`](https://github.com/apple-oss-distributions/objc4/blob/main/runtime/objc-runtime-new.mm#L816-L825).
- A portable future death-test harness should launch a fresh process image through `posix_spawn`, or
  fork and immediately `execve`, selecting one death scenario by argument. The parent should verify
  `waitpid`, then branch on `WIFSIGNALED` before reading `WTERMSIG`. See the
  [POSIX waitpid specification](https://pubs.opengroup.org/onlinepubs/9799919799/functions/waitpid.html)
  and [POSIX posix_spawn specification](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn.html).

That harness redesign is not part of T41: it would require a new executable/selector contract and
would broaden a test-only fixture repair. Current runner evidence is green, and the remaining risk
is recorded as test infrastructure portability debt rather than a production or acceptance defect.

## Post-T39 runtime replay

The two existing treatment build trees were rebuilt serially from the pre-T41 production source:
local-MTP with MTP enabled and MPI disabled, and parallel-all with both MPI and MTP enabled. The
frozen m18 input was copied to mode-specific temporary roots. T40 made the `minimal` trace preset
resolve lifecycle output to `OFF`, so each validation copy explicitly selected lifecycle `SUMMARY`;
no topology, traffic, routing, protocol, or runtime variable changed.

| Runtime | Command shape | Task result | Lifecycle records | Finite residue | Frozen tree |
|---|---|---:|---:|---:|---|
| local-MTP | two MTP threads | 1/1 | 198 | zero | byte-identical |
| MPI | two ranks, MTP off | 1/1 | 264 | zero | byte-identical |
| hybrid | two ranks x two MTP threads | 1/1 | 264 | zero | byte-identical |

All three runs exit zero. The production lifecycle parser reports four finite packet attempts, nine
resource summaries, no active packet/work/child/transport-use/tracked-event/transport-work residue,
and one expected persistent binding lease in each mode. Complete `output/` and `runlog/` directory
comparisons against the accepted m18 lifecycle role report no differing or additional file.

## Review

- Standards: no production model change, no new protocol oracle, no parallel builder/parser/state
  machine, and no new fork use. Fixture source/destination ownership now matches the production call
  direction.
- Specification: both T40 residuals are closed. The runtime replay uses the accepted m18 case and
  parser, and exact bytes remain the gate rather than a new tolerance.
- Residuals unchanged by T41: automatic TP cleanup remains excluded, and the LDST typed-tracked
  optimization remains an unpromoted prototype.
