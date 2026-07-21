# T42 Task Brief: Restore typed tracked LDST scheduling

## Batch Reuse Map

- Pre-refactor commit: `main@ccf41b58`; protocol and performance control: `next@cf8ee683`.
- Semantic batch: replace the eight high-frequency LDST generic callback wrappers with the
  pre-refactor typed member-function call shape while retaining lifecycle event tracking.
- Old active symbols: `UbLdstThread::PushTaskSegment`, `UbLdstThread::UpdateTask`,
  `UbLdstThread::HandleLoadTask`, `UbLdstThread::HandleStoreTask`, `UbPort::TriggerTransmit`, and
  `UbLdstInstance::OnRecvAck`.
- Old call paths: LDST task split -> thread queue; ACK receive -> thread completion; queue progress ->
  load/store issue; packet enqueue -> port transmit; wire response -> ACK resolution.
- Continuous code blocks reused: the eight typed `Simulator::ScheduleNow(&Member, args...)` calls
  from `main@ccf41b58` and the complete current `Track`, `Attach`, `Begin`, `Resolve`, and disposed
  cleanup blocks from `UbLdstInstance`.
- Reuse mode: copy-and-adjust the old typed call shape plus minimal extraction of the current tracked
  resolution tail.
- Required lifecycle adaptations: schedule the typed member function through a typed tracked runner;
  retain token registration, attached `EventId`, begin validation, resolution, and forced-stop
  cancellation.
- Preserved ordering and observable behavior: `Track -> ScheduleNow -> Attach`, then
  `Begin -> member action -> Resolve`; packet/header/tag construction, ACK state, queue ordering,
  callback/trace timing, and task completion order remain unchanged.
- Existing characterization: `unified-bus-ldst-lifecycle`, forced-stop and reentrant-dispose cases in
  `unified-bus-lifecycle`, p01 task/throughput/trace artifacts, and T34's six-block typed prototype.
- Explicit behavior changes: none in modeled protocol behavior; host execution no longer constructs,
  binds, copies, and invokes `Callback<void>` for the eight typed LDST actions.
- Excluded paths: packet identity accounting, `UbTrackedEventSet` storage, packet builders/parsers,
  ACK/queue/routing algorithms, transport cleanup, and non-LDST tracked-event paths.
- Focused validation: targeted UB build, LDST lifecycle suite, lifecycle suite, every shared artifact
  emitted by trace-OFF p01 against `next@cf8ee683`, and a strictly serial counterbalanced
  host-performance block.
- Re-approval triggers: any public use beyond member-function scheduling, an unexplained test or byte
  difference, forced-stop residue change, event-order change, or need to modify lifecycle storage.

## Ticket-Defined Acceptance Suite

1. The targeted Unified Bus library, test runner, and `ub-quick-example` build successfully.
2. `unified-bus-ldst-lifecycle` and `unified-bus-lifecycle` pass serially, including forced-stop,
   cancel-between-phases, and reentrant-dispose behavior.
3. The eight production callers use the typed member-function overload; the generic
   `ScheduleNow(Callback<void>)` overload remains available and characterized.
4. Every artifact emitted by trace-OFF p01, including task, throughput, PacketTrace, and TaskTrace,
   is byte-identical to `next@cf8ee683`; disabled port and per-hop outputs are not claimed as
   evidence. No unexplained exception is accepted.
5. In a corrected serial counterbalanced comparison, the typed production path improves wall time
   over the pre-T42 formal role and substantially reduces the LDST gap to next. RSS must not regress
   beyond the accepted 10% gate against next.
6. Review against `main@ccf41b58` confirms the old member actions and call order are preserved and no
   packet, state-machine, ACK, queue, routing, or lifecycle algorithm is duplicated.
7. `git diff --check` passes and staging excludes existing user modifications and raw evidence.

## Ledger

- 2026-07-21: T34 variant dylib symbols recover the exact four template instantiations, shared
  `ResolveTrackedAction(uint64_t)` seam, and eight typed caller shapes. The earlier prototype reduced
  formal wall/user CPU by `9.22%`/`9.76%` and kept semantic artifacts exact in all six blocks.
- 2026-07-21: current HEAD has no changes to the four affected LDST files relative to the measured
  formal revision `51c60c5`; later lifecycle compact-history work did not change the
  `UbTrackedEventSet` API used by this batch.
- 2026-07-21: the first T42 three-role run is invalid because the detached pre-T42 worktree lacked
  the `ns-3-ub-tools` submodule. Its simulation completed, but trace parsing did not run, two output
  artifacts were absent, and parser time was omitted. The submodule was initialized before v2.
- 2026-07-21: corrected v2 is exact in all six blocks. Typed versus pre-T42 measures `-6.90%` wall,
  `-8.44%` user CPU, and `-0.33%` RSS; typed versus next measures `+4.41%` wall, `+11.80%` user CPU,
  and `+1.19%` RSS. The next-relative wall gap recovers 10.58 percentage points.
- 2026-07-21: after constraining the public template to member-function pointers, the targeted build
  and both focused suites pass again. A final-binary byte-only run is exact for all four emitted p01
  artifacts; it is not used as a performance sample.
