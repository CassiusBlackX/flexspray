# T30 Task Brief: Skip Hot Coordinator RTTI Lookup

Status: closed

## Objective

Remove the speculative `dynamic_cast` from the node-scoped lifecycle coordinator lookup on the
packet transfer/terminal hot path. Keep the same ns-3 aggregate, TypeId, owner-node, generation,
execution-owner, and error semantics. Do not introduce a global registry, persistent cache, packet
pointer tag, or new lifecycle owner model.

The current p01 16 MiB Time Profiler capture contains 913 sampled backtraces. The packet transfer
entry is present in 47, `RoutePacketTransfer` in 34, coordinator `Resolve` in 10, `GetNNodes` in 11,
and `GetNode` in 7. RTTI/dynamic-cast frames occur in 55 inclusive samples across the process. The
profile directly records `__dynamic_cast` below coordinator resolution. These counts select the
lookup seam; paired end-to-end measurement decides whether the one-line change is useful.

## Ticket Acceptance Suite

Run build, test, and simulation work serially.

1. `git diff --check`.
2. `ninja -C cmake-cache unified-bus unified-bus-test test-runner`.
3. `./build/utils/ns3.44-test-runner-default --test-name=unified-bus-lifecycle`.
4. `./build/utils/ns3.44-test-runner-default --test-name=unified-bus-switch-voq`.
5. Run the existing p01 16 MiB minimal case as one warm-up plus five counterbalanced frozen
   pre-T30/T30 pairs, strictly serial.
6. Require exact generated task, throughput, and trace artifacts in every pair.
7. Retain the change only if paired-median wall improves by at least 1 percent, at least four of five
   wall pairs do not regress, and paired-median user CPU and RSS do not regress by more than 1
   percent. This ordinary ticket does not create a main frozen-byte milestone.

## Batch Reuse Map

Pre-refactor commit: `main@ccf41b58`.

Semantic batch: node-scoped packet lifecycle coordinator resolution.

Old active symbols: `UbLink::TransmitStart`, `UbEgressQueue::DoEnqueue`, and
`UbEgressQueue::DoDequeue`. At main these functions preserve the same queue/link callbacks and
packet order without lifecycle transfer observation.

Old call paths: enqueue updates queue bytes then emits the enqueue trace; dequeue removes the packet,
updates bytes, then emits the dequeue trace; link transmission schedules receive at the destination.
The lifecycle branch inserted transfer observation at those exact seams without changing the old
queue/link algorithm.

Continuous code blocks reused: all old queue/link bodies, current packet-tag parsing,
`IsExecutionOwner`, `RoutePacketTransfer`, `Resolve`, `ValidateMutable`, transfer counter/sequence,
trace gating, terminal handling, and cross-owner control remain in place.

Reuse mode: direct reuse of the existing `Object::GetObject<T>(TypeId)` overload in ns-3 core. No
helper, cache, registry, or parallel implementation is added.

Required lifecycle adaptations: in `UbLifecycleCoordinator::Resolve`, supply the coordinator TypeId
to the existing aggregate lookup so it skips the template overload's speculative `dynamic_cast` and
performs the same TypeId walk directly.

Preserved ordering and observable behavior: tag read, owner check, node validation, coordinator
lookup, generation validation, attempt validation, transfer counter, sequence, trace callback,
terminal callback, and cross-owner routing order remain unchanged. Abort strings remain unchanged.

Existing characterization: `unified-bus-lifecycle`, including packet transfer, terminal,
cross-owner control, stale generation, and expected fail-fast cases; `unified-bus-switch-voq`; p01
production task/throughput/trace artifacts.

Explicit behavior changes: none. Only the internal ns-3 aggregate lookup overload changes.

Excluded paths: packet builders/parsers, queue/link algorithms, ACK/RTO/retransmission, CTP/RTP,
flow control, congestion control, lifecycle storage/layout, MTP/MPI control semantics, trace schema,
public API, and automatic TP cleanup.

Focused validation: the ticket acceptance suite above.

Re-approval triggers: a test changes expected behavior; a missing coordinator no longer fails the
same way; MTP/MPI ownership logic must change; a registry/cache/public API is needed; generated
artifacts differ; performance is noisy or below the retention gate; or two equivalent failures
occur.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`.

Old active symbols and call path: the queue/link functions listed above and their unchanged
production ordering.

Code blocks copied/extracted: none.

Required lifecycle adaptations: retain the already reviewed transfer seam and replace only the
coordinator aggregate lookup overload.

Explicit behavior changes: none.

Preserved ordering/observable behavior: all behavior listed in the Batch Reuse Map.

Characterization evidence: main source comparison, lifecycle/VOQ suites, current Time Profiler
capture, and frozen pre-T30 p01 artifacts.

## Review Checklist

- No old queue/link branch, field update, callback, trace, or schedule timestamp changes.
- No packet identity, owner routing, generation, terminal, MTP/MPI, or lifecycle counter changes.
- The resolved object is still the node aggregate matching `UbLifecycleCoordinator::GetTypeId()`.
- Unknown nodes and missing coordinators retain their existing aborts.
- No coordinator registry, cache, duplicate lookup, or packet pointer state is added.

## Ledger

- 2026-07-17: T30 opened from `9d53f25`. Source behavior is identical to pre-T29 `db335a0`; the
  frozen pre-T30 executable SHA-256 is
  `7756e587027029b4d551f70d490659bcea4af0f31cc5fbd47ddc47a46bb45a78` and library SHA-256 is
  `0968fced00fb34f13331e73e603c91382601d99a1f588a37517a79aa93a32878`.
- 2026-07-17: root confirmed the one-line semantic batch and the existing characterization seam.
- 2026-07-17: the targeted build, `unified-bus-lifecycle`, and `unified-bus-switch-voq` passed. The
  lifecycle suite's abort output came from its existing expected fail-fast subprocess cases and the
  parent suite reported PASS.
- 2026-07-17: all five p01 semantic comparisons were exact. Paired-median wall was `-0.82%`, user
  CPU was flat, RSS was `+0.33%`, and only three of five wall pairs did not regress.
- 2026-07-17: the one-line TypeId lookup failed the predefined 1 percent/four-pair retention gate and
  was reverted. Coordinator resolution source is again identical to `9d53f25`; T30 retains only
  diagnostic evidence.
- 2026-07-17: root closeout review confirmed there is no remaining lifecycle source or test diff;
  the ticket closes as a rejected isolated optimization.
