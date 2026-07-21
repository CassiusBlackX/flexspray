# Tickets: Unified Bus Modeled Resource Lifecycle

Status: ready-for-agent

These tickets implement the lifecycle specification in `.scratch/unified-bus-lifecycle/PRD.md` as independently verifiable tracer-bullet slices.

Work the **frontier**: a ticket may start only after every title listed under **Blocked by** is complete. The initial frontier contains only T00.

## Acceptance Protocol

The following contract applies to every ticket:

- T00 freezes and protects exactly one current `main@ccf41b58` reference executable for each incompatible build configuration required by the acceptance matrix, together with hashes, build facts, inputs, complete output manifests, and the comparison harness. No ticket rebuilds a frozen configuration or substitutes a different compile-time superset.
- An ordinary ticket follows the Matt SDD/TDD gate: focused RED/GREEN tests, a targeted build and module tests, and only the smallest necessary scenario. It does not run frozen baseline `verify`/`compare` by default, and byte equality is not a completion gate.
- When an existing active path already implements reusable behavior, extract or copy that implementation and make only the required parameter/state adaptations. Do not rewrite equivalent packet construction, protocol handling, scheduling, parsing, or cleanup logic unless the ticket cannot be completed safely by reuse and the replacement is justified by focused evidence.
- Byte-level milestone gates are T06, T08, T12, T13, T17, T19, T20, and T21. Before each milestone comparison, verify the frozen artifact root, select the exact runtime-matching variant, and compare the applicable acceptance profiles. Exact equality remains the default pass condition; a milestone may also pass with an explicit approved exception for every differing artifact when the cause and impact are known and acceptable.
- A non-milestone ticket escalates to the smallest applicable baseline comparison only when focused tests expose an unexplained user-visible output change or the ticket explicitly changes the artifact contract.
- T00 proves reference self-reproducibility by running each accepted case at least twice. Whenever a milestone or escalated comparison runs, require raw byte equality for outputs proven stable; preserve unstable raw output and compare its documented canonical representation without deleting semantic events or values.
- Use minimal targeted cases for implementation and debugging. Use separate medium-complexity, risk-focused cases at applicable milestones; do not use one large all-features scenario.
- Validate new lifecycle-only output through schema, balance, generation, terminal-outcome, and causal-order invariants because no old counterpart exists.
- Any intentional output difference requires an explicit exception manifest with old bytes, new bytes, first differing offset, exact cause, affected and unaffected behavior, Specification or characterization evidence, and approval. Each exception is narrow to the recorded artifact difference and applies to later milestones only while its cause and evidence remain unchanged. If the cause is uncertain, the impact is unacceptable, or approval is missing, the required or escalated comparison fails and must be brought to the user instead of being waived broadly.

## Batch Implementation Policy

This policy applies to every unfinished Unified Bus lifecycle ticket. At ticket start, create a
ticket-level Reuse Plan against `main@ccf41b58`, then split work into the semantic batches required
by actual behavior and risk. Two to four batches is common, not mandatory. Before editing, record
one Batch Reuse Map for each batch; in-scope mechanical callers may then migrate continuously,
followed by one batch-level build, focused validation, and a separate review pass.

Pause only on a reassessment trigger defined in `docs/agents/implementation-workflow.md`. The root
agent normally implements inline without delegation. Every batch boundary, diff scope, and
validation result must be written to the current task brief so work can resume from the recorded
frontier without depending on chat history.

At ticket start, resolve its complete acceptance list below and the PRD into concrete commands,
scenarios, and expected artifacts in the task brief. Resolve any missing definition before
implementation.
At ticket end, run that ticket-defined full acceptance suite, then create one implementation commit
and one closeout commit. This does not mean every ticket runs repository-wide `./test.py`.

The task brief is a local operational ledger, not long-term authority; it cannot relax this file,
the PRD, or repository workflow. Existing blocking edges, per-ticket acceptance, and milestone
requirements remain authoritative.

## Task 00: Freeze the current-main executable and two-tier acceptance corpus

**What to build:** Create the immutable executable baseline, comparison harness, and bounded case corpus reused by later milestones and escalated review investigations.

**Blocked by:** None - can start immediately.

- [x] Exactly one reference executable is frozen from `main@ccf41b58` for each incompatible accepted build configuration: local single-thread (`local-single`), local MTP (`local-mtp`), and MPI plus MTP (`parallel-all`). Every hash and complete build manifest is recorded for reuse by later milestones and escalated review investigations without rebuilding.
- [x] All case inputs and comparison rules have recorded hashes and cannot silently drift between tickets.
- [x] The minimal corpus contains a two-node, single-RTP-TP, single-operation case suitable for fast diagnosis.
- [x] The medium corpus contains separate bounded cases for shared-TP concurrency, multi-TP plus deterministic loss/retransmission, CTP Entity/VL queue pressure, and a small cross-owner runtime.
- [x] Every generated reference output is generated twice; all proven byte-stable outputs are listed explicitly for raw byte equality, and canonical-only artifact lists are explicit and empty.
- [x] The harness requires the exact managed artifact set on both sides, rejects missing or unclassified artifacts, reports the first differing artifact and byte offset, and preserves both old and new outputs for diagnosis.

## Task 01: Establish a single-TP packet lifecycle tracer bullet

**What to build:** Carry one single-threaded RTP packet through protocol birth, queue and link transfer, endpoint consumption, and one explicit drop without changing the modeled network result.

**Blocked by:** T00 Freeze the current-main executable and two-tier acceptance corpus.

- [x] One RTP DATA attempt is registered exactly once before entering the packet pipeline.
- [x] Queue and local-link transfer preserve the same packet accounting identity and do not change the in-flight count.
- [x] Endpoint consumption and one explicit drop each produce exactly one typed terminal outcome and return the generation balance to zero.
- [x] Underflow, stale generation, and mutation after retirement fail immediately.
- [x] Focused lifecycle tests and the minimal single-thread scenario cover endpoint consumption, explicit drop, generation balance, and the existing task result.

## Task 02: Make WQE submission and completion atomic

**What to build:** Carry one direct-TP WRITE WQE through Function, Transaction, lazy segment creation, TP admission, transaction completion, WQE removal, and upper-layer completion as one atomic ownership chain.

**Blocked by:** T01 Establish a single-TP packet lifecycle tracer bullet.

- [x] Failed admission changes neither the Jetty WQE collection nor the Transaction operation collection.
- [x] Successful admission exposes the WQE, operation context, and transport-use lease atomically.
- [x] Segment creation remains lazy and advances its cursor only after transport admission succeeds.
- [x] The WQE receives one completion, its work and child counts return to zero, and existing completion timing is preserved.
- [x] Focused WQE/Transaction tests plus minimal WRITE and shared-TP targeted scenarios cover atomic admission, lazy progress, one completion, and preserved completion timing.

## Task 03: Complete WQE outcomes and explicit Jetty drain

**What to build:** Extend the WQE chain to READ and response-bearing work, typed failure/cancel/flush outcomes, and upper-layer-requested Jetty destruction that freezes admission and drains accepted work.

**Blocked by:** T02 Make WQE submission and completion atomic.

- [x] WRITE, READ, and response-bearing operations each produce exactly one typed WQE terminal outcome.
- [x] Duplicate and stale Transaction outcomes fail without notifying the application twice.
- [x] A destroy request rejects new WQEs while allowing already accepted response, error, cancel, and flush work to finish.
- [x] Transaction references, tracked events, and RTP/CTP bindings are released before the Jetty collection entry retires.
- [x] A pending destroy callback executes once after erase; synchronous completion does not invoke it.
- [x] Focused outcome and drain tests cover WRITE, READ, response work, failure/cancel/flush, duplicate/stale outcomes, callback ordering, and accepted-work drain.

## Task 04: Resolve direct TP and TPG references through one RTP path

**What to build:** Carry direct-TP and multi-TP operations through one generation-aware RTP submission path with explicit per-segment TPG member selection.

**Blocked by:** T03 Complete WQE outcomes and explicit Jetty drain.

- [x] Direct TP references bypass member selection and use the same final admission path as selected TPG members.
- [x] A single-segment operation resolves one TP; a multi-segment operation can distribute segments across TPG members.
- [x] Every accepted segment, its packets, responses, and retransmissions stay on its resolved TP generation.
- [x] Generation validation, member selection, TP work-lease acquisition, and enqueue are atomic in the RTP owner context.
- [x] Focused direct-TP/TPG tests and a targeted multi-TP scenario cover member selection, generation pinning, atomic admission, and segment distribution.

## Task 05: Preserve backpressure and TPG member lifecycle

**What to build:** Make ready-source scheduling, capacity wakeup, TPG membership changes, and group destruction bounded and event-driven while preserving current observable fairness.

**Blocked by:** T04 Resolve direct TP and TPG references through one RTP path.

- [x] Temporarily full members cause deferred admission without advancing RR, offset, or TASSN state.
- [x] One capacity transition produces at most one pending wakeup per affected admission domain.
- [x] Shared direct and TPG-selected traffic use the same TP capacity, queue, PSN, retransmission, and congestion state.
- [x] Member retirement excludes new selection but never migrates an accepted segment or destroys the member TP.
- [x] Removing the last member cannot deadlock an operation that still holds a TPG-use lease.
- [x] Focused backpressure and member-lifecycle tests plus targeted shared/multi-TP scenarios report fairness, starvation, event rate, and throughput without advancing state on deferred admission.

## Task 06: Introduce a descriptor-backed GBN transmit buffer

**What to build:** Carry GBN initial send, timeout/NAK selection, packet rebuilding, retransmission, acknowledgement, and item retirement through one descriptor-backed transmit buffer.

**Blocked by:** T02 Make WQE submission and completion atomic.

- [x] Sent items retain protocol descriptors and progress state rather than complete wire packets.
- [x] The first-never-sent PSN remains monotonic; retransmission selection uses separate state.
- [x] Every retransmission rebuilds headers and payload through the common RTP builder and registers a new physical packet attempt.
- [x] ACK, operation failure, and cancellation retire the corresponding items and tracked timer state.
- [x] Rebuilt bytes, protocol traces, task results, and throughput match the frozen deterministic-loss GBN baseline byte-for-byte where stable.

## Task 07: Migrate selective retransmission and remove full-packet retention

**What to build:** Carry TPSACK holes, fast retransmit, RTO, selective packet rebuilding, and ACK retirement through the shared descriptor-backed transmit buffer, then remove the old full-packet cache.

**Blocked by:** T06 Introduce a descriptor-backed GBN transmit buffer.

