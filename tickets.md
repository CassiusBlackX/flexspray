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

- [ ] Entity keys are node-local directed communication keys and congestion keys aggregate only destination node, destination Entity, and VL.
- [ ] Sharing and isolation tests cover source Entity, destination node, destination Entity, VL, and reverse-packet normalization.
- [ ] Transmit queues contain complete packets and no raw pointer to cacheable Entity or Transaction state.
- [ ] DATA, ACK, and CNP priority, packet terminal accounting, queue registration, and tracked events balance independently.
- [ ] Empty queue and completed Transaction state can retire while the CTP service and allowed cacheable policy state persist.
- [ ] Loss without lower-layer recovery remains explicitly unresolved rather than being hidden by forced cleanup.
- [ ] At the CTP milestone, the verified frozen baseline byte-matches all applicable CTP task, packet, queue, throughput, and canonical artifacts.

## Task 13: Close the LDST operation lifecycle

**What to build:** Carry LOAD and STORE task segments through worker admission, request/response packets, acknowledgement, completion, tracked events, active-record erasure, and forced stop.

**Blocked by:** T01 Establish a single-TP packet lifecycle tracer bullet.

- [ ] Task segment, request/response packet, ACK record, and scheduled action each have one owner and terminal path.
- [ ] Successful completion removes the segment from every active LDST map and updates compact statistics only where required.
- [ ] Unknown, duplicate, and stale ACKs cannot insert records or underflow counters.
- [ ] Forced stop reports active LDST residue explicitly and releases scheduled work before disposal.
- [ ] Thread selection, address behavior, packet sizes, completion timing, and stable outputs match the frozen baseline.

## Task 14: Separate route storage from routing policy on the live packet path

**What to build:** Carry packets through separated route lookup and out-port selection while preserving exact/range routes, port-set sharing, hash, packet spray, adaptive selection, and ingress filtering.

**Blocked by:** T01 Establish a single-TP packet lifecycle tracer bullet.

- [ ] Route lookup performs no policy selection and returns a bounded read-only candidate view.
- [ ] Hash, spray, shortest/non-shortest, adaptive selection, ingress filtering, and tie-break results match current behavior.
- [ ] The forwarding hot path does not allocate temporary route or best-port containers per packet.
- [ ] Route failure still produces exactly one packet terminal outcome.
- [ ] Focused routing characterization tests and targeted route-failure/medium-topology scenarios preserve selection and trace behavior; allocation count and forwarding time are reported.

## Task 15: Split Datalink flow control without changing control frames

**What to build:** Carry NONE, CBFC, shared-credit CBFC, and PFC traffic through distinct Datalink owners while preserving switch arbitration, thresholds, credit behavior, pause/resume, and control-packet lifecycle.

**Blocked by:** T01 Establish a single-TP packet lifecycle tracer bullet.

- [ ] Base flow-control, CBFC, shared-credit behavior, PFC, and decision hooks retain their existing public configuration and trace behavior.
- [ ] Credit, pause, resume, piggyback, and fallback control-frame behavior matches current tests.
- [ ] Every Datalink control packet has one persistent port/VL accounting owner and one terminal outcome.
- [ ] Switch allocator order and queue ownership do not move into the flow-control modules.
- [ ] Focused NONE/CBFC/PFC threshold and control-frame tests plus a targeted queue-pressure scenario preserve arbitration, credit, pause/resume, and trace behavior.

## Task 16: Balance VOQ ownership, allocator events, and queue disposal

**What to build:** Carry packets through switch-owned sparse VOQs and queue-manager buffer accounting, then safely retain or retire empty VOQs through one gated unregister/finalize path.

**Blocked by:** T15 Split Datalink flow control without changing control frames.

- [ ] The switch is the only VOQ collection owner; the queue manager owns buffer pools, admission, and occupancy only.
- [ ] Queue entry removal, ingress/VOQ/egress occupancy, overflow-storage release, and flow-control views remain balanced.
- [ ] A VOQ cannot retire while a packet, in-port event, allocator token, flow-control occupancy, or algorithm lease remains.
- [ ] Retaining an empty shell and destroying it use the same safe gate and are selected only by explicit teardown or measured storage policy.
- [ ] Allocator and diagnostic events are saved, canceled, and resolved symmetrically; forced disposal records residue.
- [ ] Focused sparse-VOQ and allocator tests plus a targeted queue-pressure scenario preserve allocation order, queue traces, TypeId/attributes, and balanced occupancy.

## Task 17: Route lifecycle accounting through MTP, MPI, and hybrid owners

**What to build:** Carry remote packet terminal, resource mutation, endpoint-slot state, destroy request, and destroy completion through fixed-size causal control records routed to the owning LP/rank.

**Blocked by:** T10 Tear down a paired TP connection only after both slots drain; T12 Make CTP keys, queues, events, and finite children retire independently; T13 Close the LDST operation lifecycle; T14 Separate route storage from routing policy on the live packet path; T16 Balance VOQ ownership, allocator events, and queue disposal.

