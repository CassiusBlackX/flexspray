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
- [x] Empty queue and completed Transaction state can retire while the CTP service and allowed cacheable policy state persist.
- [x] Loss without lower-layer recovery remains explicitly unresolved rather than being hidden by forced cleanup.
- [x] At the CTP milestone, the verified frozen baseline byte-matches all applicable CTP task, packet, queue, throughput, and canonical artifacts.

Closeout:

- `UbCtpEntityKey` is local to its node-scoped service and `UbCtpCongestionKey` aggregates only
  destination node, destination Entity, and VL. Existing production parsing characterizes
  source-Entity sharing, destination/VL isolation, and reverse normalization.
- The pre-refactor `BuildCtpPacket` block remains the sole CTP packet builder. `UbCtpTxQueue`
  stores only complete DATA/ACK/CNP packets, preserves CNP > ACK > DATA priority, and unregisters
  from the allocator before an empty queue is erased.
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
- [x] Direct CTP disposal detaches queue callbacks, unregisters live allocator sources, clears
      service queue ownership, and preserves explicit unresolved packet residue.
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
  CTP disposal reuses normal queue retirement order, detaches callbacks, unregisters a live allocator,
  clears service ownership, and leaves retained packet work explicitly unresolved.
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