- [x] Missing PSNs enter retransmission selection once and acknowledged stale entries are removed.
- [x] Cumulative ACK and selective acknowledgement retire the correct descriptor ranges.
- [x] Selective retransmission uses the common builder and creates a separately tracked physical attempt.
- [x] No active selective path retains a complete previously sent wire packet.
- [x] Existing selective, wire-wrap, congestion-accounting, queue-priority, and fault cases remain green.
- [x] A targeted selective-loss scenario confirms rebuilt packet/header semantics, completion, and accounting after full-packet cache removal.

## Task 08: Close all RTP packet and tracked-event terminal paths

**What to build:** Extend lifecycle closure across RTP DATA, TPACK, TPNAK, TPSACK, CNP, semantic packet copies, explicit drops, and delayed protocol work.

**Blocked by:** T05 Preserve backpressure and TPG member lifecycle; T07 Migrate selective retransmission and remove full-packet retention.

- [x] Audited production parser views and local/remote ownership transfers use distinct named
  semantics. No retained-template or physical-fork caller exists after T07, so T08 does not invent
  one or restore the removed packet caches.
- [x] Route failure, ingress/egress admission failure, fault drop, channel failure, malformed packet,
  and endpoint consumption produce explicit terminal outcomes. Invalid-endpoint and forced-stop
  handling do not own a proven physical packet attempt at `main@ccf41b58`, so no synthetic terminal
  is added without an old discard seam.
- [x] DATA and every emitted RTP control packet balance at their direct endpoint generation owner.
- [x] Audited RTP retransmission and DCQCN pending-CNP/rate/alpha/pacing events release their tokens
  exactly once on fire or cancel. The merged transaction admission event remains excluded because
  it has no single proven RTP owner and changing it would alter T05 scheduling semantics.
- [x] Lifecycle accounting remains trace-independent, and existing trace artifacts preserve their
  network results under the frozen milestone comparison.
- [x] Minimal fault cases pass and medium retransmission artifacts match the frozen baseline using
  only the inherited, fully evidenced T05/T06 narrow exceptions.

## Task 09: Give TP endpoints a complete manager-owned lifecycle

**What to build:** Move TP runtime collection ownership behind a complete manager contract and carry explicit local endpoint destroy through freeze, drain, finalize, and optional later rematerialization.

**Blocked by:** T08 Close all RTP packet and tracked-event terminal paths.

- [x] Create, resolve, state query, destroy request, drain inspection, private finalization, and forced disposal share one manager-owned record.
- [x] Endpoint destruction waits for bindings, segments, packets, events, retained state, queues, and allocator ownership.
- [x] Finalization unregisters the allocator and erases the endpoint exactly once while leaving the parent reservation and TPN valid.
- [x] Later legitimate demand can create a new endpoint generation under the still-valid reservation.
- [x] Idle alone never invokes endpoint destroy or resets required PSN/congestion continuity.
- [x] The legacy task-driven removal default remains disabled, and enabling it fails with the specified unsupported-teardown error.

Closeout:

- `UbTransportChannelManager` now owns the endpoint collection and reuses the former controller
  create, lookup, reserved materialization, and inbound channel-key validation blocks. Controller
  APIs remain forwarding-compatible.
- Explicit local destroy freezes new segment admission, cancels endpoint-only DCQCN events through
  existing tracked-event helpers, and waits on the canonical lifecycle snapshot plus retained TP
  and Transaction state. The last packet or lease terminal triggers reassessment without polling.
- Finalization preserves the old allocator unregister -> Transaction `TpDeinit` -> collection erase
  order, adds generation retirement before erase, and invokes callbacks only after erase.
- The reservation and TPN remain in `TpConnectionManager`; rematerialization opens a new endpoint
  generation. No connection teardown or paired-slot aggregation from T10 is included.
- DCQCN idle cleanup still cancels recovery events and active-flow accounting but now preserves its
  reduced rate and other congestion scalar continuity. No idle or task-completion destroy caller was
  introduced.
- Focused validation passed: targeted build, lifecycle, TP reservation/materialization,
  transport-retransmission, congestion-control, DCQCN marking, switch-VOQ, and diff check. T09 has
  no frozen byte comparison.

## Task 10: Tear down a paired TP connection only after both slots drain

**What to build:** Carry explicit connection teardown from either endpoint through canonical reservation ownership, two fixed endpoint slots, late-packet drain-only materialization, paired completion, reservation removal, and TPN release.

**Blocked by:** T09 Give TP endpoints a complete manager-owned lifecycle.

- [x] A connection generation owns exactly two endpoint slots with explicit unmaterialized, active, draining, and retired states.
- [x] A valid late packet for the closing generation creates or retains a drain-only endpoint before processing derived terminal or response work.
- [x] New application work is rejected while old DATA, ACK/NAK/SACK/CNP, transaction response, and required error handling can drain.
- [x] Repeated, simultaneous two-sided, and stronger-mode destroy requests share one monotonic drain/finalize flow.
- [x] Reservation indexes and both TPNs are released only after both slots retire; reuse creates a new connection generation.
- [x] Focused paired-teardown tests cover idle/active work, repeated and simultaneous requests, late packets, TPN release, reuse, output outcomes, and callback ordering.

Closeout:

- `TpConnectionManager` retains the old reservation construction and local index orientation while
  adding one source-owned canonical connection record, generation identity, and two fixed endpoint
  slots. Packet ownership remains on the endpoint generation.
- Either endpoint can request teardown. Repeated requests join the same callback list, stronger
  reasons upgrade monotonically, and both local endpoint destroys reuse the T09 manager path.
- The closing reservation remains available to the existing inbound channel-key validator. A valid
  late inbound packet can materialize a drain-only endpoint, which rejects new application work and
  retires through the same endpoint manager after the current handler.
- Exact local views and empty index entries are removed only after both slots retire. Both TPNs are
  then released, the connection identity and canonical record retire, and callbacks run after erase.
  Reuse of the released TPN opens a newer connection generation.
- Review against `main@ccf41b58` found no rewritten packet builder/parser, ACK/RTO, queue,
  scheduler, or congestion-control algorithm and no idle/task-completion teardown caller.
- Focused validation passed: targeted `unified-bus-test`/`test-runner` build,
  `unified-bus-lifecycle`, `unified-bus-tp-reservation`, and diff check. T10 has no frozen byte
  comparison.

## Task 11: Move CTP transaction progress into the Transaction owner

**What to build:** Carry CTP WRITE and READ through the common WQE/Transaction lifecycle while CTP remains a persistent node-scoped transport service.

**Blocked by:** T03 Complete WQE outcomes and explicit Jetty drain.

- [x] Transaction owns TASSN, windows, pending/outstanding segments, inbound units, response matching, and operation completion.
- [x] CTP reports MTU and one-packet constraints without modifying Function or Jetty segmentation state.
- [x] WRITE can use multiple one-packet segments; READ responses and single-packet operations obey their operation-specific limits.
- [x] Segment and WQE outcomes are emitted once and completed Transaction records are removed.
- [x] No CTP path creates an RTP TP reservation, TPN, PSN, transport ACK, retransmission context, or CTP root generation.
- [x] Focused CTP transaction tests and a targeted READ/WRITE scenario cover segmentation constraints, task/packet outcomes, completion, and Transaction cleanup.

Closeout:

- `UbFunction::SubmitCtpWqe` reuses the common WQE and Transaction-operation lifecycle while the
  node-scoped CTP service continues to own packetization, routing, queues, traces, and transport
  policy. CTP admission creates no RTP transport reference, TPN, PSN, ACK/RTO, or retransmission
  state.
- The pre-refactor Jetty segmentation path is parameterized with an MTU ceiling. CTP no longer
  rewrites persistent Jetty configuration, and the old prepare -> window admission -> commit order
  ensures backpressure cannot advance TASSN state.
- Transaction now owns CTP contexts, pending/outstanding segments, inbound units, response matching,
  and lifecycle correlation. Multi-MTU READ and WRITE become multiple one-packet Transaction
  segments; the CTP send seam rejects unsplit oversized segments without consuming window space.
- Normal TAACK/response completion and forced Jetty terminal retire segment, operation, and WQE
  resources once. Forced terminal also removes exact pending/outstanding retention and releases CTP
  window capacity; cacheable key retirement remains T12.
- Review against `main@ccf41b58` found no rewritten packet builder/parser, header/tag order,
  routing, queue, allocator, ACK/RTO, scheduler, or congestion-control algorithm. Two invalid
  standalone-service tests were moved to the production node-owned service and now distinguish
  packet recognition from exact Entity-context completion.
- Focused validation passed: targeted build, 16-case `unified-bus-ctp-transaction`,
  `unified-bus-lifecycle`, diff check, and the 16-task
  `clos_32hosts-4leafs-8spines_ctp-entities` scenario with eight WRITE and eight READ completions.
  T11 has no frozen byte comparison; the CTP byte milestone remains T12.

## Task 12: Make CTP keys, queues, events, and finite children retire independently

**What to build:** Carry CTP packets through canonical Entity/congestion keys, complete-packet transmit queues, allocator registration, endpoint terminal, and independent cleanup without destroying the persistent service.

**Blocked by:** T01 Establish a single-TP packet lifecycle tracer bullet; T11 Move CTP transaction progress into the Transaction owner.

- [x] Entity keys are node-local directed communication keys and congestion keys aggregate only destination node, destination Entity, and VL.
- [x] Sharing and isolation tests cover source Entity, destination node, destination Entity, VL, and reverse-packet normalization.
- [x] Transmit queues contain complete packets and no raw pointer to cacheable Entity or Transaction state.
- [x] DATA, ACK, and CNP priority, packet terminal accounting, queue registration, and tracked events balance independently.
- [x] Completed Transaction state retires independently, while an empty queue shell preserves its
      allocator identity until service disposal.
- [x] Loss without lower-layer recovery remains explicitly unresolved rather than being hidden by forced cleanup.
- [x] At the CTP milestone, the verified frozen baseline byte-matches all applicable CTP task, packet, queue, throughput, and canonical artifacts.

Closeout:

- `UbCtpEntityKey` is local to its node-scoped service and `UbCtpCongestionKey` aggregates only
  destination node, destination Entity, and VL. Existing production parsing characterizes
  source-Entity sharing, destination/VL isolation, and reverse normalization.
- The pre-refactor `BuildCtpPacket` block remains the sole CTP packet builder. `UbCtpTxQueue`
  stores only complete DATA/ACK/CNP packets, preserves CNP > ACK > DATA priority, and retains its
  stable allocator slot after draining. Service disposal unregisters retained queues before
  clearing service ownership.
- The persistent CTP service owns packet accounting without gaining a channel teardown API.
  Complete DATA and ACK packets register before enqueue; local consumption and route failure use
  typed terminals at the existing switch seams.
