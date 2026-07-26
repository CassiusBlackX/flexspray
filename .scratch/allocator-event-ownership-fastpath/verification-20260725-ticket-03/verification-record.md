# Ticket 03 verification record

## Scope and target snapshot

- Verification cwd: `/Users/ytxing/workspace/ns-3-ub-architecture-integration`
- Target: Ticket 02 working-tree diff limited to
  `src/unified-bus/model/ub-switch-allocator.cc`,
  `src/unified-bus/model/ub-switch-allocator.h`, and
  `src/unified-bus/test/ub-test.cc` on top of
  `2bf02c8c082841381d599a10d6a1e461d4c24ea6`.
- Snapshot command: `git rev-parse HEAD`; `git status --porcelain=v1`;
  `git diff --binary HEAD | shasum -a 256`; and
  `git status --porcelain=v1 -z | shasum -a 256`.
- Snapshot result: `HEAD=2bf02c8c082841381d599a10d6a1e461d4c24ea6`;
  tracked-diff SHA-256 `5f7451faa50d5d986457bcc978882b463163d2f79fc2041cd14ff5450879d6cd`;
  full-status SHA-256 `19f2de91e6c2beacf0173347ebdd84706894807a66d77f8e735b4902b4eee67e`.
- The worktree contained pre-existing unrelated documentation and diagnostic artifacts. They are
  not part of the target and will not be modified.

## Explicit acceptance criteria

1. Focused lifecycle, allocator/VOQ, TP reservation, CTP lifecycle, retransmission, and aggregate
   Unified Bus suites pass.
2. RR and DWRR cover direct and scheduled completion, single and multiple same-port retriggers,
   multiple output-port pending work, callback-entry pending/running queries, TP backing reclaim,
   VOQ destruction, and local/MTP owner cleanup.
3. Forced stop cancels allocation, retrigger, notification, and deadlock work without surviving
   allocator callback access; residue and active-work counts exactly match the characterized oracle.
4. Current candidate canonical event identity, timestamp, and task output equal the Ticket 01
   complete-ownership control.
5. The implementation target remains unchanged between this snapshot and the final verdict.

## Baseline evidence inspection

Ticket 01 baseline manifest and the approved attribution package establish the complete-ownership
control event count as 12,288, an equal identity multiset, and zero changed timestamps. They name
only `control-complete-event-ownership`, `without-allocator-event-ownership`, and
`without-inport-event-ownership`. The manifest explicitly records the output-port-local production
candidate as unimplemented and unmeasured; it therefore contains no candidate task output or
candidate canonical comparison for criterion 4.

No candidate canary was generated or run in this verification. Ticket 03 has no approved
experiment-group contract adding the current working-tree candidate, and the task forbids creating
one ad hoc.

## Command log

All build and test invocations were serial.

