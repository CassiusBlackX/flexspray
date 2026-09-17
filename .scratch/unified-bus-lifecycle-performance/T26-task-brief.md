# T26 Task Brief: Reuse Validated RTP Transport Resolution

Status: complete

Branch: `codex/unified-bus-lifecycle-implementation`

Starting revision: `45505cd`

## Ticket Acceptance

1. Direct-TP and TPG WQE submission resolve the generation-aware transport reference once and pass
   that validated result from preflight into admission commit.
2. TPG member materialization is reused only while the exact group identity and membership version
   match. Binding, member add/remove, retiring/freeze/resume, and destroy changes invalidate reuse.
3. Selection continues from the existing group cursor, skips the same non-selectable or stale
   members, commits the same selected index, and preserves no-member versus backpressure outcomes.
4. First send, TPNAK, and RTO remain pinned to `SegmentRecord::resolvedTpIdentity`; no packet,
   parser, builder, ACK/RTO, queue, scheduler, routing, or congestion behavior changes.
5. Focused counters prove fewer resolution and materialization operations. Targeted lifecycle and
   transport suites pass, and the bounded `m04` current/next RTP-TPG scenario records semantics and
   host time without requiring a performance gain.

## Pre-refactor Reuse Map

Pre-refactor commit: `main@ccf41b58`

Old active symbols:

- `UbTransaction::JettyBindTp`
- `UbTransaction::GetJettyRelatedTpVec`
- `UbTransaction::TriggerScheduleWqeSegment`
- `UbTransaction::ScheduleWqeSegment`

Old call path:

`JettyBindTp -> build ubTransportGroup -> m_jettyTpGroup[jettyNum] ->
GetJettyRelatedTpVec -> schedule the retained transport pointers`

Code blocks copied/extracted:

- Reuse the old binding-owned retained vector shape rather than rebuilding a second transport
  selection algorithm.
- Reuse the current lifecycle branch's `ResolveRtpTransportReference`, group cursor iteration,
  member generation checks, `CanAdmitWqeSegment`, and `CommitSelection` blocks unchanged in order.
- Reuse `SegmentRecord::resolvedTpIdentity` and the existing retransmission paths as the pinning
  mechanism; no new retransmission lookup is introduced.

Required lifecycle adaptations:

- Key retained TPG materialization by exact `RTP_TPG` lifecycle identity and membership version.
- Increment the version when membership, selectable state, accepting state, or destroy state changes.
- Preserve an entry for every group index so cached lookup cannot alter cursor/index semantics.
- Check exact member generation and active state at every segment admission even on a cache hit.
- Carry a validated direct/group resolution from WQE preflight into the owner-local commit.

Explicit behavior changes:

- Successful direct and TPG WQE admission performs one transport-reference resolution instead of
  four.
- Repeated materialization with unchanged group identity/version reuses retained transport pointers.

Preserved ordering/observable behavior:

- Jetty validation, transport preflight result mapping, duplicate-operation rejection, WQE resource
  creation, lease acquisition, Jetty enqueue, operation registration, and scheduling keep their
  existing order.
- TPG cursor traversal, selectable-state filtering, exact-generation/active filtering, capacity
  check, lifecycle resource/lease creation, TP commit, Jetty commit, cursor commit, and use-lease
  release keep their existing order.
- Header/tag construction, callback/trace/timer timing, packet bytes, TPNAK/RTO pinning, and modeled
  network behavior are unchanged.

Characterization evidence:

- Existing lifecycle TPG mutation, backpressure, teardown, generation-reuse, and transport
  retransmission fixtures in `src/unified-bus/test/ub-test.cc` remain the behavior oracle.
- A focused T26 counter suite will record the pre-change four WQE resolutions and repeated TPG
  materialization, then require one resolution and one build for an unchanged membership version.
- The archived differential package's `m04` RTP-TPG case provides the bounded current/next runtime
  and semantic seam.

## Semantic Batches

### Batch 1: Prepared WQE resolution

- Add test-only resolution counters at the existing resolver.
- Extend preflight to return its validated resolution.
- Pass that exact result into `TryAdmitRtpWqe` without a second lookup.
- Keep the existing public preflight wrapper for characterization callers.

Stop if the owner-local gap contains a callback/event seam, a caller requires a different
preflight classification, or the existing commit order must change.

### Batch 2: Generation-safe old-vector materialization

- Add a monotonic group membership version.
- Retain one index-aligned transport vector per Jetty binding, following the old
  `m_jettyTpGroup` ownership shape.
- Rebuild only when exact group identity/version differs; erase on bind/destroy.
- Reuse the materialization in `GetJettyRelatedTpVec` and TPG segment admission while retaining
  per-attempt selectable, generation, active, and capacity checks.

Stop if caching requires a new owner model, changes cursor order, admits a stale generation, or
changes no-member/backpressure classification.

### Batch 3: Focused and bounded validation

- Run targeted `unified-bus-test`, `test-runner`, and `ub-quick-example` build serially.
- Run the new T26 resolution/materialization suite, `unified-bus-lifecycle`, and
  `unified-bus-transport-retrans`.
- Run bounded current/next `m04` with frozen inputs, compare accepted semantic artifacts, and report
  host time without asserting a gain.
- Review each modified block against `main@ccf41b58` and the pre-T26 diff before commits.

Stop on an unexplained test/output difference, an unknown current/next behavior difference, or a
change that can affect final byte acceptance.

## Allowed Diff

- `.scratch/unified-bus-lifecycle-performance/T26-task-brief.md`
- `.scratch/unified-bus-lifecycle-performance/T26-report.md`
- `src/unified-bus/model/protocol/ub-function.cc`
- `src/unified-bus/model/protocol/ub-transaction.cc`
- `src/unified-bus/model/protocol/ub-transaction.h`
- `src/unified-bus/model/protocol/ub-transport.cc`
- `src/unified-bus/model/protocol/ub-transport.h`
- `src/unified-bus/test/ub-test.cc`
- focused T26 evidence under `.scratch/unified-bus-lifecycle-performance/evidence/`

## Validation Ledger

- 2026-07-17: ticket acceptance, pre-refactor reuse map, three semantic batches, stop conditions,
  and allowed diff recorded before production edits.
- 2026-07-17: red counter loop confirmed four direct-WQE resolutions before implementation; the
  same first test passed with one resolution after prepared resolution reuse.
- 2026-07-17: focused `unified-bus-rtp-resolution` passes direct, TPG, unchanged-membership,
  add/remove, freeze/resume, and destroy invalidation cases. Repeated materialization builds once.
- 2026-07-17: targeted Unified Bus test and quick-example build, `unified-bus-lifecycle`, and
  `unified-bus-transport-retrans` pass serially.
- 2026-07-17: three serial current/next `m04` pairs both have `0.07 s` median wall and
  `16,678,912 B` median RSS. All shared artifacts are byte-identical except the inherited T05
  same-timestamp `TaskTrace_node_1.tr` order; bytes, lines, and sorted row multiset are identical.
- Implementation commit: `ba5d110ee3f998121d88d6d60ea323d324074323`.
- Reuse review: PASS. The implementation restores the pre-refactor retained-vector shape with
  lifecycle identity/version guards and reuses the current cursor/admission blocks without a new
  builder, parser, state machine, ACK/RTO, queue, scheduler, routing, or congestion algorithm.