- Delayed TAACK uses the common tracked-event sequence and a per-key Transaction hold. Completion
  and disposal cancellation each release the token once; cancellation preserves unresolved work.
- `UbTransaction` retires finite CTP records only after operations, segment containers, inbound
  units, delayed events, and window occupancy drain. A compact cursor preserves TASSN continuity,
  while Entity routing and congestion policy may remain cached in the service.
- Focused validation passed: targeted build, 13-case CTP lifecycle, 17-case CTP Transaction,
  lifecycle, switch-VOQ, and flow-control suites. The 16-task multi-Entity READ/WRITE scenario
  completed eight WRITE and eight READ tasks.
- The frozen root verified 369 files. The current `local-single`
  `medium-ctp-entity-vl-pressure` candidate completed 4/4 tasks and compared
  `artifacts=identical`; no exception was required.

## Task 13: Close the LDST operation lifecycle

**What to build:** Carry LOAD and STORE task segments through worker admission, request/response packets, acknowledgement, completion, tracked events, active-record erasure, and forced stop.

**Blocked by:** T01 Establish a single-TP packet lifecycle tracer bullet.

- [x] Task segment, request/response packet, ACK record, and scheduled action each have one owner and terminal path.
- [x] Successful completion removes the segment from every active LDST map and updates compact statistics only where required.
- [x] Unknown, duplicate, and stale ACKs cannot insert records or underflow counters.
- [x] Forced stop reports active LDST residue explicitly and releases scheduled work before disposal.
- [x] Thread selection, address behavior, packet sizes, completion timing, and stable outputs match the frozen baseline.

Closeout:

- The `main@ccf41b58` admission, worker, request/response builder, routing, VOQ, ACK, and completion
  blocks remain the active implementation. Lifecycle changes are limited to canonical records,
  non-inserting ACK lookup, exact retirement, typed packet terminals, tracked actions, and forced-stop
  residue; no packet builder, parser, routing, queue, scheduler, ACK/RTO, or flow-control algorithm
  was rewritten.
- Completion erases ACK, segment, and task records exactly once. Unknown, duplicate, and stale ACKs
  do not mutate state or restore an outstanding slot, and reentrant disposal cannot schedule new LDST
  work after pending actions have been canceled.
- Focused validation passed: targeted build, the two-case LDST lifecycle suite, lifecycle suite, and
  switch-VOQ suite. The two-task multipath MEM_STORE scenario and a separate two-task 4 KiB MEM_LOAD
  scenario completed successfully.
- The frozen root verified 369 files. Two frozen `local-single` reference runs matched each other,
  and the current candidate matched all 31 bounded LDST multipath artifacts byte-for-byte; no
  exception was required.
- An extra repository-wide `--test-name=unified-bus` run remains unaccepted on this macOS host.
  T13 initially attributed its exit 139 to the direct-app invalid-priority child-abort fixture, but
  later removal of that fixture did not remove the crash. The actual umbrella crash point remains
  unlocalized; ticket-defined focused suites are the accepted macOS validation path.

## Task 14: Separate route storage from routing policy on the live packet path

**What to build:** Carry packets through separated route lookup and out-port selection while preserving exact/range routes, port-set sharing, hash, packet spray, adaptive selection, and ingress filtering.

**Blocked by:** T01 Establish a single-TP packet lifecycle tracer bullet.

- [x] Route lookup performs no policy selection and returns a bounded read-only candidate view.
- [x] Hash, shortest/non-shortest, adaptive selection, ingress filtering, and tie-break results match
  current behavior; packet spray uses the user-approved strict RR correction.
- [x] The forwarding hot path does not allocate temporary route or best-port containers per packet.
- [x] Route failure still produces exactly one packet terminal outcome.
- [x] Focused routing characterization tests and targeted route-failure/medium-topology scenarios preserve selection and trace behavior; allocation count and forwarding time are reported.

Closeout:

- `UbRoutingTable` owns the copied exact/range maps, port-set interning, overlap boundaries, and
  copy-out APIs. Hot lookup returns bounded read-only spans and performs no policy selection.
- `UbRoutingPolicy` owns the copied per-flow HASH and ADAPTIVE implementations. Candidate traversal
  preserves shortest-before-non-shortest order, ingress filtering, VOQ+egress load score, and the
  first-minimum tie-break without temporary candidate or best-port vectors.
- Packet spray intentionally uses strict RR as approved during T14: the monotonically increasing
  `sport` salt directly selects the RR step, with no hashed starting offset. Packet-spray Hash64
  calls were removed; other routing semantics are unchanged.
- `UbRoutingProcess` remains the TypeId/configuration/caller facade. Existing switch, packet builder,
  route loader, and TP connection callers retain their API and route-failure terminal seams.
- Target build and the five-case routing, lifecycle, and switch-VOQ suites passed. The measured
  100,000 hot lookups took about 1 ms of suite time with zero route-selection container allocations.
- The two-task multipath scenario completed 2/2 tasks in 1.315601 s of simulation run time and
  1.786822 s case-reported total time. T14 is not a frozen-byte milestone; the strict-RR output
  change is explicit and user-approved.
- A follow-up clarification removed the remaining flow-hash starting offset. Packet spray now uses
  the encoded per-packet RR step directly; exact-order characterization and the five-case routing
  suite pass, while non-spray HASH behavior remains unchanged.

## Task 15: Split Datalink flow control without changing control frames

**What to build:** Carry NONE, CBFC, shared-credit CBFC, and PFC traffic through distinct Datalink owners while preserving switch arbitration, thresholds, credit behavior, pause/resume, and control-packet lifecycle.

**Blocked by:** T01 Establish a single-TP packet lifecycle tracer bullet.

- [x] Base flow-control, CBFC, shared-credit behavior, PFC, and decision hooks retain their existing public configuration and trace behavior.
- [x] Credit, pause, resume, piggyback, and fallback control-frame behavior matches current tests.
- [x] Every Datalink control packet has one persistent port/VL accounting owner and one terminal outcome.
- [x] Switch allocator order and queue ownership do not move into the flow-control modules.
- [x] Focused NONE/CBFC/PFC threshold and control-frame tests plus a targeted queue-pressure scenario preserve arbitration, credit, pause/resume, and trace behavior.

Closeout:

- `UbFlowControl` retains the compatibility event/base surface. The complete inherited CBFC and
  shared-credit blocks now live in `UbCbfc` owners, while PFC decision hooks and the inherited PFC
  block live in `UbPfc` owners; existing TypeIds, configuration, traces and factory behavior remain.
- The moved CBFC/PFC implementation bodies were compared against `main@ccf41b58`. Credit math,
  threshold/status order, pause/resume, piggyback, fallback control-frame builders and callbacks are
  unchanged; no parallel parser, builder, state machine, queue or allocator was added.
- Each valid control frame registers once under a lazy persistent Datalink port/VL owner after type
  validation and before VOQ retention. The peer terminal occurs after the inherited flow-control
  receive callback. Queue/backpressure/channel outcomes retain their existing typed seams.
- Target build and the 35-case flow-control, lifecycle and switch-VOQ suites passed. The linked-port
  characterization proves the packet remains live during the receive callback and balances after it.
- `scratch/2nodes_single-tp_cbfc-credit-large` completed 1/1 task in about 56 ms run time and 158 ms
  total wall time. The 128-cell `credit-small` negative case was stopped at 0/1 task after sustained
  starvation and is explicitly not reported PASS. T15 is not a frozen-byte milestone.

## Task 16: Balance VOQ ownership, allocator events, and queue disposal

**What to build:** Carry packets through switch-owned sparse VOQs and queue-manager buffer accounting, then safely retain or retire empty VOQs through one gated unregister/finalize path.

**Blocked by:** T15 Split Datalink flow control without changing control frames.

- [x] The switch is the only VOQ collection owner; the queue manager owns buffer pools, admission, and occupancy only.
- [x] Queue entry removal, ingress/VOQ/egress occupancy, overflow-storage release, and flow-control views remain balanced.
- [x] A VOQ cannot retire while a packet, in-port event, allocator token, flow-control occupancy, or algorithm lease remains.
- [x] Retaining an empty shell and destroying it use the same safe gate and are selected only by explicit teardown or measured storage policy.
- [x] Allocator and diagnostic events are saved, canceled, and resolved symmetrically; forced disposal records residue.
- [x] Focused sparse-VOQ and allocator tests plus a targeted queue-pressure scenario preserve allocation order, queue traces, TypeId/attributes, and balanced occupancy.

Closeout:

- `UbSwitch` remains the sole sparse VOQ collection owner. `UbIngressQueue` and `UbPacketQueue`
  were extracted from the pre-ticket queue-manager file without redesign; `UbQueueManager` retains
  only admission, buffer pools, occupancy and trace accounting.
- The destruction gate rejects retained packets, delayed in-port work, pending/active allocator
  work and nonzero production accounting, then unregisters and repeats the checks before erasing.
  Ordinary runtime retains drained empty shells; only the explicit teardown/test seam destroys one.
- Allocation, retrigger, port-notification and recurring deadlock events are saved and canceled on
  disposal. Switch and allocator disposal record event, byte, packet, buffer and active-lease
  residue before releasing owned state.
- RR/DWRR selection, cursor/deficit updates, dequeue, flow-control callbacks, accounting, port
  notification and retrigger order were compared with `main@ccf41b58` and remain unchanged. No
  parallel queue, scheduler, parser, packet builder or state machine was introduced.
- Target build passed with `-j2`; the 10-case switch-VOQ, 35-case flow-control and lifecycle suites
  passed. `scratch/2nodes_single-tp_cbfc-credit-large` completed 1/1 task in about 70 ms simulation
  time and 156 ms total wall time. T16 is not a frozen-byte milestone.
- Queue-class debug messages now use the `UbIngressQueue` log component because ns-3 rejects
  duplicate `UbQueueManager` component registration across translation units. This narrow,
  explained diagnostic-filter difference does not affect packet bytes, protocol behavior, traces,
  callback timing or accounting.

## Task 17: Route lifecycle accounting through MTP, MPI, and hybrid owners

**What to build:** Carry remote packet terminal, resource mutation, endpoint-slot state, destroy request, and destroy completion through fixed-size causal control records routed to the owning LP/rank.

**Blocked by:** T10 Tear down a paired TP connection only after both slots drain; T12 Make CTP keys, queues, events, and finite children retire independently; T13 Close the LDST operation lifecycle; T14 Separate route storage from routing policy on the live packet path; T16 Balance VOQ ownership, allocator events, and queue disposal.