| Command | Exit | Observation | Artifact |
| --- | --- | --- | --- |
| `ninja -C cmake-cache unified-bus-test` | 0 | Target already up to date; no reconfiguration. | `build-unified-bus-test.log` |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle --xml --out=...` | 0 | Root result `PASS`. | `unified-bus-lifecycle.xml` |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq --xml --out=...` | 0 | Root result `PASS`; 16 contained test cases passed. | `unified-bus-switch-voq.xml` |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-tp-reservation --xml --out=...` | 0 | Root result `PASS`. | `unified-bus-tp-reservation.xml` |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-ctp-lifecycle --xml --out=...` | 0 | Root result `PASS`. | `unified-bus-ctp-lifecycle.xml` |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-transport-retrans --xml --out=...` | 0 | Root result `PASS`. | `unified-bus-transport-retrans.xml` |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-parallel-lifecycle --xml --out=...` | 0 | Root result `PASS`; all three MTP lifecycle cases passed. | `unified-bus-parallel-lifecycle.xml` |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus --xml --out=...` | 0 | Aggregate root result `PASS`. | `unified-bus.xml` |
| `git diff --check` | 0 | No whitespace errors in the candidate diff. | This record |
| `pgrep -fl 'ns3\\.44-test-runner|ns3\\.44-ub-quick-example'` | 1 | No matching test-runner or simulator process remained. | This record |

All paths in the Artifact column are relative to this directory:

`/Users/ytxing/workspace/ns-3-ub-architecture-integration/.scratch/allocator-event-ownership-fastpath/verification-20260725-ticket-03/`

Each suite also has sibling `.stdout.log` and `.stderr.log` artifacts. The XML result is the
authoritative structured test output.

## Matrix coverage and verdicts

| Acceptance item | Verdict | Observed facts | Interpretation |
| --- | --- | --- | --- |
| Required focused suites and aggregate Unified Bus suite | PASS | All seven listed test-runner commands exited 0 and each XML root is `PASS`. | Required regression suites pass for this target. |
| RR and DWRR same-output multiple retriggers | PASS | `unified-bus-switch-voq.xml` reports `PASS` for both `RR allocator retains multiple same-port retrigger events` and `DWRR allocator retains multiple same-port retrigger events`; the source asserts residues 3 and 2 respectively. | Multiple concrete retrigger handles are retained and canceled in both schedulers. |
| Direct and scheduled completion | PASS | The same XML reports `PASS` for `direct allocation completion starts an idle egress port` and `scheduled allocation completion remains available`. | Both completion modes retained their existing behavior. |
| Callback-entry reclaim query | PASS | The same XML reports `PASS` for `allocator callback-entry reclaim query tracks pending peer`. | First callback sees the pending peer; the last callback sees no stale scheduling-work gate. |
| VOQ destroy gate | PASS | The same XML reports `PASS` for `VOQ destruction waits for allocator and accounting gates`. | VOQ destruction stays blocked for pending allocator work and succeeds after resolution in that case. |
| Forced stop: all pending allocator event kinds and exact event residue | PASS | The same XML reports `PASS` for `switch disposal cancels queue events and records residue`; its assertions require pending/residue count 5 for deadlock, notification, allocation, and two retriggers, then pending count 0. | Pending allocator cancellation and event-only residue characterization pass. |
| No residual callback process after forced stop | PASS (bounded) | The RR/DWRR retrigger test calls `Simulator::Run()` after `Dispose()` and passes; post-run `pgrep` found no test or simulator process. | This is evidence that canceled retrigger/deadlock callbacks do not run after disposal. It is not an all-event-kind running-callback test. |
| Local and MTP owner-cleanup controls | PASS | `unified-bus-lifecycle.xml` and `unified-bus-parallel-lifecycle.xml` both have root `PASS`; the MTP XML contains `typed connection control reuses paired destroy cleanup` and `task cleanup selects causal delay only for partitioned MTP`. | Existing local and partitioned-MTP lifecycle controls regress cleanly. |
| Single retrigger case | FAIL (coverage) | Valid-initialized `ScheduleTriggerAllocator()` test calls occur as two-retrigger pairs; the uninitialized/disposed test does not schedule a retained event. | No independent successful single-retrigger ownership assertion was found or executed. |
| Multiple output ports simultaneously pending | FAIL (coverage) | The four-output-port test only calls `SelectNextIngressQueue()`; no test schedules retained work for two output ports and queries each pending gate independently. | Per-port isolation under concurrent pending state is not directly verified. |
| TP backing reclaim remains blocked while allocator work exists | FAIL (coverage) | `unified-bus-tp-reservation.xml` passes dormant reclaim, but its focused source assertion observes no allocator work before successful reclaim. | There is no observed negative TP-backing-reclaim assertion while allocator work is pending. |
| Forced stop while allocator callback is running; active-work residue exactness | FAIL (coverage) | The exact-residue test exercises only pending handles. No allocator test disposes during an active ingress callback and asserts active-work residue. | Pending-versus-running forced-stop semantics are not fully verified. |
| Candidate canonical identity, timestamp, and task output versus Ticket 01 control | BLOCKED | The approved comparison JSON lists only `control-complete-event-ownership`, `without-allocator-event-ownership`, and `without-inport-event-ownership`; Ticket 01 manifest marks the output-port-local candidate unmeasured. | No approved candidate experiment-group contract or candidate output exists. This verification did not create or run one. |
| Target stability | PASS | End snapshot matches the start: `HEAD=2bf02c8c082841381d599a10d6a1e461d4c24ea6`; tracked-diff SHA-256 `5f7451faa50d5d986457bcc978882b463163d2f79fc2041cd14ff5450879d6cd`; full-status SHA-256 `19f2de91e6c2beacf0173347ebdd84706894807a66d77f8e735b4902b4eee67e`. | The tested implementation did not change during verification. |

## Overall verdict

Ticket 03 is **BLOCKED / not accepted**. The fixed candidate is stable and all required suites
pass, but the required current-candidate canonical comparison is unavailable without an approved
candidate canary contract. Four matrix cells also lack direct regression evidence: single retrigger,
multiple output ports pending, allocator-pending TP backing reclaim, and forced stop during active
allocator work. These are coverage failures, not observed product failures; use a debugger or
implementer to add bounded tests after the missing experiment contract is supplied.

## Final snapshot commands

```text
git rev-parse HEAD
git diff --binary HEAD | shasum -a 256
git status --porcelain=v1 -z | shasum -a 256
git diff --no-ext-diff --name-only HEAD -- src/unified-bus/model/ub-switch-allocator.cc src/unified-bus/model/ub-switch-allocator.h src/unified-bus/test/ub-test.cc
```

The final target-file list was exactly the three files named in the initial target snapshot.
