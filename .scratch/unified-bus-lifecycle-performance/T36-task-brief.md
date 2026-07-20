# T36 Task Brief: Lazily create CTP send-window context

Status: in-progress

## Ticket-defined acceptance suite

1. Targeted build: `ninja -C cmake-cache unified-bus unified-bus-test test-runner`.
2. Focused suites: `unified-bus-ctp-transaction`, `unified-bus-ctp-lifecycle`, and
   `unified-bus-lifecycle`.
3. Characterization seam: creating a Transaction-owned CTP state does not allocate a send-window
   context; the existing service context seam creates it with the retained TASSN cursor, and normal
   send, ACK, delayed-ACK, forced-terminal, and finite-retirement cases remain green.
4. Real CTP correctness: run the frozen `ctp-win1`, `ctp-win4`, and `ctp-win128` cases with current
   and `next@cf8ee683`; shared task, throughput, packet, queue, and protocol artifacts must be byte
   identical. Any difference must have a path-, hash-, revision-, and cause-bound exception approved
   before acceptance.
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

## Current diff scope

- `.scratch/unified-bus-lifecycle-performance/T36-task-brief.md`
- `.scratch/unified-bus-lifecycle-performance/T36-report.md`
- `.scratch/unified-bus-lifecycle-performance/T36-summary.json`
- T36 evidence under `.scratch/unified-bus-lifecycle-performance/evidence/`
- `tickets.md`
- `src/unified-bus/model/protocol/ub-transaction.h`
- `src/unified-bus/model/protocol/ub-transaction.cc`
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