- [x] Only the execution owner mutates each resource generation's ledger and protocol container.
- [x] Remote terminal and destroy-completion records are delivered exactly once or rejected as duplicate/stale in diagnostic injection tests.
- [x] Drain-only slot creation is causally visible before terminal, response, or endpoint completion derived from the late packet.
- [x] Raw callbacks, mutable protocol objects, and direct peer-object mutation never cross LP/rank boundaries.
- [x] Unsupported control transport fails before enabling lifecycle functionality that needs remote confirmation.
- [x] Single-thread, MTP, MPI, and hybrid semantic artifacts match according to the frozen acceptance manifest; owner sequence and pending-record balance are reported.

- Fixed causal controls reuse the granted-time-window MPI pending-send, polling and visibility
  paths. Task completion remains on tag 77, packet transfer remains on tag 0, and lifecycle control
  uses tag 78 with rank-first and owner-LP-second dispatch.
- Packet and TP lifecycle controls apply through the canonical owner with per-source sequence,
  duplicate/stale rejection, buffered gap handling, local callback tokens, typed remote completion,
  and two-view cleanup acknowledgement. T10 slot, destroy-reason, TPN reuse and callback-after-erase
  behavior remains the local characterization oracle.
- Targeted lifecycle, TP reservation, causal scheduling, MPI/hybrid control, remote-TP and minimal
  quick-example suites passed. Builds, tests and simulations ran serially with build concurrency
  limited to `-j2`.
- The frozen root verified 369 files. Exact `parallel-all`, `local-mtp`, and `local-single`
  candidates completed the cross-owner case in all four runtime modes. Each comparison had the
  already-reviewed T05 same-timestamp `Sends`/`Completes` ordering change only; four runtime-bound
  exception manifests approved that one byte-offset/hash pair while every other managed artifact
  remained byte-identical.

## Task 18: Publish lifecycle trace schema and parser semantics

**What to build:** Carry lifecycle state, ledger delta, identity, and terminal evidence from typed trace sources through SUMMARY/EVENT/DETAIL sinks, versioned schema metadata, complete-identity sampling, and parser validation.

**Blocked by:** T17 Route lifecycle accounting through MTP, MPI, and hybrid owners.

- [x] Correctness ledger, trace sources, and deep verification remain independent execution planes.
- [x] Summary reads existing counters without creating duplicate event accounting.
- [x] Event/detail records use fixed fields, generation-aware identities, owner sequence, stable numeric enums, and one schema metadata source.
- [x] Detail filtering retains complete selected resource generations or packet identities and never randomly drops events from a retained chain.
- [x] Missing metadata, unknown version/enum, and inconsistent MPI-rank mappings are explicit parser errors.
- [x] Focused OFF/SUMMARY/EVENT/DETAIL runs preserve task completion, throughput, and deterministic protocol outcomes while validating emitted schemas.

- `UbLifecycleCoordinator` now publishes post-mutation typed lifecycle records through one ns-3
  trace source. SUMMARY reads existing snapshots at teardown; EVENT records resource/work changes;
  DETAIL adds complete deterministic resource-generation, lease and packet-attempt chains.
- `LifecycleTraceSchema.json` and rank-scoped variants serialize the C++ numeric registries used by
  fixed-column records. The production lifecycle parser has no duplicate enum table and rejects
  missing metadata, unsupported schemas/enums, non-monotonic owner sequence and rank-registry drift.
- The trace sink directly reuses the pre-refactor `UbUtils` path, file registry, 16 KiB pending
  buffer and teardown flush/close path. Existing trace formats, protocol builders/parsers, state
  machines, ACK/RTO, queue/scheduler, congestion and cleanup algorithms are unchanged.
- Targeted build, lifecycle trace tests, the existing lifecycle suite, parser tests and OpenUSim
  helper/reference tests passed. Serial OFF/SUMMARY/EVENT/DETAIL simple-case runs all completed; task
  statistics and throughput artifacts were byte-identical across levels. T18 is not a frozen-byte
  milestone, so no frozen baseline comparison was run.

## Task 19: Bound trace writer memory, file descriptors, and write overhead

**What to build:** Carry trace records through pre-resolved sink handles, owner-local bounded chunks, pressure flushing, batched writes, and file-descriptor LRU without changing existing artifact names or parser inputs.

**Blocked by:** T18 Publish lifecycle trace schema and parser semantics.

- [x] Default chunks are 64 KiB and are created lazily; no owner/sink holds more than one unfinished chunk.
- [x] Each execution owner enforces the 1 MiB/512 KiB high/low-water budget without a polling timer or unbounded completed-chunk queue.
- [x] Effective open-file count respects the configured 1024 maximum, platform soft limit minus reserved descriptors, and conservative fallback.
- [x] File reopen uses append mode, and open/write/flush/close errors cannot silently lose enabled evidence.
- [x] Existing final filenames and parser inputs remain unchanged.
- [x] Benchmarks report 16/64/256 KiB results, wall time, syscall count, RSS, trace bytes, descriptor pressure, visibility behavior, and MTP scaling.
- [x] At the trace/writer milestone, the verified frozen baseline byte-matches every applicable existing network artifact while writer-specific outputs retain their filename and parser contracts.

- The pre-refactor `UbUtils` trace registry and buffered append path now expose stable sink handles,
  lazy owner-local chunks, synchronous high/low-water pressure flushing, and pinned-write-aware FD
  LRU. Parent creation and append-mode open remain on the old active path; all I/O failures are fatal.
- The configured FD maximum is 1024, bounded by `RLIMIT_NOFILE - 128` with a 256 fallback. Focused
  tests cover lazy allocation, owner capacity, teardown tails, concurrency, two-FD eviction/reopen,
  and isolated parent/open/write/flush/close failures.
- On the same local CTP case and binary, 16/64/256 KiB chunks produced byte-identical 13,942,960-byte
  runlog trees. Total wall times were 393.081/358.437/435.237 ms; writes were 885/252/205; only the
  256 KiB setting caused pressure flushes (23). The measured default remains 64 KiB.
- The exact local-MTP case completed with two threads, 10 writes/flushes, no pressure flush or LRU
  close, 10 peak descriptors, 1,050 peak buffered bytes, and 18,006,016-byte max RSS. Focused writer,
  lifecycle trace, lifecycle ledger, OpenUSim helper, and frozen harness suites passed serially.
- Frozen comparison passed with two runtime-bound narrow exceptions: the local CTP tree contains only
  T14's user-approved strict-RR routing differences, proven identical to a pre-T19 control tree; the
  local-MTP tree contains only the T05/T17-reviewed same-timestamp task-trace order. Every managed
  artifact and hash is accounted for, and existing filenames/parser inputs are unchanged.

## Task 20: Contract compatibility layers after all owners migrate

**What to build:** Complete the expand-contract migration by removing obsolete ownership, dead mirrors, and no-longer-needed compatibility forms only after every vertical slice has moved to the canonical owner and interface.

**Blocked by:** T05 Preserve backpressure and TPG member lifecycle; T07 Migrate selective retransmission and remove full-packet retention; T10 Tear down a paired TP connection only after both slots drain; T12 Make CTP keys, queues, events, and finite children retire independently; T13 Close the LDST operation lifecycle; T14 Separate route storage from routing policy on the live packet path; T16 Balance VOQ ownership, allocator events, and queue disposal; T18 Publish lifecycle trace schema and parser semantics.

- [x] Every migrated collection has one owner and no old caller retains a mutable mirror or erase path.
- [x] Compatibility forwarding and aliases remain only where an explicit public compatibility decision requires them.
- [x] Protocol headers, value types, managers, queues, routing, and flow-control modules reflect their canonical domain ownership.
- [x] Pure movement/renaming is separated from behavior changes in reviewable commits.
- [x] Build, module tests, minimal cases, and all applicable byte-stable medium-case outputs remain green.

- Removed proven-dead `UbController` collections, migrated TP callers directly to the canonical
  `UbTransportChannelManager`, and migrated route-storage callers directly to `UbRoutingTable`.
  The receive path retains `UbRoutingProcess::GetOutPort`; documented compatibility contracts remain.
- Independent review against `main@ccf41b58` found no rewritten builder/parser/state machine,
  ACK/RTO, queue/scheduler, congestion, callback/timer, or cleanup algorithm, and no changed caller
  arguments, state-update order, header/tag order, trace timing, or lifecycle semantics.
- Targeted local-single and local-MTP builds passed with `-j2`. Lifecycle, TP reservation, routing,
  flow-control, switch-VOQ, transport-retransmission, and quick-example suites passed serially, as
  did the 33-case frozen-baseline harness.
- Shared-TP milestone output differs only in two TaskTrace artifacts due to the reviewed T05
  same-timestamp Sends/Completes ordering; its other 23 managed artifacts are byte-identical.
  Retransmission, CTP pressure, local-single cross-owner, and local-MTP cross-owner outputs exactly
  match their previously approved candidate trees under narrow runtime/path/hash-bound manifests.

## Task 21: Run the full semantic and performance acceptance matrix

**What to build:** Execute the complete validation matrix, classify every result, and decide storage/default policies from evidence without enabling automatic close or destroy prematurely.

**Blocked by:** T17 Route lifecycle accounting through MTP, MPI, and hybrid owners; T19 Bound trace writer memory, file descriptors, and write overhead; T20 Contract compatibility layers after all owners migrate.

- [x] RTP, CTP, LDST, WRITE, READ, response work, loss/drop, GBN/selective, CAQM/DCQCN, NONE/PFC/CBFC, direct/TPG, teardown, late reuse, lifecycle races, and forced stop all have required evidence.
- [x] Single-thread, MTP, MPI, and hybrid comparisons include owner-local balance and deterministic/canonical artifact checks.
- [x] Every reference artifact classified as byte-stable is byte-identical; every canonical-only artifact has matching canonical bytes and preserved raw evidence.
- [x] Every mismatch is either fixed or listed in the approved exception manifest with Specification or characterization evidence.
- [x] Accounting, deep verification, detail trace, writer settings, and storage policies are measured separately for wall time, events, objects/allocations, peak RSS, trace bytes, syscalls, descriptors, and scaling.
- [x] Automatic TP close/destroy remains disabled; any storage-retention default is selected only from the reported measurements.

- Published the final semantic, runtime, frozen-artifact, performance and storage-policy matrix in
  `docs/superpowers/specs/2026-07-16-unified-bus-lifecycle-acceptance.md`. Ticket-defined focused
  suites, 38 lifecycle harness tests and production parser checks pass serially.
- Current local-single ran all five bounded T00 cases. Current MPI-single and hybrid cross-owner
  trees are byte-identical to their accepted T17 trees; T20 already supplies the exact current
  local-MTP tree. All finite packet/work/event balances return to zero. Persistent binding leases
  remain by design for retained idle endpoints.
