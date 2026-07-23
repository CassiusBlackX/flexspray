# T31 Task Brief: Fuse Packet Owner And Coordinator Resolution

Status: closed

## Objective

Remove duplicate owner-node validation and lookup from packet transfer and terminal routing without
changing lifecycle functionality. The current local path calls `IsExecutionOwner`, then calls
`UbLifecycleCoordinator::Resolve`; both validate the node ID and the latter looks up the same node
again. T31 extracts the existing bodies and lets the packet path reuse the node already resolved by
the owner check.

No coordinator registry, static/thread-local cache, packet pointer state, public API, owner model, or
protocol change is allowed.

## Ticket Acceptance Suite

Run build, test, and simulation work serially.

1. `git diff --check`.
2. After extracting helpers and migrating only old callers, run
   `ninja -C cmake-cache unified-bus unified-bus-test test-runner` and
   `./build/utils/ns3.44-test-runner-default --test-name=unified-bus-lifecycle`.
3. After connecting packet transfer/terminal, rerun the targeted build only for the changed source,
   then run `unified-bus-lifecycle` and `unified-bus-switch-voq`.
4. Run one warm-up plus five counterbalanced frozen pre-T31/T31 p01 pairs, strictly serial.
5. Require exact generated task, throughput, and trace artifacts in every pair.
6. Retain only if paired-median wall improves by at least 1.5 percent, at least four of five wall
   pairs do not regress, and paired-median user CPU and RSS do not regress by more than 1 percent.
   This ordinary ticket does not create a main frozen-byte milestone.

## Batch Reuse Map

Pre-refactor commit: `main@ccf41b58`.

Semantic batch: packet execution-owner classification and local coordinator resolution.

Old active symbols: pre-refactor `UbLink::TransmitStart`, `UbEgressQueue::DoEnqueue`, and
`UbEgressQueue::DoDequeue`; current `UbParallelLifecycleAdapter::IsExecutionOwner`,
`UbLifecycleCoordinator::Resolve`, `RoutePacketTransfer`, and `RoutePacketTerminal`.

Old call paths: main queue/link paths preserve the existing packet and callback order without
lifecycle observation. The current lifecycle seam reads the packet identity, classifies the owner,
resolves the owner node's coordinator when local, and then runs the existing transfer or terminal
mutation; remote owners take the existing causal-control path.

Continuous code blocks reused: the full node-range, MPI system ID, MTP logical-process, current-LP,
node lookup, coordinator aggregate lookup, missing-coordinator abort, route-record construction,
source-context validation, transfer mutation, and terminal mutation blocks.

Reuse mode: minimal extraction of existing active bodies, old callers first, then direct reuse by
the packet route callers.

Required lifecycle adaptations: the extracted owner helper accepts an optional `Ptr<Node>*` output.
The public old caller supplies no output and keeps its prior cost/behavior. Packet routing requests
the node and passes it to the extracted coordinator helper, eliminating the second range check and
node lookup. The coordinator TypeId lookup itself remains unchanged from pre-T31 after T30 rejection.

Preserved ordering and observable behavior: unknown-node abort; MPI check; MTP current-LP check and
owner classification; local coordinator lookup and missing-coordinator abort; transfer/terminal
generation and attempt validation; counters; callback/trace order; remote source-context validation;
control record fields; routing timestamp.

Existing characterization: `unified-bus-lifecycle` packet transfer, terminal, stale generation,
expected fail-fast, cross-owner control, and adapter accounting cases; `unified-bus-switch-voq`; p01
production task/throughput/trace artifacts.

Explicit behavior changes: none.

Excluded paths: builders/parsers, queue/link algorithms, packet tags, lifecycle storage/layout,
ACK/RTO/retransmission, CTP/RTP, flow/congestion control, MTP/MPI transport algorithms, trace schema,
public API, automatic TP cleanup, and coordinator caching.

Focused validation: the ticket acceptance suite above.

Re-approval triggers: any existing owner branch or abort changes; an MTP/MPI control path requires
new logic; a public API/cache/registry is needed; semantic artifacts differ; performance misses its
gate; or two equivalent failures occur.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`.

Old active symbols and call path: the unchanged queue/link functions listed above. Main has no
lifecycle owner routing, so current reviewed lifecycle functions are the required compatibility
behavior and are reused rather than redesigned.

Code blocks copied/extracted: exact current `IsExecutionOwner` and `Resolve` bodies.

Required lifecycle adaptations: optional return of the already classified owner node to the current
local packet route.

Explicit behavior changes: none.

Preserved ordering/observable behavior: all behavior listed in the Batch Reuse Map.

Characterization evidence: main source comparison, T30 profile/evidence, lifecycle/VOQ suites, and
frozen pre-T31 p01 artifacts.

## Review Checklist

- Extracted helpers contain every old condition, abort, and branch in the same order.
- Old callers use the helpers before packet route callers are connected.
- Local packet route performs one owner check/node lookup and the same coordinator mutation.
- Remote packet route does not resolve a local coordinator and preserves causal control.
- No registry, cache, packet pointer, duplicate state machine, or new owner semantics are added.
- Queue/link, callback/trace, terminal, and simulation timestamp order remain unchanged.

## Ledger

- 2026-07-17: T31 opened from `02acfbb`. Source behavior and the frozen pre-T31 executable/library
  remain identical to T29/T30 baseline hashes `7756e587...45a78` and `0968fced...32878`.
- 2026-07-17: root approved the single semantic batch and existing characterization seam.
- 2026-07-17: extracted the old owner-classification and coordinator-resolution bodies, migrated the
  old callers first, and passed the targeted build plus `unified-bus-lifecycle` before connecting
  packet transfer/terminal.
- 2026-07-17: after packet route migration, the targeted build, `unified-bus-lifecycle`, and
  `unified-bus-switch-voq` passed. Existing expected fail-fast subprocess cases remained accepted by
  the lifecycle parent suite.
- 2026-07-17: all five p01 semantic comparisons were exact, but all five T31 wall pairs regressed.
  Paired-median wall was `+0.50%`, user CPU was `+1.54%`, and RSS was flat.
- 2026-07-17: T31 failed every performance-retention condition and was reverted. Together with T30,
  this is the second equivalent owner-resolution optimization failure; no further variants on this
  chain are authorized without new measurement evidence.
- 2026-07-17: root closeout review confirmed no source/test diff remains and the rebuilt Unified Bus
  library exactly matches the frozen pre-T31 SHA-256 `0968fced...32878`.
