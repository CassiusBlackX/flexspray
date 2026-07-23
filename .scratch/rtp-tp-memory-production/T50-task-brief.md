# T50 Task Brief: Attribute And Validate Production TP Backing Compaction

Status: complete

## Authority And Scope

- Feature contract: `.scratch/rtp-tp-memory-production/PRD.md`.
- Starting commit: `7dbd099`.
- Implementation commit: `36334c0` (`feat(unified-bus): attribute TP compaction storage`).
- T50 exposes storage attribution, removes the abandoned prototype surface, verifies canonical
  behavior against T43, and runs the approved guarded candidate checkpoints.
- T50 does not close RTP connections from task completion, reuse TPNs, add a public feature flag,
  run `p064`, or relax the frozen 10 GiB / 900-second guards.

## Production Outcome

- Every active RTP consumer routes through owner-local live-or-dormant resolution before use.
- Owner-local reclaim triggers preserve the reservation, endpoint identity, generation, and
  congestion-control continuity while releasing heavy backing state after all gates drain.
- Task-scoped Jetties retire after completion publication; reusable Jetties retain their existing
  lifetime.
- The congestion-control base class documents the optional dormancy contract. Algorithms without
  snapshot, restore, idle-reset, and event semantics fail closed and retain live backing.
- Prototype CLI, traffic-generator switches, cleanup callbacks, and prototype evidence were
  removed. `RemoveUselessTps()` remains unsupported and fail-fast.

## Attribution Contract

- Runtime CSV schema remains version `1` with five columns and unchanged meanings for existing
  rows.
- New rows are `reserved_endpoint_slots`, `live_tp_backing_objects`, `dormant_tp_records`,
  `tp_backing_rehydrations`, `peak_live_tp_backing_objects`, `task_scoped_jetties`,
  `reusable_jetties`, and `simulator_event_count`.
- Live/dormant counts scan manager-owned records. Rehydration increments only after complete
  restore and publication. Peak live backing is process-wide and mutex-protected.
- `UbUtils::Destroy()` resets the peak immediately when no backing remains and otherwise defers the
  reset until the final backing release.

## Focused Validation

- Serial build passed for `unified-bus-test` and the Unified Bus example target.
- `unified-bus-tp-reservation`, `unified-bus-lifecycle`,
  `unified-bus-congestion-control`, `unified-bus-runtime-storage-snapshot`, and `unified-bus`
  passed serially after formatting.
- `unified-bus-parallel-lifecycle`, `unified-bus-ctp-transaction`, and
  `unified-bus-ctp-lifecycle` passed serially during final shared-path review.
- Independent implementation review accepted one conservative metric limitation: destroy erases
  its owner record before decrementing the process-wide tracker, so a concurrent materialization in
  that narrow interval may overstate the peak by one. It cannot understate memory risk.
- T43 baseline verification passed for source `aa2b26a` before candidate execution.

## Guarded Candidate Evidence

- Candidate source: `36334c02e784de1d203e0251081485b032e38aba`.
- Binary SHA256: `55df15a727a50736019a8f69b70eafacc4cae6191db05540c0a31f5f9a4ae81c`.
- Build: release Ninja, Unified Bus/MTP enabled, examples/tests/MPI/werror disabled.
- All runs used fresh case/result directories, 10 GiB sampled process-tree RSS and 900-second wall
  guards, and serial execution.

| Run | Result | Peak RSS | Wall / elapsed | Completion and storage |
|---|---|---:|---:|---|
| `p001` local | success | 194,904,064 B | 5.62 / 5.90 s | 1,024 tasks; 209 live / 1,839 dormant |
| `p001` MTP16 | success | 203,735,040 B | 22.74 / 22.97 s | 1,024 tasks; 0 live / 2,048 dormant |
| `p004` MTP16 | success | 283,820,032 B | 116.54 / 116.68 s | 4,096 tasks; 0 live / 8,192 dormant |
| `p016` MTP16 | wall-limited | 599,752,704 B sampled | 900.60 s elapsed | no completion snapshot |

- Both `p001` task-trace trees match the frozen T43 SHA256
  `0a94a0f5a15af54fad159cd839e904170f4a7672a548214ab616ae77827df270`.
- `p004` matches the previous owner-attribution task-trace SHA256
  `5b88205313efcc7ef5b24478ed8281ae379fe27696c4f8a476073fb59de55ef0`.
- The T43 legacy snapshot projection changes only for intended task-scoped Jetty retirement.
- From `p001` to `p004`, the `time -l` RSS increment is 80,084,992 B for 3,072 additional pairs,
  or 26,069 B/pair. The earlier attribution build measured 93,525 B/pair, so the completed-run
  slope decreases by 72.12%.
- `p004` process peak decreases 43.10%, from 498,827,264 B to 283,820,032 B.
- `p064` was not run and has no candidate runtime/result directory.

## Limitations And Decision

- `p016` is inconclusive for completed-run memory scaling. It progressed through 700 us and used
  sustained CPU until the wall guard fired, but emitted no completion trace or storage snapshot.
- Runtime regresses versus the earlier attribution build: `p001` MTP16 wall time increases 42.30%
  and `p004` increases 31.13%. T50 therefore proves memory compaction, not simulation acceleration.
- The local/MTP `p001` final live count differs because same-time opportunistic reclaim events fall
  on opposite sides of the completion-poll stop boundary. Canonical task output and all other
  non-trace, non-event storage rows match; final live count is not a semantic equality oracle.
- The next bounded performance task should reduce redundant hot-path reclaim attempts and global
  connection-control lock contention, then repeat `p004` under the same manifest before reopening
  `p016`. It must preserve owner-local gates and cannot force reclaim at the end-of-run cutoff.
- Raw case copies and task traces remain local artifacts. Curated manifests, commands, RSS samples,
  snapshots, console evidence, and task-trace checksum lists are committed under `t50-evidence/`.
  `t50-comparison-manifest.json` and `verify-t50-candidate.sh` make the admitted subset fail on
  source, input, guard, artifact, task-trace, snapshot, or `p064` drift.