- The new minimal comparison has 18/19 byte-identical artifacts. Its sole TaskTrace difference is
  the already reviewed T05 same-timestamp Sends/Completes order and is bound by a T21 manifest.
  Every medium difference remains restricted by its runtime/path/hash-bound T20/T17 manifest.
- OFF/SUMMARY/EVENT/DETAIL produce identical network outputs and bounded trace/writer resources.
  Existing evidence retains 64 KiB chunks and current watermarks/FD cap. Automatic TP close,
  task-driven removal and idle/RSS-driven reclamation remain disabled; ordinary empty VOQs and idle
  TP endpoints remain retained pending representative long-workload evidence.
- Correctness accounting remains mandatory and has no valid off mode. No separate production deep
  verification toggle exists; its stale/underflow/duplicate/drain cost is classified through focused
  invariant tests rather than reported as a fictitious runtime on/off comparison.
- Non-gating residual: the extra umbrella `unified-bus` suite exits 139 on this macOS host. Removing
  the direct-app invalid-priority child-abort fixture does not remove the crash, so that fixture is
  not the root cause. Ticket-defined lifecycle/CTP focused suites remain the macOS acceptance path;
  the umbrella crash requires separate localization.

## Task 22: Close lifecycle review gaps

**What to build:** Resolve confirmed post-acceptance review gaps at existing public seams: freeze
draining Jetty rebind before side effects, validate lifecycle trace continuity and file ownership,
preserve lease-release causal ordering under reentrant retirement, and close CTP allocator queue
ownership during direct disposal. Verify the disputed pre-run owner-control source path and change it
only if the existing tracer bullet reproduces the failure.

**Blocked by:** T21 Run the full semantic and performance acceptance matrix.

- [x] Cross-owner initialization control is either proven valid at the supported seam or routes an
      explicit caller-known source without relying on ambient simulator context.
- [x] Direct-TP and TPG rebind of a draining Jetty return a typed rejection before group, reference,
      cursor, or lease mutation.
- [x] Lifecycle parser rejects missing/header-only trace evidence, sequence gaps, filename/content
      owner mismatch, and metadata filename/content rank mismatch.
- [x] DETAIL evidence records `LEASE_RELEASE` after ledger mutation but before callback-driven
      resource retirement.
- [x] Direct CTP disposal unregisters retained allocator sources, clears service queue ownership,
      and preserves explicit unresolved packet residue.
- [x] Targeted build and ticket-defined focused suites pass serially with `-j2`; the requested
      supplemental T22 frozen-byte comparison against GitCode main has no unexplained difference.

Closeout:

- Pre-run local-MTP control reproduced the ambient-context abort while carrying a valid explicit
  source. `RouteControl` now preserves that source, every TP control builder supplies its known local
  node, and both scheduled and pre-run tracer modes apply one terminal with zero pending/balance.
- Draining direct-TP and TPG rebind now reuse the existing Jetty admission query before the old
  binding branches. Rejection preserves the exact reference and does not advance group/cursor/lease
  state; successful binding order and selection algorithms are unchanged.
- The production parser now rejects missing/header-only evidence, sequence gaps, record/file owner
  mismatch and metadata filename/content rank mismatch while retaining the two-argument direct parse
  interface and C++-owned enum registry.
- Lease release evidence now follows the ledger mutation and precedes its reentrant callback. Direct
  CTP disposal unregisters retained queues from a live allocator before clearing service ownership
  and leaves retained packet work explicitly unresolved.
- Target build, 8 parser tests, 3 focused trace tests, lifecycle, CTP lifecycle and parallel lifecycle
  suites passed serially. Both local-MTP tracer modes passed.
- The requested supplemental frozen comparison used candidate `94de7ef` and the
  `medium-ctp-entity-vl-pressure` case. GitCode `origin/main` resolves to `ccf41b58`, exactly the
  frozen source commit, and the immutable root verifies 369 files. All 44 managed artifacts are
  accounted for: five inputs are byte-identical and the other 39 exactly match the accepted T20/T21
  candidate hashes and offsets for the user-approved T14 strict-RR change. The path-bound T22
  manifest passes with `artifacts=approved-exceptions`; there is no new or unexplained difference.
- Independent Standards/Spec review against `main@ccf41b58` and a final root diff review found no
  parallel builder/parser registry/state machine, ACK/RTO, queue scheduler or congestion algorithm;
  no further lifecycle-review findings remain. The direct-app invalid-priority child-abort test was
  removed at the user's request without changing production validation. The umbrella suite still
  exits 139 elsewhere on macOS and remains excluded from the acceptance claim.

The performance follow-up T23-T28 implements
`.scratch/unified-bus-lifecycle-performance/PRD.md`. It preserves the completed T00-T22 lifecycle
semantics while reducing host observability, bookkeeping, and retained-state cost. Work remains
serial: the current ticket needs implementation and closeout commits before another frontier ticket
starts.

## Task 23: Make minimal lifecycle observability fast by default

**What to build:** Make the minimal observability preset omit lifecycle trace output while balanced,
detailed, custom, and explicit lifecycle levels retain their documented evidence. When SUMMARY is
selected, carry the same records through the existing bounded writer with less lifecycle-layer
dispatch overhead and no artifact-contract change.

**Blocked by:** None — can start immediately.

- [x] Minimal resolves to lifecycle OFF, balanced to SUMMARY, detailed to EVENT, and every explicit
      lifecycle-level override remains authoritative.
- [x] Explicit SUMMARY preserves schema metadata, filenames, fixed fields, record order, parser
      inputs, and complete output bytes.
- [x] The existing bounded trace writer, chunk policy, pressure behavior, and descriptor limits are
      reused without a second buffering or I/O system.
- [x] OFF and SUMMARY runs preserve task results, throughput, protocol traces, completion, and
      simulated time.
- [x] The production observability generator, reference documentation, and documentation tests agree
      with the revised preset contract.
- [x] A frozen current/main/next p02 comparison uses one warm-up and five counterbalanced measured
      pairs, runs serially, and records wall, CPU, RSS, I/O, record count, bytes, and binary hashes.
- [x] Default p02 wall regression is no more than 10 percent, or the ticket stops with a measured
      unexplained blocker instead of widening scope.

Closeout:

- Minimal now defaults lifecycle output to OFF while balanced remains SUMMARY, detailed remains
  EVENT, explicit overrides remain authoritative, and correctness accounting stays always enabled.
- Explicit SUMMARY reuses the T19 bounded `UbUtils` writer. The former `Emit` sink/header block is
  extracted once, the old caller still uses it, and coordinator SUMMARY batches acquire the lifecycle
  mutex and resolve the owner sink once without changing record construction or order.
- A sequential-versus-batch characterization and a frozen pre/post simple scenario prove complete
  SUMMARY schema and trace bytes are identical. Default OFF and explicit SUMMARY preserve task,
  throughput, and non-lifecycle trace bytes; parser validation passes.
- Serial p02 V5 uses one warm-up and five counterbalanced rounds per frozen main/current/next binary.
  Current is `+6.02%` paired-median wall and `+8.78%` RSS versus main, and `+1.46%` wall /
  `+6.61%` RSS versus next,
  within the 10 percent thresholds. Explicit SUMMARY remains an opt-in `+28.71%` wall cost.
- Main/current/next p02 outputs are byte-identical. The only protocol-trace differences are the
  inherited T05 same-timestamp send/complete order in eight TaskTrace files; row multisets,
  timestamps, values, task completion, and derived output are identical.
- Focused build, lifecycle trace suite, lifecycle parser, OpenUSim helper/docs tests, simple scenario,
  SUMMARY byte milestone, performance gate, Standards review, and Spec review pass. T24 has not
  started.

## Task 24: Reduce steady-state packet lifecycle bookkeeping

**What to build:** Carry packet attempts and leases through the same exact identity, transfer,
terminal, generation, and drain invariants with lower steady-state host cost, selecting the concrete
change from a current packet-heavy profile rather than from code inspection alone.

**Blocked by:** T23 Make minimal lifecycle observability fast by default.

- [x] The current p01 no-spray, lifecycle-OFF case is a reproducible red performance loop with one
      warm-up and five counterbalanced measured pairs.
- [x] A bounded profile or site counters identify the selected packet-attempt, lease-container, owner
      resolution, or ACTIVE-endpoint notification hotspot before implementation.
- [x] Exact packet membership remains available for duplicate birth/terminal, underflow,
      out-of-order terminal, stale-generation, and drain validation; it is not replaced by a counter.
- [x] ACTIVE TP steady state performs no destroy-only callback work; DRAINING installation and the
      immediate drain check cannot miss a completion edge.
- [x] Existing production packet tags, queue/link transfers, endpoint/drop terminals, and lifecycle
      characterization remain the test oracle.
- [x] p01 task, throughput, and protocol artifacts retain their accepted bytes and wall regression is
      no more than 10 percent, or the measured blocker is reported without speculative redesign.

Closeout:

- The p01 red loop used one warm-up and five strictly serial counterbalanced current/main pairs with
  packet spray disabled and lifecycle output OFF. The pre-change paired-median wall regression was
  `+20.19%`, with `+15.63%` RSS.
- A bounded profile selected the packet transfer chain, exact-attempt membership allocation, and
  owner/resource lookup. Exact attempt membership now packs 64 identities per map node; only the
  last canonical resource lookup is reused, with owner, generation, and retirement checks retained
  on every mutation.
- TP lifecycle notification is installed only on the first ACTIVE-to-DRAINING transition, before
  freeze and the immediate drain check. Existing TP creation, transaction, allocator, ACK/RTO,
  queue, routing, congestion, callback, and trace ordering remains unchanged.
- Targeted build and `unified-bus-lifecycle` pass. All p01 semantic artifacts are byte-identical;
  there are no unexplained differences.
- Final paired-median wall regression is `+16.65%` and RSS regression is `+15.29%`. This is an
  improvement but does not meet the 10 percent investigation threshold. T24 therefore closes with
  a measured performance blocker under its explicit stop rule instead of widening scope.

## Task 25: Compact retired lifecycle resource storage

**What to build:** Release heavy active containers after a resource generation reaches a valid
terminal retirement while preserving compact generation history, stale-use rejection, optional
final evidence, and the distinction between host storage reclamation and modeled logical closure.

**Blocked by:** T24 Reduce steady-state packet lifecycle bookkeeping.

- [x] A representative sequential task-churn case measures resource count, retained state, peak RSS,
      and growth before choosing the compact layout.
- [x] Retired generations release active attempt, lease, callback, and other active-only storage only
      after balance and terminal invariants pass.