- [ ] Only the execution owner mutates each resource generation's ledger and protocol container.
- [ ] Remote terminal and destroy-completion records are delivered exactly once or rejected as duplicate/stale in diagnostic injection tests.
- [ ] Drain-only slot creation is causally visible before terminal, response, or endpoint completion derived from the late packet.
- [ ] Raw callbacks, mutable protocol objects, and direct peer-object mutation never cross LP/rank boundaries.
- [ ] Unsupported control transport fails before enabling lifecycle functionality that needs remote confirmation.
- [ ] Single-thread, MTP, MPI, and hybrid semantic artifacts match according to the frozen acceptance manifest; owner sequence and pending-record balance are reported.

## Task 18: Publish lifecycle trace schema and parser semantics

**What to build:** Carry lifecycle state, ledger delta, identity, and terminal evidence from typed trace sources through SUMMARY/EVENT/DETAIL sinks, versioned schema metadata, complete-identity sampling, and parser validation.

**Blocked by:** T17 Route lifecycle accounting through MTP, MPI, and hybrid owners.

- [ ] Correctness ledger, trace sources, and deep verification remain independent execution planes.
- [ ] Summary reads existing counters without creating duplicate event accounting.
- [ ] Event/detail records use fixed fields, generation-aware identities, owner sequence, stable numeric enums, and one schema metadata source.
- [ ] Detail filtering retains complete selected resource generations or packet identities and never randomly drops events from a retained chain.
- [ ] Missing metadata, unknown version/enum, and inconsistent MPI-rank mappings are explicit parser errors.
- [ ] Focused OFF/SUMMARY/EVENT/DETAIL runs preserve task completion, throughput, and deterministic protocol outcomes while validating emitted schemas.

## Task 19: Bound trace writer memory, file descriptors, and write overhead

**What to build:** Carry trace records through pre-resolved sink handles, owner-local bounded chunks, pressure flushing, batched writes, and file-descriptor LRU without changing existing artifact names or parser inputs.

**Blocked by:** T18 Publish lifecycle trace schema and parser semantics.

- [ ] Default chunks are 64 KiB and are created lazily; no owner/sink holds more than one unfinished chunk.
- [ ] Each execution owner enforces the 1 MiB/512 KiB high/low-water budget without a polling timer or unbounded completed-chunk queue.
- [ ] Effective open-file count respects the configured 1024 maximum, platform soft limit minus reserved descriptors, and conservative fallback.
- [ ] File reopen uses append mode, and open/write/flush/close errors cannot silently lose enabled evidence.
- [ ] Existing final filenames and parser inputs remain unchanged.
- [ ] Benchmarks report 16/64/256 KiB results, wall time, syscall count, RSS, trace bytes, descriptor pressure, visibility behavior, and MTP scaling.
- [ ] At the trace/writer milestone, the verified frozen baseline byte-matches every applicable existing network artifact while writer-specific outputs retain their filename and parser contracts.

## Task 20: Contract compatibility layers after all owners migrate

**What to build:** Complete the expand-contract migration by removing obsolete ownership, dead mirrors, and no-longer-needed compatibility forms only after every vertical slice has moved to the canonical owner and interface.

**Blocked by:** T05 Preserve backpressure and TPG member lifecycle; T07 Migrate selective retransmission and remove full-packet retention; T10 Tear down a paired TP connection only after both slots drain; T12 Make CTP keys, queues, events, and finite children retire independently; T13 Close the LDST operation lifecycle; T14 Separate route storage from routing policy on the live packet path; T16 Balance VOQ ownership, allocator events, and queue disposal; T18 Publish lifecycle trace schema and parser semantics.

- [ ] Every migrated collection has one owner and no old caller retains a mutable mirror or erase path.
- [ ] Compatibility forwarding and aliases remain only where an explicit public compatibility decision requires them.
- [ ] Protocol headers, value types, managers, queues, routing, and flow-control modules reflect their canonical domain ownership.
- [ ] Pure movement/renaming is separated from behavior changes in reviewable commits.
- [ ] Build, module tests, minimal cases, and all applicable byte-stable medium-case outputs remain green.

## Task 21: Run the full semantic and performance acceptance matrix

**What to build:** Execute the complete validation matrix, classify every result, and decide storage/default policies from evidence without enabling automatic close or destroy prematurely.

**Blocked by:** T17 Route lifecycle accounting through MTP, MPI, and hybrid owners; T19 Bound trace writer memory, file descriptors, and write overhead; T20 Contract compatibility layers after all owners migrate.

- [ ] RTP, CTP, LDST, WRITE, READ, response work, loss/drop, GBN/selective, CAQM/DCQCN, NONE/PFC/CBFC, direct/TPG, teardown, late reuse, lifecycle races, and forced stop all have required evidence.
- [ ] Single-thread, MTP, MPI, and hybrid comparisons include owner-local balance and deterministic/canonical artifact checks.
- [ ] Every reference artifact classified as byte-stable is byte-identical; every canonical-only artifact has matching canonical bytes and preserved raw evidence.
- [ ] Every mismatch is either fixed or listed in the approved exception manifest with Specification or characterization evidence.
- [ ] Accounting, deep verification, detail trace, writer settings, and storage policies are measured separately for wall time, events, objects/allocations, peak RSS, trace bytes, syscalls, descriptors, and scaling.
- [ ] Automatic TP close/destroy remains disabled; any storage-retention default is selected only from the reported measurements.
