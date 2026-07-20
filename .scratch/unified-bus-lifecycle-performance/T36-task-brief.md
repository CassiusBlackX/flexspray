# T36 Task Brief: Lazily create CTP send-window context

Status: accepted

Final gate: PASS. The repair-backed candidate matches `next@cf8ee683` exactly in every frozen and
complex correctness case, passes the 2 GiB wall/RSS thresholds, and remains neutral at 8 GiB.

## Ticket-defined acceptance suite

1. Targeted build: `ninja -C cmake-cache unified-bus unified-bus-test test-runner`.
2. Focused suites: `unified-bus-ctp-transaction`, `unified-bus-ctp-lifecycle`,
   `unified-bus-lifecycle-storage`, `unified-bus-lifecycle-trace`, and
   `unified-bus-lifecycle`.
3. Characterization seam: creating a Transaction-owned CTP state does not allocate a send-window
   context; the existing service context seam creates it with the retained TASSN cursor, and normal
   send, ACK, delayed-ACK, forced-terminal, and finite-retirement cases remain green.
4. Real CTP correctness: run the frozen `ctp-win1`, `ctp-win4`, and `ctp-win128` cases with current
   and `next@cf8ee683`; shared task, throughput, packet, queue, and protocol artifacts must be byte
   identical. Any difference must have a path-, hash-, revision-, and cause-bound exception approved
   before acceptance.
   The final production correction also replays the multi-Entity and mixed-bidirectional canaries.
5. Real CTP performance: reuse the frozen four-flow `ctp-win4` input at 2 GiB total payload, all
   legacy and lifecycle trace output OFF, one warm-up per role, and five counterbalanced
   current/next pairs. Report wall, user CPU, system CPU, peak RSS, exact revisions, binary hashes,
   and every measured pair.