- [x] Local-ID reuse increments generation, rejects late stale mutation, and cannot alias an active
      replacement generation.
- [x] Explicit SUMMARY retains the same final values and bytes; OFF does not retain evidence-only
      state.
- [x] Forced stop and unresolved work remain visible and cannot be compacted into successful
      completion.
- [x] The sequential-churn and p02 trace-OFF RSS results are bounded and reported with absolute and
      percentage values without introducing automatic TP teardown.

Closeout:

- Retired resource generations now release one active allocation containing the snapshot, exact
  attempt membership, leases, callback, counters, and sequence. Compact generation and terminal
  outcome history remains in the canonical resource slot.
- With lifecycle output OFF, 100,000 sequential retired resources retain zero active states and zero
  final summaries. Focused peak RSS falls from `59,441,152 B` to `28,491,776 B` (`-52.07%`).
- Non-OFF levels retain a final snapshot. The frozen pre/post explicit-SUMMARY output and runlog
  trees are completely byte-identical.
- Local-ID reuse increments generation and reuses the same canonical slot; stale and retired
  mutations remain fail-fast. Failed, canceled, flushed, forced-stop, and unresolved-work tests pass.
- p02 OFF is effectively flat: paired-median wall `-1.87%`, median RSS `63,356,928 B ->
  63,324,160 B` (`-0.05%`). The large benefit is specific to long distinct-resource churn; no
  automatic TP cleanup was introduced or claimed.

## Task 26: Reuse validated RTP transport resolution

**What to build:** Resolve each RTP direct-TP or TPG transport reference once per admission attempt
and reuse unchanged TPG membership materialization behind generation and membership-version checks,
without changing which TP admits, schedules, sends, retransmits, or drains the segment.

**Blocked by:** None — can start immediately.

- [x] Direct TP and TPG preflight consume an already validated generation-aware resolution instead of
      repeating the same lookup.
- [x] TPG member materialization is reused only while generation and membership version match, and is
      invalidated by bind, member add/remove, drain, and destroy changes.
- [x] No eligible member, draining member, shared direct-plus-TPG use, and member-selection ordering
      preserve existing outcomes and side-effect order.
- [x] First send, TPNAK, and RTO retransmission remain pinned to the admitted resolved TP generation.
- [x] Focused counters show fewer resolution/materialization operations on the exercised RTP path,
      and a bounded current/next scenario reports the host-time result without requiring a gain.

Closeout:

- Direct and TPG WQE submission now pass one generation-aware resolution from preflight into commit,
  reducing the focused count from four resolver calls to one without changing rejection mapping or
  commit order.
- Binding-owned TPG materialization restores the pre-refactor retained-vector shape. Exact group
  identity plus membership version guard reuse; add/remove, freeze/resume, destroy, bind, and TP
  deinit invalidate it. Segment admission retains exact generation/active checks and group indices.
- The focused counter suite passes direct, TPG, unchanged membership, mutation, drain, and destroy
  cases. Lifecycle and transport retransmission suites pass; resolved generation pinning remains the
  first-send, TPNAK, and RTO source of truth.
- Three serial `m04` pairs are flat at `0.07 s` median wall and `16,678,912 B` median RSS for both
  T26 and next. All shared semantics are byte-identical except the inherited, row-multiset-identical
  T05 same-timestamp TaskTrace ordering difference; no gain is claimed and no difference is hidden.

## Task 27: Avoid redundant selective-retransmission window scans

**What to build:** Preserve the current selective retransmission state machine while allowing an
already-running retransmission timer to bypass an outstanding-window scan that cannot change timer
state.

**Blocked by:** None — can start immediately.

- [x] A large-window selective-retransmission characterization proves the current repeated scan and
      records calls, scanned entries, timer state, and host time.
- [x] The timer-running fast path does not scan retained PSN state and does not schedule, cancel, or
      reset an event.
- [x] GBN behavior is unchanged.
- [x] SELECTIVE ACK, SACK, timeout, fast retry, descriptor retirement, retry set, and completion retain
      existing ordering and outputs.
- [x] Focused retransmission tests and a bounded fault scenario pass without changing packet builders,
      parsers, ACK/RTO policy, or queue scheduling.

Closeout:

- Restored the `main@ccf41b58` timer-first guard while retaining the descriptor-era empty-state
  check. Running timers bypass outstanding state; expired timers still require live GBN/SELECTIVE
  descriptors before the unchanged scheduler runs.
- A 4,096-descriptor, 1,000,000-check fixture reduces SELECTIVE queries and visited entries from
  1,000,001 to 1. Schedule remains 1 and cancel/restart remain 0; the GBN empty/start/running control
  passes.
- Five counterbalanced focused loops improve median time from 5,262 us to 4,305 us (`-18.19%`). The
  old live-map invariant usually visited one entry per query, so no full-window-per-packet claim is
  made.
- Targeted build, focused timer scan, complete transport retransmission, lifecycle, and the existing
  selective-loss MarkPSN scenario pass serially. The scenario's complete pre/post trees are
  byte-identical; its one-shot host timing is noisy and no scenario-level gain is claimed.
- Independent review found no ACK/TPSACK/RTO, retry, descriptor retirement, builder/parser,
  queue/scheduler, congestion, callback/trace, lifecycle tracked-event, or completion change.

## Task 28: Accept current lifecycle performance against main and next

**What to build:** Produce a portable final semantic and host-performance report for the optimized
current branch against frozen GitCode main and pinned next binaries, keeping negative, flat,
exception, and inconclusive results visible.

**Blocked by:** T23 Make minimal lifecycle observability fast by default; T24 Reduce steady-state
packet lifecycle bookkeeping; T25 Compact retired lifecycle resource storage; T26 Reuse validated
RTP transport resolution; T27 Avoid redundant selective-retransmission window scans.

- [x] Frozen binaries, revisions, compatible build configurations, compiler/runtime facts, hashes,
      inputs, and commands are recorded and reused serially.
- [x] p01 and p02 each use one warm-up and five counterbalanced measured pairs and report wall, user
      CPU, system CPU, peak RSS, page faults, file I/O, event/resource counts, and trace volume.
- [x] Complex CTP and RTP control scenarios preserve the accepted completion, window, retransmission,
      selection, queue, and lifecycle-balance behavior.
- [x] Shared byte-stable semantic artifacts are exact; every accepted difference is narrow,
      explained, revision/runtime/case/path/hash-bound, and user-approved where required.
- [x] Unexplained differences and performance regressions above the 10 percent investigation threshold
      remain failures or inconclusive results rather than being hidden by aggregation.
- [x] Automatic TP cleanup remains excluded from the measured benefit and from the implementation.

Closeout:

- Froze and reused main `ccf41b58`, current `85c61e8`, and next `cf8ee683` release binaries and
  Unified Bus libraries. The portable manifest binds revisions, executable/library/input hashes,
  build/runtime facts, commands, archived complex-tree digests, and raw-summary digests.
- p01/p02 used one warm-up and five strictly serial counterbalanced three-role rounds. p01 current
  versus main fails at `+16.64%` paired wall and `+14.86%` paired RSS; current versus next passes at
  `+7.25%` and `+8.42%`. p02 passes against main (`+5.46%`, `+7.83%`) and next (`+2.04%`, `+5.85%`).
- Explicit p02 SUMMARY adds 68,816 records for 3,128 resource generations and costs `+27.11%` wall
  and `+1.86%` RSS versus OFF. Shared task, throughput, and trace semantics remain exact.
- Complex CTP windows 1/4/128 exactly reproduce next and visibly exercise Jetty backpressure. RTP
  shared-window, GBN double-loss, and TPG pinning reproduce the exact archived T05/T06/T14 path and
  hash boundaries; production lifecycle parser validation passes with no new difference.
- Final semantic status is PASS. Final performance status is FAIL because the packet-heavy main gate
  remains above 10%; T24-to-final wall is effectively flat (`+16.65% -> +16.64%`). The failure is
  retained rather than averaged with passing p02/next results. Automatic TP cleanup remains excluded.

## Task 29: Amortize allocator event reclamation

**What to build:** Preserve exact switch-allocator event ownership, VOQ destroy gating, pending-event
queries, forced-stop residue, and cancellation while removing repeated full-vector pruning from the
packet-heavy allocator callback path.

**Blocked by:** T28 Accept current lifecycle performance against main and next.

- [x] A current profile and serial p01 scale loop identify resolved-event pruning as a measured hot
      path before implementation.
- [x] Every allocation, retrigger, port-notification, and deadlock event remains tracked while
      pending and cancelable during allocator disposal.
- [x] Resolved records are reclaimed in bounded batches or at exact query/dispose boundaries without
      changing event timestamps, callback targets, RR/DWRR selection, queue order, or flow control.
- [x] Existing VOQ destroy-gate, forced-stop residue, allocator-ordering, and lifecycle suites pass.
- [x] One warm-up and five counterbalanced pre-T29/T29 p01 pairs report wall, CPU, RSS, and exact task
      completion; a gain below 5 percent remains visible and does not authorize scheduler redesign.

Closeout:

- Tested two lifecycle-only reclamation variants against the frozen `db335a0` executable and
  Unified Bus library: 64-event batch pruning and O(1) generation-token slot reuse.
- Both variants kept all five p01 semantic comparisons exact. The generation-slot variant also
  passed the targeted build, `unified-bus-switch-voq`, and `unified-bus-lifecycle`.
- Batch pruning regressed paired-median wall by `+0.77%`; generation slots improved it by only
  `-0.23%`, with flat user CPU and `+0.08%` RSS. Neither met the 5 percent useful-gain gate.
- Rejected both variants and restored allocator source and tests exactly to pre-T29. The final branch
  therefore preserves all existing event ownership and allocator behavior without carrying added
  token/wrapper complexity. The next performance ticket must target a different measured hot path.

## Task 30: Skip hot coordinator RTTI lookup

**What to build:** Preserve packet identity, execution-owner routing, coordinator validation,
transfer/terminal ordering, and cross-process behavior while using the existing TypeId-based ns-3
aggregate lookup in the packet lifecycle coordinator hot path.

**Blocked by:** T29 Amortize allocator event reclamation.

- [x] The current p01 profile identifies coordinator resolution and RTTI lookup inside the packet
      transfer chain before implementation.
- [x] `UbLifecycleCoordinator::Resolve` retains the same node-range and missing-coordinator aborts
      and resolves the same node-scoped object without adding a registry or cache.
- [x] Existing packet lifecycle, cross-owner routing, queue transfer, and terminal behavior remain
      unchanged under focused lifecycle tests.
