# T24 Task Brief: Reduce Steady-State Packet Lifecycle Bookkeeping

Status: complete with measured performance blocker

Branch: `codex/unified-bus-lifecycle-implementation`

Starting revision: `006e20d`

## Ticket Acceptance

1. Reproduce p01 with LDST packet spray disabled and lifecycle output OFF using one warm-up and five
   measured counterbalanced current/main pairs, strictly serial.
2. Before production edits, use a bounded time profile or site counters to select a measured
   packet-attempt, lease-container, owner-resolution, or ACTIVE-endpoint notification hotspot.
3. Preserve exact packet membership and generation-aware validation for duplicate birth/terminal,
   underflow, out-of-order terminal, stale generation, and drain checks.
4. ACTIVE TP steady state performs no destroy-only callback work. Installing notification on the
   DRAINING transition plus an immediate drain check must not miss a completion edge.
5. Reuse existing packet tags, queue/link transfer seams, typed endpoint/drop terminals, and
   lifecycle characterization as the oracle. Do not change builders, parsers, ACK/RTO, queues,
   schedulers, routing, congestion control, or packet bytes.
6. p01 task, throughput, and protocol artifacts retain the accepted byte contract. Final
   current/main paired-median wall regression is no more than 10 percent; an unexplained failure
   stops the ticket without speculative redesign.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`

Old active symbols:

- `UbController::CreateTp`
- `UbController::DestroyTp`
- `UbTransportChannel::SetLifecycleStateChangedCallback`
- `UbTransportChannel::IsSafeForExplicitDestroy`

Old call path:

`UbController::CreateTp` creates and registers an ACTIVE transport endpoint without installing a
coordinator callback. Destruction is entered explicitly through `UbController::DestroyTp`; protocol
state, transaction state, allocator registration, packet processing, ACK/RTO, and queue scheduling
remain owned by their existing modules.

Current active symbols:

- `UbTransportChannelManager::CreateTp`
- `UbTransportChannelManager::DestroyTp`
- `UbTransportChannelManager::NotifyStateChanged`
- `UbTransportChannelManager::NotifyLifecycleStateChanged`
- `UbTransportChannelManager::CanFinalize`
- `UbLifecycleCoordinator::SetResourceStateChangedCallback`
- `UbLifecycleCoordinator::ReleaseLease`
- `UbLifecycleCoordinator::TerminatePacket`

Code blocks copied/extracted: none. Reuse the current destroy transition, `CanFinalize` predicate,
notification callback, and coordinator callback slot directly.

Required lifecycle adaptations:

- If measurement confirms the notification hotspot, leave ACTIVE endpoints with a null coordinator
  callback.
- On the first ACTIVE-to-DRAINING transition, install the existing callback before the existing
  immediate `CanFinalize` check.
- Preserve the current callback-before-trace ordering in lease release and packet terminal paths.

Explicit behavior changes: none. Only destroy-only host work moves from ACTIVE steady state to the
existing DRAINING lifecycle seam.

Preserved ordering and observable behavior:

- TP creation, `TpInit`, endpoint insertion, allocator registration, materialization notification,
  trace connection, freeze-new-work, drain predicate, finalization, callback delivery, packet tag,
  transfer, terminal, ACK/RTO, queue, routing, and congestion ordering.
- The DRAINING callback is installed before the immediate drain check, so a completion cannot occur
  between the check and notification installation.

Characterization evidence:

- `UbLifecyclePacketAccountingTest` fixes exact packet membership, terminal balance, sequence, and
  generation reuse.
- Existing explicit TP destroy tests fix ACTIVE/DRAINING behavior, pending completion, finalization,
  and callback delivery.
- The archived p01 no-spray/OFF package supplies the real packet-heavy scenario and production trace
  boundaries.

## Batch 1 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: frozen p01 red loop and hotspot selection.

Old active symbols and call paths: the p01 main binary at `ccf41b58`; existing p01 materialized
input and production `ub-quick-example` runner.

Continuous code blocks reused: T23 timing, hashing, artifact collection, serial timeout, and
counterbalanced ordering helpers, copied and adjusted only for p01 no-spray/OFF.

Reuse mode: test-asset / copy-and-adjust.

Required lifecycle adaptations: disable `ns3::UbLdstApi::UsePacketSpray` for both revisions and
append `UB_LIFECYCLE_TRACE_LEVEL=OFF` only for the lifecycle-capable current binary.

Preserved ordering and observable behavior: same binary per revision, input bytes per role, RNG run,
production parser/output path, and serial execution.

Existing characterization: archived p01 diagnostics and accepted no-spray semantic comparison.

Explicit behavior changes: none in production.

Excluded paths: all source files.

Focused validation: manifest hashes, successful completion, artifact hashes, one warm-up plus five
counterbalanced pairs, and bounded profile/site counts.

Re-approval triggers: incompatible input, unexplained model artifact difference, failed run, or a
profile that selects a different semantic owner.

## Batch 2 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: exact compressed active-attempt membership.

Old active symbols and call paths: pre-refactor packet builders, transfers, and terminals carry no
lifecycle membership container. The reusable pre-refactor `UbSlidingBitmapWindow` was inspected but
is a fixed-capacity protocol receive window whose marked/advance semantics cannot represent an
unbounded sparse active-attempt set without losing old live attempts.

Continuous code blocks reused: the current `RegisterPacketAttempt`, `ObservePacketTransfer`, and
`TerminatePacket` validation, counter mutation, trace, and callback blocks remain in place. Only the
three `activeAttempts` container operations are minimally adjusted.

Reuse mode: copy-and-adjust of the existing exact membership operations. The compressed layout uses
the same `std::map` ownership already present in `ResourceState`, with one 64-bit membership word per
active attempt range instead of one tree node per attempt.

Required lifecycle adaptations: insert/check/erase the exact attempt bit; erase a word only when its
last active attempt terminates.

Preserved ordering and observable behavior: attempt allocation, duplicate detection, in-flight and
terminal counters, sequence numbers, callback timing, DETAIL trace timing, and typed outcomes stay
in their current order.

Existing characterization: packet accounting, packet metadata transfer, parallel ordering, all
typed terminal integrations, and fail-fast duplicate/stale/retired tests in
`unified-bus-lifecycle`.

Explicit behavior changes: none.

Excluded paths: packet tags and serialization, resource lookup, parallel routing, builders/parsers,
protocol state machines, queues, schedulers, routing, and congestion control.

Focused validation: targeted build plus `unified-bus-lifecycle`, followed by the p01 gate after the
notification batch.

Re-approval triggers: an attempt outside the exact word representation, changed callback/trace
ordering, failed duplicate/stale tests, or a need to alter packet semantics.

## Batch 3 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: destroy-only lifecycle notification at the DRAINING seam.

Old active symbols and call paths: `UbController::CreateTp` ACTIVE path has no coordinator callback;
explicit destruction starts at `UbController::DestroyTp`.

Continuous code blocks reused: current callback registration statement, current `DestroyTp`
transition, `CanFinalize`, `NotifyLifecycleStateChanged`, and `FinalizeDestroy` cleanup.

Reuse mode: direct / move existing statement.

Required lifecycle adaptations: move existing coordinator callback installation from `CreateTp` to
the first DRAINING transition before the immediate finalization check.

Preserved ordering and observable behavior: creation order and packet paths otherwise remain exact;
freeze, callback collection, drain checking, finalization, and completion callback order remain
unchanged.

Existing characterization: packet lifecycle suite and existing TP connection destroy tests.

Explicit behavior changes: none.

Excluded paths: packet membership container layout, leases, resource storage, builders/parsers,
protocol state machines, retransmission, queues, scheduling, routing, and congestion control.

Focused validation:

- Targeted build: `ninja -C cmake-cache unified-bus-test test-runner ub-quick-example`
- `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle`
- The TP destroy characterizations embedded in `unified-bus-lifecycle` (idle, active, busy,
  reentrant, forced-stop, and generation-reuse cases).
- p01 no-spray/OFF semantic artifact comparison and performance rerun.

Re-approval triggers: profile selects another hotspot; a public API is required; callback/trace
ordering changes; a destroy completion edge is unclear; or any focused failure is unexplained.

## Batch 4 Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: stable execution-owner and canonical resource lookup reuse.

Old active symbols and call paths: pre-refactor modules directly retain their owning controller,
transport, and LDST objects rather than rediscovering them for every packet seam. Current lifecycle
must still route by the packet's generation-aware owner identity.

Continuous code blocks reused: current `GetOrCreate`, `Resolve`, `ValidateMutable`, canonical
`m_resources`, adapter creation, and all validation/abort statements remain the authority.

Reuse mode: minimal lookup cache around the existing canonical objects.

Required lifecycle adaptations:

- Reuse the last canonical `ResourceState` pointer while kind/local ID match; retain owner,
  generation, and retired validation on every mutation.

Preserved ordering and observable behavior: coordinator/adapter creation order, canonical map
iteration, generation validation, stale/retired errors, mutations, callbacks, traces, and all packet
behavior remain unchanged.

Existing characterization: the lifecycle suite repeatedly creates and destroys simulator/node
graphs and covers local/parallel owner routing, generation reuse, and stale rejection.

Explicit behavior changes: none.

Excluded paths: public APIs, packet tags, builders/parsers, protocol state machines, transfers,
terminals, queues, schedulers, routing, and congestion control.

Focused validation: targeted build, `unified-bus-lifecycle`, and final p01 no-spray/OFF gate.

Re-approval triggers: cache lifetime is not bounded by node disposal, canonical map storage changes,
parallel owner behavior changes, or a focused failure is unexplained.

## Ticket Full Acceptance Suite

Run serially:

1. Targeted build:
   `ninja -C cmake-cache unified-bus-test test-runner ub-quick-example`
2. Packet/lifecycle characterization:
   `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle`
3. TP destruction characterization: resolve the existing focused suite name from the test registry
   and run it; if destroy cases live inside the lifecycle suite, record that exact coverage rather
   than inventing a suite.
4. p01 no-spray/OFF: one warm-up plus five counterbalanced current/main measured pairs, binary/input
   hashes, wall/user/system/RSS/fault/I/O metrics, and non-lifecycle artifact hashes.
5. Compare pre/post current p01 task, throughput, and protocol artifacts exactly. Across main/current,
   allow only the already accepted same-timestamp TaskTrace ordering exception after proving equal
   row multisets.
6. Require final paired-median wall regression versus main no greater than 10 percent, or report a
   measured unexplained blocker without widening scope.

## Current Diff Scope

Allowed production files after Batch 1 selects the hotspot:

- `src/unified-bus/model/ub-transport-channel-manager.cc`
- `src/unified-bus/test/ub-test.cc` only for destroy-seam characterization missing from existing tests

Allowed ticket evidence:

- `.scratch/unified-bus-lifecycle-performance/T24-task-brief.md`
- `.scratch/unified-bus-lifecycle-performance/run_t24_p01.py`
- compact T24 manifest, summary, and report files under the same directory

Raw evidence remains local and untracked. Existing untracked p02 and differential artifacts are
read-only and must not be staged or removed.

## Validation Ledger

- 2026-07-17: acceptance suite, pre-refactor map, initial semantic batches, and allowed diff recorded
  before production edits.
- 2026-07-17: frozen p01 no-spray/OFF red loop PASS. All five current/main paired wall deltas are
  positive; paired median is `+20.19%`, median wall delta is `+20.10%`, and RSS is `+15.63%`.
  The 22 non-lifecycle task/throughput/protocol artifacts are byte-identical.
- 2026-07-17: bounded macOS Time Profiler runs collected 1,333 current and 1,042 main CPU samples.
  Current's hottest added chain is packet transfer: 55 samples include public transfer, 51 include
  owner routing, 27 include coordinator transfer mutation, with 25 leaf samples in the latter.
  Tag parsing, owner resolution, exact membership tree work, allocation, and release are also
  visible. ACTIVE destroy callback work has no profile sample in p01, so notification movement is an
  acceptance fix rather than the selected p01 performance optimization.
- 2026-07-17: inspected pre-refactor `UbSlidingBitmapWindow`; its fixed protocol window and
  mark/advance semantics cannot preserve arbitrary exact active membership. Batch 2 therefore keeps
  the existing map ownership and compresses 64 attempt membership bits per active tree node while
  retaining the current birth/transfer/terminal blocks.
- 2026-07-17: Batch 2/3 targeted build and `unified-bus-lifecycle` PASS. Post-change p01 retains
  byte-identical semantics and improves paired wall from `+20.19%` to `+18.54%`, RSS from `+15.63%`
  to `+14.60%`; this is useful but below the ticket target.
- 2026-07-17: post-change profile still shows 46 transfer-chain samples, 12 coordinator resolve
  samples, 7 coordinator `GetOrCreate` samples, and repeated `Object::DoGetObject`/resource lookup.
  Batch 4 is limited to reuse of already-canonical owner/resource objects; if it does not reach the
  gate, T24 stops with the measured blocker rather than expanding again.
- 2026-07-17: rejected a process-static non-owning coordinator cache before acceptance. The
  fail-fast child process inherits static state across `fork` while rebuilding NodeList state, so the
  cache produced an owner mismatch. The cache was removed rather than special-cased; Batch 4 retains
  only the object-local canonical resource lookup cache.
- 2026-07-17: final targeted build PASS and `unified-bus-lifecycle --verbose` PASS. The suite's
  expected fail-fast child aborts were collected normally; all lifecycle cases passed, including TP
  destroy, packet underflow, stale generation, and retired mutation characterization.
- 2026-07-17: final serial p01 gate completed with one warm-up and five counterbalanced pairs. All
  five current/main wall deltas remain positive. Paired-median wall is `+16.65%`, median wall is
  `+16.29%`, and RSS is `+15.29%`. The semantic comparison has no current-only, main-only, hash, or
  unexplained mismatch.
- 2026-07-17: independent review against `main@ccf41b58` PASS for behavior preservation. TP
  creation/destruction order outside the lifecycle seam is unchanged; exact membership,
  generation/retired validation, terminal counters, traces, and callbacks retain their ordering. No
  packet builder/parser, protocol state machine, ACK/RTO, queue/scheduler, routing, or congestion
  implementation was added or changed.
- 2026-07-17: the ticket's 10 percent wall threshold remains unmet. Per the acceptance stop rule,
  T24 closes with this measured blocker rather than widening into speculative protocol or ownership
  redesign. Implementation commit: `83080d6`.