6. Performance gate: reduce the archived T35 current/next wall gap by at least eight percentage
   points, leave the final paired-median current/next wall gap at no more than five percent, and keep
   current peak RSS within ten percent of next. A noisy or missed gate is reported rather than
   averaged away.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`

Old active symbols:

- `UbCtpTransportService::GetOrCreateEntityState`
- `UbCtpTransportService::GetOrCreateTransactionContext`
- `UbCtpTransportService::SendSegment`
- `UbCtpTransportService::DrainJettys`
- `UbCtpTransportService::TrackInboundTaPacket`
- `UbCtpTransportService::CompleteFromTaAck`
- `UbCtpTransportService::CompleteFromTaAckNow`
- `UbCtpTransportService::ResolveWireTaSsn`

Old call paths:

- Send: Jetty admission -> `DrainJettys` or direct `SendSegment` -> `Tx().context->TryAdmit` ->
  inherited packet builder -> CTP queue.
- Receive: `HandleReceivedPacket` -> `TrackInboundTaPacket` -> `Rx().inboundTaUnits` -> inherited
  request processor -> WRITE TAACK or READ response builder.
- ACK: `HandleReceivedPacket` -> `CompleteFromTaAck` -> optional delayed callback ->
  `CompleteFromTaAckNow` -> `Tx().context->MarkTaAck` -> Transaction completion.

Code blocks copied/extracted:

- Move the existing context construction block, including the retained cursor and unchanged
  `UB_JETTY_TASSN_OOO_THRESHOLD`, out of unconditional state creation into one lazy Transaction-owner
  context seam.
- Reuse every existing send/ACK caller and replace only its context source with that seam.
- Keep the current Transaction-owned `inboundTaUnits` block unchanged.

Required lifecycle adaptations:

- A Transaction-owned CTP state may exist with a null context while it contains receiver-only
  assembly or an admitted operation that has not yet reached send-window admission.
- Finite retirement accepts a null context and records `sendNext` only when a context exists.
- The existing testing query reports actual context presence instead of using state presence as a
  proxy.

Explicit behavior changes:

- Receiver-only WRITE requests and READ requests/responses no longer allocate or reset a send ACK
  bitmap.
- No wire, modeled timing, scheduling, queueing, ACK, or operation-completion behavior is intended to
  change.

Preserved ordering and observable behavior:

- Window admission remains before Transaction segment commit and packet construction.
- Header/tag construction, packet birth, queue registration/enqueue, trace, port trigger, ACK
  marking, completion callback, and finite-retirement order remain unchanged.
- TASSN continuity still comes from `m_ctpNextTaSsn` and is saved only after an instantiated window
  drains.

Characterization evidence:

- Existing CTP Transaction and lifecycle fixtures cover window admission, wire ACK unwrap, delayed
  TAACK, route loss, forced terminal, READ/WRITE completion, queue priority, and finite cleanup.
- T35 identifies the receiver call path and records a stable `+15.04%` 2 GiB wall gap and growing
  RSS against `next@cf8ee683`.

## Batch Reuse Map

Pre-refactor commit: `main@ccf41b58`

Semantic batch: CTP receiver-state/send-window separation.

Old active symbols and call paths: the send, receive, and ACK paths listed in the pre-refactor map.

Continuous code blocks reused: existing context constructor call, Transaction state map, receive
assembly block, window admission/ACK blocks, finite retirement, and existing fixtures.

Reuse mode: minimal extraction plus direct caller reuse.

Preserved ordering and observable behavior: all protocol and lifecycle ordering listed above.

Required lifecycle-only adaptations: nullable context, lazy creation on send/ACK window use, and
nullable finite retirement.

Existing characterization: the ticket-defined focused suites and frozen CTP window cases.

Explicitly excluded paths: packet builders/parsers, CTP queue and allocator behavior, routing,
Jetty inflight semantics, ACK-window algorithm/capacity, delayed-event scheduling, RTP, LDST,
retransmission, DCQCN, automatic TP cleanup, and bitmap `Resize/Reset` optimization.

Focused validation: the ticket-defined acceptance suite.

Re-approval triggers: a context-free caller needs ACK/window behavior; any builder/parser/window,
Jetty, queue, routing, callback, trace, or timer ordering must change; byte output differs; the
performance gate is missed for an unclear reason; or the same failure occurs twice.

## Batch Reuse Map: complete inbound unit fast path

Pre-refactor commit: `main@ccf41b58`

Semantic batch: CTP complete inbound Transaction unit storage.

Old active symbols and call paths:

- `UbCtpTransportService::HandleReceivedPacket` parses the production packet and calls
  `TrackInboundTaPacket` before the unchanged Transaction request processor and response path.
- `UbCtpTransportService::TrackInboundTaPacket` creates one `UbCtpInboundTaUnit`, copies the wire
  fields into `UbWqeSegment`, updates received payload, and returns the completed request.
- The pre-refactor unit lived in `UbCtpEntityState::Rx().inboundTaUnits`; the lifecycle branch moved
  the same block into `UbCtpTransactionState::inboundTaUnits` without changing its protocol logic.

Continuous code blocks reused: retain the complete pre-refactor/current unit initialization,
segment field assignment, payload accounting, completion check, and completed-segment return block
without rewriting fields or changing their order.

Reuse mode: copy-and-adjust the old storage selection only. A complete first packet uses a stack
`UbCtpInboundTaUnit`; an already retained unit or a future incomplete first packet continues through
the existing Transaction-owned `inboundTaUnits` map and unchanged assembly block.

Preserved ordering and observable behavior: production datalink/CNA/CTP/TA parsing, receive trace,
all `UbWqeSegment` field assignments, payload update, Transaction request processing, remote-memory
effect, response construction, routing hint, response enqueue, and finite retirement remain in
their current order. Duplicate complete TASSNs remain independently processable as before.

Required lifecycle-only adaptations: do not allocate a canonical Transaction state or an inbound
map node when the current packet completes a previously unseen CTP Transaction segment. Retained
partial state, if encountered, remains canonical and retirement-aware.

Existing characterization: `UbCtpLocalDataPacketGeneratesTaAckTest`, the CTP Transaction and
lifecycle suites, the frozen `ctp-win1`/`ctp-win4`/`ctp-win128` byte comparison, and the archived
2 GiB five-pair performance comparison. The archived T36 `+12.00%` wall gap and 57.7 MB median RSS
are the RED performance gate for this batch.

Explicit behavior changes: none at packet, timing, response, callback, trace, queue, ACK-window, or
lifecycle outcome boundaries. Allocation lifetime for a complete first inbound unit is intentionally
shortened from two transient tree nodes to one stack object.

Excluded paths: packet builders/parsers, request execution, response builders, CTP send/ACK window,
Jetty admission/inflight semantics, queue/allocator behavior, routing, delayed events, trace setup,
RTP, LDST, retransmission, DCQCN, and automatic TP cleanup.

Focused validation: the existing ticket-defined build and focused suites, followed by the frozen
three-case byte comparison and 2 GiB five-pair performance gate against `next@cf8ee683`.

Re-approval triggers: the complete-first-packet condition no longer follows the declared CTP
one-packet-per-segment constraint; retained partial assembly needs new semantics; any segment field,
parser/builder, request/response, callback/trace/timer, ACK/window, Jetty, queue, or routing order must
change; bytes differ; or the same failure occurs twice.

## Diagnostic Ablation Reuse Map: stable CTP queue registration

Pre-refactor commit: `main@ccf41b58`; correctness control: GitCode `next@cf8ee683`.

Semantic batch: diagnose whether finite empty-queue retirement changes multi-Entity allocator order.

Old active symbols and call paths:

- `UbCompactTransportChannel::GetNextPacket` pops the final packet without scheduling queue cleanup.
- `UbCtpTransportService::GetOrCreateQueue` retains the queue in `m_queues`.
- `UbCtpTransportService::RegisterQueueIfNeeded` registers each retained queue once.
- `UbRoundRobinAllocator::SelectNextIngressQueue` advances by stable vector slot.

Continuous code blocks reused: restore the old dequeue-to-retained-empty-queue behavior by
temporarily omitting only the lifecycle branch's `SetEmptyCallback` binding. Packet dequeue,
registration, enqueue, port trigger, and RR selection remain unchanged.

Reuse mode: direct pre-refactor behavior restoration for one diagnostic binary.

Preserved ordering and observable behavior: packet construction, queue priority, dequeue order,
port notification, ACK/window processing, parser/trace boundaries, and RR implementation are
unchanged. Only empty-queue unregister/erase and its scheduled callback are suppressed.

Required lifecycle-only adaptations: none. This is not an implementation candidate and does not
change the approved T36 batch.

Existing characterization: the frozen 16-Entity, 1 MiB-per-operation trace-on canary and its
existing `next@cf8ee683` result.

Explicit behavior change: empty CTP queues remain registered and mapped for the diagnostic run,
matching the pre-refactor queue lifetime and intentionally bypassing finite queue retirement.

Excluded paths: builders, parsers, Transaction ownership, segmentation, ACK/window algorithms,
Jetty admission, routing, allocator code, packet lifecycle, RTP, LDST, retransmission, and DCQCN.

Focused validation: serial targeted `ub-quick-example` build; one diagnostic multi-Entity canary;
SHA-256 comparison of every shared output/runlog artifact and parsed task rows against next.

Re-approval triggers: the canary remains different, any input hash differs, build/run fails for an
unclear reason, or confirming the hypothesis would require a second production semantic change.

Rollback condition: restore the callback binding and rebuild the original T36 fast-path binary
after evidence capture, regardless of the diagnostic outcome.

## Batch Reuse Map: preserve CTP scheduler identity

Pre-refactor commit: `main@ccf41b58`; correctness control: GitCode `next@cf8ee683`.

Semantic batch: decouple finite CTP work retirement from the allocator identity of an empty
transmit queue.

Old active symbols and call paths:

- `UbCompactTransportChannel::GetNextPacket` pops the selected packet and retains the empty queue.
- `UbCtpTransportService::GetOrCreateQueue` returns the same queue for a stable
  `(EntityKey, outPort, priority)` key.
- `UbCtpTransportService::RegisterQueueIfNeeded` registers that queue once, preserving its
  non-VOQ vector slot and the existing RR/DWRR cursor interpretation.
- `UbCtpTransportService::DoDispose` unregisters all remaining CTP queues at service disposal.

Continuous code blocks reused: restore the pre-refactor final-pop behavior and persistent
`m_queues`/`m_registeredQueues` ownership. Keep the current complete-packet queue, priority order,
registration, enqueue, port trigger, and disposal blocks unchanged.

Reuse mode: direct pre-refactor behavior restoration; remove only the lifecycle branch's automatic
empty callback, scheduled retirement, and callback detachment.

Required lifecycle adaptations: finite Transaction state, packets, delayed events, and lifecycle
attempts continue to retire independently. An empty CTP queue shell remains node-service scheduler
state until service disposal because its allocator slot is observable protocol state.

Preserved ordering and observable behavior: queue creation/registration order, non-VOQ slot,
RR/DWRR cursor interpretation, packet priority, dequeue, port notification, ACK/window processing,
header/tag bytes, traces, and disposal unregister order match the pre-refactor path.

Existing characterization: reuse `UbCtpSendSegmentRegistersAndTriggersPortTest` to require stable
queue object identity and registration after drain; reuse the 16-Entity `multi-entity-2g` canary and
its complete output/runlog hash manifest against `next@cf8ee683`.

Explicit behavior changes: supersede T12's automatic empty-queue object retirement. Empty queue
shells remain retained during normal service lifetime; finite protocol work still retires. Update
the T12 closeout so it no longer claims allocator unregister/erase at ordinary empty transition.

Excluded paths: allocator implementation, packet builders/parsers, Transaction segmentation,
ACK/window algorithms, Jetty admission, routing, lifecycle packet accounting, RTP, LDST,
retransmission, DCQCN, and automatic TP cleanup.

Focused validation: RED/GREEN `unified-bus-ctp-lifecycle`; serial targeted build; CTP Transaction
and lifecycle suites; lifecycle suite; exact `multi-entity-2g` comparison against next; then the
existing 2 GiB five-pair and large-scale memory/performance matrices.

Re-approval triggers: stable bytes require an allocator algorithm/API change; disposal can no
longer unregister retained queues; finite Transaction/event/packet cleanup regresses; any
unexplained byte difference remains; or the same failure occurs twice.

## Batch Reuse Map: compact OFF retired-generation history

Pre-refactor commit: `main@ccf41b58`; storage baseline: T25 implementation `1afd398`.

Semantic batch: preserve exact generation and terminal-outcome history without retaining one
ordered-map node for every retired local ID when lifecycle trace is OFF.

Old active symbols and call paths:

- `UbLifecycleCoordinator::OpenResource` reuses the latest generation for an active key or
  increments the retained generation after retirement.
- `UbLifecycleCoordinator::RetireResource` validates packet and lease balance, records the terminal
  outcome, emits the retirement trace when enabled, and releases active-only state.
- `UbLifecycleCoordinator::ValidateMutable`, `IsResourceActive`, and `GetSnapshot` enforce exact
  owner/kind/local-ID/generation identity and distinguish active, retired, stale, and unknown state.
- `UbTransaction::CommitCtpWqeSegment` and `TryAdmitRtpSegment` use
  `(jettyNum << 32) | taSsn` as the Transaction-segment local ID; the existing Jetty TASSN cursor
  advances monotonically, producing long contiguous retired-ID ranges under CTP scale traffic.

Continuous code blocks reused: retain the existing resource-open, balance validation,
terminal-outcome selection, RESOURCE_RETIRE trace, active-state release, generation comparison,
snapshot reconstruction, and mutable-state validation blocks. Extract only retired-history lookup,
take, and merge operations around the current map seam.

Reuse mode: minimal extraction and storage substitution from the T25 active/retired split.

Required lifecycle adaptations: with trace OFF, move a retired resource's compact
`generation/hasTerminalOutcome/terminalOutcome` tuple into an ordered run that merges adjacent local
IDs with identical history, then erase the per-ID `ResourceState` shell. Reopening removes exactly
one ID from its run, increments its generation through the existing open path, and creates the
normal active state. Enabled trace levels retain the current per-resource final summary path.

Preserved ordering and observable behavior: retirement balance checks, terminal selection,
RESOURCE_RETIRE emission, active-state release, generation increment, stale/unknown/retired error
classification, packet/lease identity, callbacks, and enabled summary bytes remain unchanged.

Existing characterization: T25 `unified-bus-lifecycle-storage`, lifecycle packet accounting,
parallel stale-generation rejection, lifecycle fail-fast cases, explicit SUMMARY byte milestone,
and the T36 CTP frozen/complex next comparisons.

Explicit behavior changes: none. `GetSnapshot` for an OFF retired identity still reconstructs the
same compact retired and terminal-outcome fields established by T25. The storage test additionally
exposes the number of compact history runs.

Excluded paths: packet builders/parsers, CTP and RTP algorithms, ACK/window processing, Jetty
admission, Transaction completion order, queue/allocator/RR behavior, routing, packet lifecycle
accounting, LDST, retransmission, DCQCN, and TP cleanup.

Focused validation: RED/GREEN `unified-bus-lifecycle-storage`; targeted Unified Bus build; lifecycle
and trace suites; exact complex CTP bytes against `next@cf8ee683`; then serial 512 MiB, 2 GiB, and
8 GiB performance/RSS measurements.

Re-approval triggers: a non-OFF summary must use compact runs; generation or error classification
changes; an active resource can alias retained history; a protocol caller needs a new public API;
bytes differ; memory remains linear in retired segment count; or the same failure occurs twice.

## Current diff scope

- `.scratch/unified-bus-lifecycle-performance/T36-task-brief.md`
- `.scratch/unified-bus-lifecycle-performance/T36-report.md`
- `.scratch/unified-bus-lifecycle-performance/T36-summary.json`
- T36 evidence under `.scratch/unified-bus-lifecycle-performance/evidence/`
- Diagnostic experiment package under `scratch/20260720-ctp-fastpath-memory-scaling/`
- `tickets.md`
- `src/unified-bus/model/protocol/ub-transaction.h`
- `src/unified-bus/model/protocol/ub-transaction.cc`
- `src/unified-bus/model/ub-lifecycle.h`
- `src/unified-bus/model/ub-lifecycle.cc`
- `src/unified-bus/model/protocol/ub-ctp.h`
- `src/unified-bus/model/protocol/ub-ctp.cc`
- `src/unified-bus/test/ub-test.cc`

## Evidence log

- 2026-07-20: user approved lazy CTP context implementation and required byte correctness and host
  performance acceptance against GitCode `next`.
- 2026-07-20: pre-refactor receive, send, ACK, state-owner, and retirement paths were compared with
  `main@ccf41b58`; the batch boundary and acceptance suite are fixed above.
- 2026-07-20: RED confirmed with the no-suffix test runner: state creation eagerly returned a
  non-null context at the agreed characterization seam. The stale `-default` runner was excluded
  after it loaded an older test dylib and produced a false PASS.
- 2026-07-20: the no-suffix `unified-bus-lifecycle` suite exposed a pre-existing T32 fixture gap:
  two packet-transfer tests expected transfer evidence while leaving the global level OFF. Both
  tests now explicitly select SUMMARY and reset it on every exit; no production behavior changed.
- 2026-07-20: targeted no-suffix build passed. `unified-bus-ctp-transaction`,
  `unified-bus-ctp-lifecycle`, and `unified-bus-lifecycle` passed serially.
- 2026-07-20: inline two-axis review against this brief and repo standards found no production
  finding. Review against `main@ccf41b58` confirmed that receive assembly remains separate from
  send-window operations, and that the current builder, parser, queue, Jetty, ACK, trace, callback,
  and timer blocks are unchanged.
- 2026-07-20: the first T36 implementation removed eager send-window construction but missed its
  performance gate: the five-pair 2 GiB comparison remained `+12.00%` slower than
  `next@cf8ee683`, and median RSS remained 57.7 MB versus 15.5 MB. Profiling removed the bitmap
  constructor hotspot but left per-segment Transaction state and `inboundTaUnits` tree allocation
  in `HandleReceivedPacket`; this failed result is the RED gate for the complete-unit fast path.
- 2026-07-20: the complete-unit candidate keeps the pre-refactor segment construction and payload
  block intact, selects a stack `UbCtpInboundTaUnit` for a complete first packet, and retains the
  Transaction-owned map path for an existing or future incomplete unit. The targeted build and
  `unified-bus-ctp-transaction`, `unified-bus-ctp-lifecycle`, and `unified-bus-lifecycle` suites
  passed serially with the no-suffix runner.
- 2026-07-20: the frozen `ctp-win1`, `ctp-win4`, and `ctp-win128` current/next replay passed under
  `.scratch/unified-bus-lifecycle-performance/evidence/t36-ctp-fastpath-correctness-v1`. Every
  shared artifact and task row is byte-identical, all completion timestamps match, and the three
  distinct Jetty inflight-window effects are reproduced exactly. No exception is present.
- 2026-07-20: the 2 GiB five-pair run under
  `.scratch/unified-bus-lifecycle-performance/evidence/t36-ctp-fastpath-performance-v1` reports a
  `+0.49%` paired-median wall gap, but individual pairs range from `-34.13%` to `+25.75%` under a
  noisy host. Role medians are 11.764 s current versus 10.941 s next. Median peak RSS remains
  57,589,760 B current versus 15,515,648 B next, so the ticket RSS gate failed decisively at that
  checkpoint.
- 2026-07-20: the matching 2 GiB Time Profiler evidence under
  `.scratch/unified-bus-lifecycle-performance/evidence/t36-ctp-fastpath-profile-v1` still measures
  12,506 ms current versus 11,191 ms next. Current `HandleReceivedPacket` is 2,697 ms versus
  1,790 ms; `CompleteFromTaAckNow` is 1,489 ms versus 725 ms; lifecycle work is 818 ms (6.54%).
  The complete-unit allocation is not the dominant RSS source.
- 2026-07-20: source review localizes the RSS high-water to canonical retired-resource history:
  `UbLifecycleCoordinator::m_resources` intentionally retains one compact `ResourceState` map node
  for every distinct Transaction-segment local ID after its active state is released. A 2 GiB CTP
  run creates hundreds of thousands of distinct TASSN-derived segment IDs. This is the explicit T25
  generation/stale-rejection contract and could not be changed inside the then-approved CTP
  receiver-storage batch. T36 remained in progress at that checkpoint pending a user decision on
  whether to keep the CPU-only improvement and split tombstone compaction into a new ticket, reject
  the candidate, or widen T36 into a cross-module lifecycle-storage change.
- 2026-07-20: the user approved a single-variable queue-retirement diagnostic after the complex
  canary showed lazy and fastpath were identical to each other but different from next. Omitting
  only `UbCtpTxQueue`'s empty callback binding restored exact equality with `next@cf8ee683` for all
  16 shared output/runlog artifacts and all parsed task rows, with identical input hashes. This
  confirms empty-queue unregister/erase and consequent allocator-slot churn as the cause of the
  multi-Entity divergence; Transaction-owned segmentation and the complete-unit fast path are
  excluded by the ablation. The diagnostic source change was then restored. Source, executable,
  and Unified Bus library SHA-256 values returned exactly to their pre-ablation values:
  `89c2d262...`, `7756e587...`, and `856aea69...`. Evidence is under
  `scratch/20260720-ctp-fastpath-memory-scaling/results/diagnostics/queue-retirement-disabled`.
- 2026-07-20: the user approved the production correction and continuation of performance testing.
  The implementation batch above restores the pre-refactor stable queue identity while retaining
  current finite Transaction, packet, and event cleanup.
- 2026-07-20: the production stable-queue repair made both `multi-entity-2g` and
  `mixed-bidir-2g` byte-identical to `next@cf8ee683` across every shared output/runlog artifact and
  parsed task row. The frozen `ctp-win1`, `ctp-win4`, and `ctp-win128` replays also remain exact;
  there is no correctness exception.
- 2026-07-20: the user approved extending T36 to the measured lifecycle storage source. OFF retired
  history now stores adjacent equal generation/outcome tuples as compact runs and reconstructs the
  same retired snapshot. The 100,000-resource storage characterization occupies one run, middle-ID
  reopen creates two side runs, replacement retirement preserves its distinct generation/outcome,
  and all five focused suites pass.
- 2026-07-20: five counterbalanced 2 GiB pairs under
  `scratch/20260720-ctp-fastpath-memory-scaling/results/performance-compact-history-v1` measure a
  `+4.20%` paired-median wall gap, `+4.05%` user CPU gap, and `-0.74%` RSS delta versus next. The
  current median peak RSS is 15,335,424 B versus 15,499,264 B, replacing the pre-compaction
  57,589,760 B failure and passing the ticket's five-percent wall and ten-percent RSS gates.
- 2026-07-20: an independent five-pair 8 GiB replay under
  `scratch/20260720-ctp-fastpath-memory-scaling/results/performance-compact-history-8g-v2` measures
  `-0.01%` wall, `+0.26%` user CPU, and `-0.42%` RSS. Current median peak RSS is 15,384,576 B versus
  15,417,344 B, so the former roughly 184.5 MB payload-scaled high-water is eliminated.
- 2026-07-20: final inline review and two independent read-only review axes found no missing old
  CTP branch, altered builder/parser/ACK/window/Jetty ordering, parallel algorithm, generation
  classification error, active/history overlap, or enabled-trace change. One low-risk pre-existing
  TP TPN-reuse cross-generation test gap remains outside T36; no current production failure was
  found on that path.