- [x] One targeted build and the ticket-defined focused suites pass serially.
- [x] One warm-up and five counterbalanced pre-T30/T30 p01 pairs preserve exact semantic artifacts;
      retain the one-line change only if paired-median wall improves by at least 1 percent, at least
      four pairs do not regress, and paired-median CPU/RSS do not regress by more than 1 percent.

Closeout:

- Reused the existing TypeId-based ns-3 aggregate lookup to remove the speculative RTTI cast from
  coordinator resolution; no registry, cache, state, public API, or lifecycle behavior was added.
- Targeted build, lifecycle, and VOQ suites passed. All five p01 task/throughput/trace artifact sets
  were exact.
- Paired-median wall was `-0.82%`, only three of five pairs did not regress, user CPU was flat, and
  RSS was `+0.33%`. The variant failed its predefined 1 percent/four-pair retention gate.
- Reverted the one-line variant. T30 closes with no source change and narrows the next optimization
  to eliminating duplicated execution-owner/node resolution as one measured semantic batch.

## Task 31: Fuse packet execution-owner and coordinator resolution

**What to build:** Preserve exact packet lifecycle owner routing while reusing the node resolved by
the execution-owner check for local coordinator mutation, avoiding a second range check and node
lookup on every transfer and terminal.

**Blocked by:** T30 Skip hot coordinator RTTI lookup.

- [x] Extract the existing execution-owner and coordinator-resolution bodies into private/internal
      helpers, migrate their old callers first, and prove existing behavior before packet routing uses
      them.
- [x] Local transfer/terminal paths perform one node-range check and one `NodeList::GetNode`; remote
      MPI/MTP paths retain their existing owner classification, source context, and control routing.
- [x] Unknown-node, missing-coordinator, inactive-attempt, stale-generation, and remote-owner behavior
      retain their existing errors and ordering.
- [x] Targeted build, lifecycle, and VOQ focused suites pass serially.
- [x] One warm-up and five counterbalanced pre-T31/T31 p01 pairs preserve exact semantic artifacts;
      retain the change only if paired-median wall improves by at least 1.5 percent, at least four
      pairs do not regress, and paired-median CPU/RSS do not regress by more than 1 percent.

Closeout:

- Extracted and reused the exact current execution-owner and coordinator-resolution bodies. The old
  callers passed focused validation before packet transfer/terminal used the helpers.
- Targeted builds, lifecycle, and VOQ suites passed; all five p01 task/throughput/trace artifact sets
  were exact.
- Every T31 wall pair regressed: paired median `+0.50%`; user CPU `+1.54%`; RSS flat. The treatment
  failed its predefined performance gate.
- Reverted T31 and rebuilt the baseline dylib, reproducing exact SHA-256 `0968fced...32878`. Together
  with T30 this is the second failed owner-resolution variant, so that optimization line is stopped
  pending new evidence from a different hot path.

## Task 32: Remove packet transfer evidence work when lifecycle trace is off

**What to build:** Preserve packet lifecycle correctness while making the default OFF trace level
skip packet-transfer evidence collection before packet-tag parsing and skip trace-owner preparation
before its mutex.

**Blocked by:** T31 Fuse packet execution-owner and coordinator resolution.

- [x] OFF preserves packet birth identity, active-attempt and in-flight balance, typed terminal
      cleanup, generation and stale-terminal rejection, retirement, leases, callbacks, and all
      packet/protocol bytes while omitting transfer evidence counters and transfer-order evidence.
- [x] SUMMARY, EVENT, and DETAIL preserve the existing packet transfer tag parsing, owner routing,
      active-attempt validation, counters, causal sequence, selection, and emitted bytes.
- [x] Existing queue, link, terminal, lifecycle summary, and packet-accounting characterization
      passes with explicit trace levels; no packet builder, parser, queue, scheduler, ACK/RTO, CTP,
      RTP, or parallel terminal-control algorithm changes.
- [x] Targeted Unified Bus build and lifecycle/VOQ suites pass serially.
- [x] One warm-up and five counterbalanced pre-T32/T32 p01 OFF pairs preserve exact task,
      throughput, and protocol artifacts and report wall, CPU, and RSS. Retain the change when it
      does not regress paired-median wall or correctness; the measured gain remains visible even if
      below earlier speculative-optimization gates.
- [x] Explicit SUMMARY output is byte-identical to the frozen pre-T32 SUMMARY output. The OFF-only
      absence of transfer evidence is the narrow intentional exception and must not alter the
      normal OFF artifact set.

Closeout:

- OFF now returns before packet lifecycle tag parsing, owner routing, active-attempt lookup,
  transfer evidence mutation, and transfer-only parallel control. Packet birth, terminal balance,
  generation, leases, cleanup, retirement, and callbacks remain active.
- Trace-owner setup also returns before its mutex under OFF. Non-OFF transfer observation and sink
  setup remain the existing implementation.
- Targeted Unified Bus build, `unified-bus-lifecycle`, and `unified-bus-switch-voq` pass. The
  intended RED was masked by a stale no-suffix test dylib containing an earlier experimental fast
  path; final test and production libraries were rebuilt from source before GREEN.
- Five counterbalanced p01 OFF pairs preserve exact task, throughput, and protocol artifacts.
  Paired-median wall improves `-0.97%`, user CPU `-1.64%`, and RSS `-0.08%`; three of five
  individual wall pairs do not regress, so noise remains explicit.
- Explicit SUMMARY managed artifacts are fully byte-identical, including the schema and 16 node
  lifecycle trace files. No before-only, after-only, or changed artifact exists.
- Review found no remaining ungated record construction or packet/resource selection. Startup-only
  configuration work and public active-snapshot counters remain separate candidates because they
  do not share this hot-path semantic boundary.

## Task 33: Skip unused lifecycle trace setup when output is off

**What to build:** Preserve trace-state reset and every enabled trace artifact while avoiding
detail sampling reads, rank resolution, sink construction, and metadata emission when the resolved
lifecycle trace level is OFF.

**Blocked by:** T32 Remove packet transfer evidence work when lifecycle trace is off.

- [x] OFF configures canonical empty lifecycle trace state without reading unused detail sampling
      values, resolving rank scope, constructing a sink resolver, or entering metadata emission.
- [x] SUMMARY, EVENT, and DETAIL preserve current level resolution, detail validation, rank-scoped
      filenames, sink behavior, metadata, and record bytes.
- [x] Targeted Unified Bus build and lifecycle focused validation pass serially.
- [x] One bounded OFF p01 run preserves the managed artifact set and an explicit SUMMARY run remains
      byte-identical to T32.

Closeout:

- OFF now resets lifecycle trace state with canonical empty configuration, then returns before
  unused detail sampling reads, MPI rank resolution, sink construction, and metadata emission.
- Trace directory preparation, runtime-drop reset, queue-sampler reset, and setup timestamp retain
  their old ordering. SUMMARY, EVENT, and DETAIL retain the previous setup block.
- Targeted Unified Bus build and `unified-bus-lifecycle` pass. Bounded p01 OFF and explicit SUMMARY
  managed artifact trees are both byte-identical to T32 with no missing, added, or changed files.
- This is deterministic once-per-simulation cleanup, so no repeated host-performance gain is
  claimed. Packet lifecycle state, public snapshots, parallel control, and protocol paths are
  untouched.

## Task 34: Calibrate lifecycle OFF performance root causes

**What to build:** Establish a corrected, counterbalanced host-performance baseline against
`next@cf8ee683`, attribute the LDST regression without weakening lifecycle correctness, and retain
only reproducible summaries and runners.

**Blocked by:** T33 Skip unused lifecycle trace setup when output is off.

- [x] Correct role-specific dynamic-library loading through macOS `/usr/bin/time` and exclude
      invalid earlier comparisons.
- [x] Locate the first large LDST regression and separate generic callback wrapping, tracked-event
      bookkeeping, and packet birth/terminal accounting.
- [x] Validate a typed tracked scheduling prototype that preserves forced-stop cancellation and
      exact semantic artifacts.
- [x] Publish the accepted report, aggregate summary, authoritative comparison summaries, and
      reproducible final runners without raw profile or run directories.

Closeout:

- Corrected counterbalanced comparisons identify T13 LDST callback wrapping and lifecycle tracking
  as the first large regression. The typed tracked prototype reduces its current in-block wall gap
  versus next from `18.12%` to `4.75%` while preserving exact artifacts and forced-stop behavior.
- Packet identity accounting remains a smaller shared cost. Transfer observation already returns
  before packet-tag reads when lifecycle output is OFF.
- LDST is not generalized to other Unified Bus modules; module-specific calibration is delegated to
  T35 because LDST is an uncommon path with a pending redesign.

## Task 35: Calibrate module-level lifecycle hotspots

**What to build:** Measure ordinary RTP, CTP, RTP GBN/RTO, and PFC/DCQCN independently against
`next@cf8ee683`, then select the first common-path optimization from evidence rather than LDST code
shape.

**Blocked by:** T34 Calibrate lifecycle OFF performance root causes.

- [x] Run all module fixtures serially with one warm-up and counterbalanced pairs.
- [x] Preserve exact task and protocol artifacts with legacy and lifecycle trace output disabled.
- [x] Measure CTP payload scaling through 4 GiB and attribute the stable wall/RSS growth to
      receiver-side send-window context churn.
- [x] Publish the accepted manifest, timing/profile summaries, aggregate report, and reproducible
      runner without raw profiles or per-run output trees.

Closeout:

- Ordinary RTP, RTP GBN/RTO, and PFC/DCQCN remain within `+2.51%` paired-median wall of next in this
  matrix; ordinary CTP is the only stable common-path regression at `+14.73%` for 512 MiB.
- CTP remains about `+15%` slower from 512 MiB through 4 GiB while RSS grows from 25.9 MB to
  99.9 MB. Profiles identify repeated receiver-side creation and retirement of the send ACK-window
  context as the primary mechanism.
- T36 therefore owns the lazy CTP context, receiver complete-operation path, queue identity, and
  lifecycle retired-history corrections under byte and performance gates.

## Task 36: Lazily create the CTP send-window context

**What to build:** Process each CTP TP Packet as one complete transaction operation without
receiver-side partial assembly or a send-side ACK bitmap, and instantiate the existing context only
when a send or ACK window path requires it. Preserve pre-refactor CTP allocator identity and compact
trace-OFF retired generation history where required to meet the accepted correctness and performance
gates.

**Blocked by:** T35 Module-level lifecycle hotspot calibration.

- [x] Creating finite CTP Transaction state does not itself allocate a send-window context.
- [x] Direct sends, Jetty drains, wire ACKs, delayed TAACK, forced terminal, TASSN continuity, and
      finite retirement preserve the existing window and lifecycle behavior.
- [x] WRITE TAACK and READ response generation do not allocate a send-window context on the target.
- [x] Each received CTP TP Packet is submitted directly as one complete transaction operation and
      leaves no partial-assembly Transaction state.
- [x] Empty CTP queue shells preserve their pre-refactor allocator identity until service disposal;
      complex multi-Entity and bidirectional scheduling remains byte-identical to `next@cf8ee683`.
- [x] Trace-OFF retired generation history uses bounded runs without changing stale/retired
      classification, local-ID reuse, or enabled lifecycle summary bytes.
- [x] Targeted build and the CTP Transaction, CTP lifecycle, lifecycle storage, lifecycle trace, and
      lifecycle suites pass serially.
- [x] Frozen CTP window cases are byte-identical to `next@cf8ee683` without an unexplained
      difference.
- [x] Five counterbalanced 2 GiB CTP pairs recover at least eight percentage points of the T35 wall
      gap, finish within five percent of next, and keep current peak RSS within ten percent of next.

Closeout:

- UB Base Specification 2.0 section 6.7.2 defines each CTP TP Packet as one complete transaction
  operation. The receiver reuses the existing segment construction block directly and retains no
  cross-packet assembly state. Higher-level Write/Read segmentation remains unchanged: each segment
  has its own TASSN and CTP TP Packet. Builders, parsers, Jetty admission, ACK/window, response,
  callback, timer, trace, and completion algorithms are unchanged.
- Empty CTP queues retain the pre-refactor service-owned object and allocator slot after draining.
  This preserves RR/DWRR cursor interpretation across Entity bursts; `DoDispose` still unregisters
  every retained queue before clearing the maps.
- With lifecycle trace OFF, adjacent retired IDs with identical generation/outcome history share a
  compact run. Reopen splits exactly one ID, increments through the old open path, and preserves
  stale, retired, unknown, and terminal-outcome behavior. Enabled trace levels retain per-resource
  summaries and bytes.
- The targeted build and five focused suites passed serially. Frozen `ctp-win1`, `ctp-win4`, and
  `ctp-win128`, plus complex multi-Entity and mixed-bidirectional CTP canaries, are byte-identical to
  `next@cf8ee683`; no exception is required.
- At 2 GiB, five counterbalanced pairs measure current versus next at `+4.20%` wall, `+4.05%` user
  CPU, and `-0.74%` peak RSS. This recovers 10.84 percentage points of the T35 wall gap and passes
  both final gates. An independent five-pair 8 GiB run measures `-0.01%` wall, `+0.26%` user CPU,
  and `-0.42%` peak RSS, with no payload-scaled tombstone growth.
- The portable frozen aggregate resolves next from `--next-root` or `T36_NEXT_ROOT`, rejects any
  revision other than exact `cf8ee683`, records source/binary/library hashes, and exits zero for all
  three frozen CTP cases without an exception.

## Task 37: Restore DCQCN idle reset semantics

**What to build:** Restore the pre-refactor DCQCN sender reset when a transport becomes idle while
retaining lifecycle-tracked cancellation for rate, alpha, and pacing events.

**Blocked by:** T36 Lazily create the CTP send-window context.

- [x] Reuse the continuous reset block from `main@ccf41b58` and `next@cf8ee683` without changing
      CNP, recovery-increase, pacing-debt, marking, packet, or TP cleanup algorithms.
- [x] Preserve release-active-flow, tracked-event cancellation, scalar reset, and pacing-time order.
- [x] Characterize immediate current/target-rate reset and prove canceled recovery timers cannot
      mutate the idle state.
- [x] Pass targeted build plus congestion-control, DCQCN marking, and lifecycle suites serially.
- [x] Run one bounded 16 MiB PFC/DCQCN differential against `next@cf8ee683` through real Task,
      Port, PFC, and congestion trace seams.

Closeout:

- Idle now resets alpha, current/target rate, time/byte recovery counters, and byte-progress state
  exactly as main and next did. Current typed tracked-event cancellation remains unchanged.
- Fifteen shared PFC, Port, congestion, and task artifacts are byte-identical and parsed task rows
  match. `TaskTrace_node_2.tr` has the same 2,048-record multiset and exact timestamps/TASSNs but an
  existing same-timestamp Send/Complete write order; this trace-only narrow exception is outside the
  idle path and does not affect protocol bytes or completion.

## Task 38: Move CTP transaction state to its canonical owner

**What to build:** Move the CTP transaction identity, ACK-window context, and finite state types from
the CTP service header to the Transaction owner without changing runtime behavior.

**Blocked by:** T37 Restore DCQCN idle reset semantics.

- [x] Move `UbCtpEntityKey`, `UbCtpTransactionContext`, and `UbCtpTransactionState` into
      `ub-transaction.h` without changing fields, defaults, or public methods.
- [x] Move the key comparison and ACK-window method implementations byte-for-byte into
      `ub-transaction.cc`.
- [x] Remove the `ub-transaction.h -> ub-ctp.h` reverse header dependency while keeping the CTP
      service dependent on its Transaction-owned state.
- [x] Preserve lazy context creation, ACK bitmap behavior, pending/outstanding containers,
      completion-event accounting, retirement, packet, queue, routing, and callback order.
- [x] Pass the targeted build and the CTP transaction, CTP lifecycle, and lifecycle suites serially.
- [x] Complete a separate review against the pre-move implementation and pass `git diff --check`.

Closeout:

- This is a pure owner contraction. The complete comparator and ACK-window method blocks are exact
  moves; no branch, state mutation, error, timer, trace, packet, queue, or protocol algorithm changed.
- `ub-transaction.h` no longer includes `ub-ctp.h`; `ub-ctp.h` now consumes the
  Transaction-owned definitions. The implementation-level include remains where Transaction calls
  CTP service methods.
- The targeted build and all three focused suites passed. T38 is not a frozen-byte milestone, so no
  large baseline comparison was rerun.

## Task 39: Narrow the Transaction-to-Function WQE terminal seam

**What to build:** Keep Transaction's existing WQE/segment terminal mechanics while moving the
continue/stop/force decision to the Function-owned Jetty cleanup caller.

**Blocked by:** T38 Move CTP transaction state to its canonical owner.

- [x] Preserve the complete `WqeFinish` cancellation, lease, lifecycle, erase, ordering, outcome,
      and callback sequence.
- [x] Map ACTIVE and graceful drain to `CONTINUE_ACCEPTED`, abort drain to `STOP_ACCEPTED`, and
      simulation-stop drain to `FORCE_DRAIN` at the Jetty manager call seam.
- [x] Remove Transaction's reverse calls to Function cleanup policy without removing owner-side
      characterization queries.
- [x] Preserve the original three-argument `CompleteWqeOperation` API and scheduled member-function
      binding; use one manager-specific drain entry for the typed mode.
- [x] Pass the targeted build plus transaction URMA, transport pipeline, and lifecycle suites.
- [x] Review every `WqeFinish` and `CompleteWqeOperation` caller and pass `git diff --check`.

Closeout:

- Transaction no longer reaches into Function to decide Jetty cleanup policy. The manager selects a
  typed terminal mode from its canonical `CleanupMode`, while Transaction reuses the same terminal
  implementation and still emits exactly one typed outcome to the manager-owned WQE record.
- Reentrant cleanup escalation remains observable per WQE because `DestroyJetty` re-reads the
  manager record and derives the mode on every loop iteration.
- The targeted build and all three focused suites passed. T39 changes ownership plumbing only and is
  not a frozen-byte milestone.

## Task 40: Curate the promotion tree and publish final acceptance

**What to build:** Freeze the final production and exact next roles, replay the accepted complex
CTP/RTP matrix, promote only compact reproducible evidence, and publish an honest final acceptance
report without changing Unified Bus source code.

**Blocked by:** T39 Narrow the Transaction-to-Function WQE terminal seam.

- [x] Inventory tracked and untracked evidence without deleting or bulk-staging user files.
- [x] Freeze source, binary, Unified Bus library, input, and accepted-comparison hashes.
- [x] Run CTP windows 1/4/128 and RTP shared-window, GBN double-loss, and TPG pinning serially.
- [x] Require exact CTP shared artifacts/task rows and non-degenerate next-matched window response.
- [x] Require each RTP role to reproduce its archived shared tree exactly with matching task IDs.
- [x] Publish the final performance matrix without averaging p01/p02, LDST, CTP, RTP, GBN, or
      DCQCN into one number.
- [x] Preserve the macOS umbrella-runner, automatic TP cleanup, LDST, and parallel-rerun residuals
      explicitly.
- [x] Review the final source against `main@ccf41b58` and `next@cf8ee683` and pass `git diff --check`.

Closeout:

- All 12 simulations completed serially. CTP is byte-identical for windows 1, 4, and 128, and the
  completion vectors exactly match next while remaining distinct across the three windows.
- The three RTP cases exactly reproduce both archived role trees with matching task IDs. Their
  differences remain limited to the existing T05/T06/T14 boundaries; no path, hash, task, or
  exception was added.
- An initial analysis-only false failure required full RTP task-row equality. The runner now reuses
  the T28 archived-replay plus task-ID gate and can analyze saved results; no simulation was rerun.
- The promotion commit stages only five compact T40 artifacts. All 17,880 other untracked files and
  the user-modified p01 diagnosis runner remain untouched.
- Final review finds no new hard standards or spec defect. Automatic TP cleanup remains excluded,
  the LDST typed-tracked prototype remains unpromoted, the macOS umbrella runner remains unresolved,
  and the complete parallel matrix remains bound to T20 evidence.

## Task 41: Close post-promotion validation residuals

**What to build:** Repair stale CTP test fixtures that masked assertion failures as runner exit 139,
then replay the frozen cross-owner case under local-MTP, MPI, and hybrid runtimes after T38/T39.

**Blocked by:** T40 Curate the promotion tree and publish final acceptance.

- [x] CTP packet fixtures use the real sender service, dynamic topology node IDs, the current
      Transaction-owned submission path, and the CTP segment ceiling without changing production
      protocol code.
- [x] A failed packet setup assertion cannot continue into a null packet dereference.
- [x] The umbrella `unified-bus` suite exits zero; expected isolated death-test diagnostics remain
      distinguishable from a parent-runner crash.
- [x] Frozen m18 local-MTP, MPI, and hybrid cases complete serially under post-T39 binaries with
      explicit lifecycle SUMMARY output.
- [x] All three lifecycle summaries have zero finite active residue, and their complete `output/`
      and `runlog/` trees are byte-identical to the accepted m18 lifecycle role.
- [x] The final report separates the confirmed fixture root cause from the independent macOS
      post-`fork()` safety limitation and records the canonical future harness direction.
- [x] Review confirms no production model change and `git diff --check` passes.
