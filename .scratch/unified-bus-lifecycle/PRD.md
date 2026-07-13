# Specification: Unified Bus Modeled Resource Lifecycle

Status: ready-for-agent

Source baseline: confirmed lifecycle architecture at commit `7648121`, audited against `main@ccf41b58` and the UB Base Specification 2.0.

## Problem Statement

ns-3-UB already exposes most protocol events needed to determine when UnifiedBus work starts, remains active, and reaches a terminal outcome, but ownership and cleanup are spread across Function, Transaction, Transport, Network, Datalink, queue, fault, and parallel-runtime components. Current cleanup can depend on traffic-workload foresight, direct container erasure, mirrored references, or object disposal rather than modeled protocol progress. This can retain completed WQEs, transaction segments, packets, events, retransmission state, CTP state, LDST records, and dynamic queue state until simulation teardown, while also making safe TP or Jetty teardown impossible to prove.

The simulator needs one lifecycle architecture that preserves current modeled protocol behavior, assigns every dynamic resource to one protocol owner, accounts for finite child resources from creation to a typed terminal outcome, and coordinates drain without turning the lifecycle layer into a global protocol manager. Correctness must not depend on trace output, future workload knowledge, host memory pressure, or idle timeouts.

## Solution

Introduce a node-scoped lifecycle coordinator with a deliberately small interface for generation-aware resource registration, typed binding/work/retained-state leases, tracked events, packet birth and terminal accounting, drain readiness, and invariant snapshots. Existing protocol owners remain responsible for protocol-specific admission, progress, failure, cancellation, drain inspection, and finalization.

Every physical packet attempt is accounted from protocol emission until endpoint consumption or an explicit drop. Every WQE and transaction segment is accounted until completion, failure, cancellation, or flush. Every scheduled operation that can retain a reclaimable resource is tracked until fire or cancellation. Managers that own collections provide create, resolve, destroy request, drain inspection, private finalize, and simulation-stop disposal as one complete contract.

Modeled resource lifecycle stays separate from storage policy. Idle RTP TP channels remain valid by default and may release only reconstructible backing state without changing their reservation, TPN, identity, or generation. TP endpoint destroy and TP connection teardown are explicit simulator-local management actions, not workload-driven automatic close or a newly invented wire protocol. CTP remains a node-scoped service rather than a channel generation, while its finite transaction, queue, event, packet, and algorithm work terminates normally.

Current network results, completion semantics, scheduling behavior, routing decisions, flow-control behavior, trace contracts, and deterministic runtime behavior are preserved unless a separate change has Base Specification evidence or characterization evidence.

## User Stories

1. As a simulator maintainer, I want each dynamic resource generation to have one authoritative owner, so that no component can silently mutate or retire another owner's state.
2. As a protocol developer, I want lifecycle accounting to use typed leases and terminal outcomes, so that new algorithms cannot pair unrelated generic increments and decrements.
3. As a simulation user, I want completed network work to release finite state promptly, so that long workloads do not retain every historical operation until teardown.
4. As a simulation user, I want network results to remain unchanged by the refactor, so that existing studies remain comparable.
5. As a simulation user, I want trace settings to affect evidence volume only, so that disabling trace never disables correctness accounting.
6. As a Function-layer developer, I want Jetty lifecycle intent to remain with the upper-layer URMA client, so that temporary idleness is not mistaken for logical destruction.
7. As a Function-layer developer, I want WQE submission to be atomic across Jetty, Transaction, and transport binding state, so that validation failure cannot leave a half-submitted operation.
8. As a Function-layer developer, I want each WQE to receive exactly one typed terminal outcome, so that completion, failure, cancellation, and flush are distinguishable.
9. As a Transaction-layer developer, I want transaction segmentation, TASSN state, ordering, response matching, and operation completion to have one owner, so that transport modes do not maintain competing transaction engines.
10. As a Transaction-layer developer, I want segment creation to remain lazy and capacity-driven, so that memory use stays bounded by active windows and existing pacing remains stable.
11. As an RTP developer, I want direct TP and TPG references to enter one atomic submission path, so that resolution, generation validation, admission, and enqueue cannot race with teardown.
12. As an RTP developer, I want a TPG to select one TP per transaction segment, so that multi-segment operations can stripe while every segment and retransmission stays on one resolved TP generation.
13. As an RTP developer, I want direct and TPG-selected traffic to share one TP's queue, PSN, retransmission, and congestion state, so that TPG membership does not create parallel transport contexts.
14. As an RTP developer, I want ready transaction sources to use bounded event-driven scheduling, so that capacity wakeups do not scan all bindings or create polling timers.
15. As an RTP developer, I want retransmission state to retain protocol descriptors instead of completed wire packets, so that every retransmission is a separately accounted physical attempt.
16. As an RTP developer, I want GBN and selective retransmission to share sent-item retirement and packet rebuilding, so that ACK, SACK, timeout, loss, and cancellation have symmetric terminal paths.
17. As a TP reservation owner, I want reservation, session generation, endpoint slot, and runtime endpoint state to remain distinct, so that lazy materialization and TPN validation continue to work.
18. As a TP reservation owner, I want an explicit local endpoint destroy operation to freeze new work and wait for packets, bindings, events, retained state, queues, and allocator ownership, so that finalization cannot occur early.
19. As a TP reservation owner, I want explicit connection teardown to aggregate both fixed endpoint slots, so that reservation removal and TPN release happen only after both sides drain.
20. As a TP receiver, I want a late packet for a closing reserved generation to create or retain a drain-only endpoint, so that valid old work is not mistaken for a new connection or silently discarded.
21. As a simulation user, I want idle TP channels to stay logically connected by default, so that temporary inactivity does not change PSN, congestion, reservation, or TPN semantics.
22. As a configuration author, I want legacy task-driven TP removal to fail fast when enabled, so that unsupported automatic teardown cannot silently change results.
23. As a CTP developer, I want CTP to remain a node-scoped service without RTP channel identity, so that the simulator does not invent TPN, PSN, or teardown semantics for CTP.
24. As a CTP developer, I want Transaction to own CTP transaction windows and completion while CTP owns packetization, routing, congestion, and transmit queues, so that each finite child has one terminal path.
25. As a CTP developer, I want CTP segmentation constraints to flow from transport capability into Transaction, so that CTP does not mutate Function-layer segmentation state.
26. As a CTP developer, I want Entity and congestion keys to use their canonical node-local scopes, so that unrelated sources do not split or merge congestion state incorrectly.
27. As a CTP developer, I want transmit queues to contain complete packets without raw pointers into service records, so that queues and cacheable Entity state can retire independently.
28. As an LDST developer, I want each task segment, ACK record, and scheduled action to terminate symmetrically, so that completed LDST operations do not remain in active maps.
29. As a switch developer, I want the switch to own VOQ instances while the queue manager owns buffer accounting, so that empty-queue retention is a storage decision rather than ambiguous ownership.
30. As a switch developer, I want safe VOQ unregister and destroy to use one owner-private gate, so that future retention policies cannot bypass allocator, flow-control, or occupancy checks.
31. As a routing developer, I want route storage and route selection to be separate deep modules, so that current route results can be preserved while removing per-packet temporary allocation.
32. As a Datalink developer, I want CBFC and PFC to keep distinct protocol state and control-packet accounting, so that responsibility moves do not alter thresholds or packet behavior.
33. As a fault-model developer, I want every drop, delay, and physical fork to have explicit lifecycle semantics, so that fault injection cannot leak or double-terminate packet attempts.
34. As an MTP developer, I want resource mutation to occur only on the owning LP, so that lifecycle state follows the same ownership rule as protocol state.
35. As an MPI developer, I want remote packet terminal and destroy completion to use causal fixed-size control records, so that peers never directly mutate each other's resources.
36. As a parallel-runtime developer, I want unsupported lifecycle control transport to fail fast, so that teardown never relies on timing assumptions or cleanup timeouts.
37. As a diagnostics user, I want low-cost counters and teardown balance to be always available, so that leaks and premature retirement are visible without detailed trace.
38. As a diagnostics user, I want optional lifecycle event and detail records to use a versioned schema and generation-aware identities, so that parsers can reconstruct causality without guessing.
39. As a performance engineer, I want trace buffers and open files to have explicit bounds, so that observability cannot introduce unbounded host-memory or descriptor growth.
40. As a performance engineer, I want correctness accounting, deep verification, detailed trace, and storage policies benchmarked separately, so that diagnostic cost is not confused with normal runtime cost.
41. As a contributor, I want every new dynamic resource to declare ownership, leases, child resources, terminal paths, drain conditions, storage bounds, and parallel requirements, so that lifecycle omissions are rejected during review.
42. As a maintainer, I want compatibility aliases and forwarding APIs during migration, so that ownership can move without combining behavior changes with a repository-wide rename.
43. As a maintainer, I want one immutable current-main reference binary and hashed build manifest, so that every implementation slice compares against the same executable baseline instead of rebuilding a moving target.
44. As a developer, I want simple bring-up cases and separate medium-complexity acceptance cases, so that failures are easy to debug while acceptance still exercises the lifecycle risks that trivial traffic cannot reach.

## Implementation Decisions

- `【用户已确认】` The architecture is protocol owners coordinated by one node-scoped lifecycle coordinator, not a process-global manager and not one lifecycle manager per protocol layer.
- `【用户已确认】` The coordinator owns generation allocation, lifecycle state, typed parent/child and binding edges, work/event/retained-state/packet counters, invariant snapshots, and idle notifications. It does not parse protocol headers, decide protocol completion, choose routes or TPG members, or destroy remote objects.
- `【用户已确认】` Resource identity is owner-local and generation-aware. A new logical resource receives a new monotonically increasing generation; transparent backing-state reconstruction preserves the current generation; stale mutations fail fast.
- `【用户已确认】` Lifecycle state and protocol state are independent. Temporary zero use means `Idle`; explicit destruction is irreversible for the current generation and freezes new application work; finite child resources may retire directly on their typed terminal outcome.
- `【用户已确认】` Lifecycle policy, logical close/destroy, C++ object lifetime, and container capacity retention are separate decisions. Host RSS, allocator pressure, idle duration, and workload completion cannot prove protocol drain.
- `【用户已确认】` Layer-owned lifecycle accounting records each finite resource once at its direct owner. Ancestors are protected through typed work, child, and binding relationships rather than duplicated packet counters.
- `【用户已确认】` Every physical packet attempt has exactly one accounting owner and remains active across queues, ports, local links, remote links, and MPI transfer until endpoint consumption or an explicit terminal drop. Parser views, ownership transfers, retained templates, physical forks, and terminals use distinct named operations.
- `【审查确认】` Packet lifetime metadata is simulator-only, survives packet copy and serialization, and is separate from wire headers. Native packet UID remains diagnostic correlation only because copy and fragment operations can preserve it.
- `【用户已确认】` Every collection-owning manager provides create/register, resolve, state query, destroy request, drain inspection, private finalization, and simulation-stop disposal. Callers cannot erase manager-owned collections directly.
- `【用户已确认】` Destroy requests return a typed synchronous status. Only accepted pending requests register completion callbacks; finalization happens once, after owner-local erase, and repeated requests share the same drain flow.
- `【审查确认】` Dynamic delayed work carries a generation-aware resource reference and tracked-event token instead of a long-lived strong reference. Event fire and cancellation each resolve the token exactly once.
- `【用户已确认】` Function owns URMA submission intent, Jetty collection through a Jetty manager, WQE records, and upper-layer completion delivery. Transaction owns segmentation, TASSN, ordering, response matching, operation progress, and transaction-segment outcomes.
- `【用户已确认】` WQE submission uses one Function façade that validates the Jetty generation and binding before atomically exposing the WQE, operation context, and transport-use lease. Failed admission changes no container.
- `【用户已确认】` Transaction segmentation stays lazy. Transport reports bounded mode/MTU capabilities; Transaction applies operation-specific request and response constraints; transport cannot mutate Jetty segmentation state.
- `【用户已确认】` RTP uses a node-scoped service, a TP channel manager, and a real TPG collection. Direct TP and TPG references are non-owning, generation-aware values resolved at use time.
- `【用户已确认】` TPG member selection occurs once per transaction segment. Direct and selected segments share one TP admission and backpressure path. Selection state and Transaction cursors advance only after atomic admission succeeds.
- `【用户已确认】` The first TPG member-selection behavior preserves segment-level round-robin compatibility. Shared-TP source order remains one fixed ready-source discipline, not a second public strategy framework.
- `【用户已确认】` TPG membership, TPG-use leases, direct bindings, and accepted TP work are distinct. Removing a member or destroying a TPG never migrates accepted segments and never destroys the member TP.
- `【用户已确认】` TP reservation, RTP session generation, two fixed endpoint slots, runtime endpoint generation, and reconstructible endpoint backing state remain distinct.
- `【用户已确认】` Idle TP channels are not automatically closed. They retain identity, reservation, TPN, generation, PSN continuity, and protocol state required across idle. Only reconstructible backing state may be reclaimed transparently.
- `【用户已确认】` Local TP endpoint destroy and paired TP connection teardown are explicit simulator-local management interfaces. The former ends one endpoint generation while preserving the reservation; the latter ends the reservation/session generation and releases both TPNs only after both endpoint slots drain.
- `【审查确认】` Closing reservations continue to validate old packets. A valid late packet can materialize a drain-only endpoint for the closing generation; invalid TPNs remain hard simulator errors and cannot create a new generation.
- `【用户已确认】` Legacy task-driven TP removal is removed as an active path. Its default remains disabled, and enabling it fails fast instead of being reinterpreted as storage reclamation.
- `【用户已确认】` RTP sent state uses descriptor-based items and one packet builder for initial send and retransmission. `sndNxt` remains the first never-sent PSN; retransmission selection uses separate state; ACK, SACK, failure, and cancellation retire items symmetrically.
- `【用户已确认】` CTP is a persistent node-scoped service with no CTP root generation or channel manager. Transaction-owned CTP operation state is separated from CTP-owned Entity policy, packetization, congestion, routing, event, and transmit-queue state.
- `【审查确认】` CTP Entity keys are node-local directed communication keys, while CTP congestion keys aggregate by destination node, destination Entity, and VL. Queue records do not retain raw pointers to cacheable service records.
- `【审查确认】` Topology structures remain persistent. The switch owns VOQ instances; the queue manager owns switch buffer pools, admission, and occupancy. Empty VOQ object retention is a measured storage policy behind one safe unregister/finalize gate.
- `【审查确认】` Route storage/lookup and routing policy become separate deep modules while preserving exact/range routes, port-set sharing, hash, packet-spray, adaptive selection, ingress filtering, and result ordering.
- `【审查确认】` Datalink flow-control responsibilities are split into base, CBFC, and PFC modules without moving switch arbitration or VOQ ownership into Network or Datalink.
- `【审查确认】` LDST worker threads remain topology-scoped; task segments, ACK records, and scheduled operation work are finite lifecycle-managed children.
- `【用户已确认】` Every resource generation follows a single-writer rule. MTP routes mutation to the owning LP; MPI routes fixed-size control events to the owning rank; hybrid runtime routes rank first and LP second.
- `【用户已确认】` Cross-owner terminal and destroy-completion control records preserve causal order and do not transport raw callbacks or mutable protocol objects. Unsupported runtimes fail fast.
- `【用户已确认】` Correctness ledger, optional typed trace, and optional deep diagnostic verification are independent planes. Trace presets cannot alter network semantics or lifecycle correctness.
- `【用户已确认】` Lifecycle observability uses `OFF`, `SUMMARY`, `EVENT`, and `DETAIL` levels with versioned fixed-field records and separate schema metadata. Detail sampling retains complete selected identities rather than random individual events.
- `【用户已确认】` The first writer defaults are 64 KiB chunks, a 1 MiB aggregate buffer budget per execution owner with bounded pressure flushing, and at most 1024 open trace files per process subject to the platform soft limit. These defaults remain performance-validation targets, not assumed optima.
- `【用户已确认】` Migration preserves current active modeled behavior before responsibility moves. Compatibility façades and aliases may coexist temporarily; pure moves, renames, and behavior changes remain separable; final contraction happens only after all consumers migrate.
- `【用户已确认】` Before source implementation begins, build the current `main` commit once as an immutable reference executable and record its commit, build configuration, compiler/runtime versions, linked-library facts, executable hash, case-input hashes, and output manifest. Every later slice reuses this exact reference executable for old-versus-new comparison.
- `【用户已确认】` Verification uses two bounded corpora rather than one all-purpose scenario: minimal targeted bring-up cases for fast implementation/debug loops, and several medium-complexity risk-focused acceptance cases for shared resources, concurrency, retransmission/faults, CTP/queue behavior, and parallel ownership.

## Testing Decisions

- The primary test seam is externally observable simulation behavior. Compare task completion artifacts, throughput evidence, canonical task events, relevant packet traces, deterministic event ordering, and explicit lifecycle summaries before and after each slice.
- Build the reference executable from the verified current `main@ccf41b58` once, before implementation. Preserve it with a machine-readable build manifest and SHA-256 digest; do not rebuild it per ticket.
- Before accepting any golden output, run the reference executable at least twice with identical inputs and runtime configuration. A file is eligible for raw byte-for-byte comparison only after the reference runs prove that file is itself byte-stable.
- For byte-stable, user-visible semantic artifacts, old and new output must be byte-for-byte identical. This includes stable task statistics, canonical task events, throughput evidence, and deterministic protocol traces selected by the acceptance manifest.
- If the reference executable proves a raw artifact is nondeterministic, preserve the raw files, report the instability, and compare a documented canonical representation byte-for-byte. Canonicalization may reorder only fields whose order is already non-semantic; it cannot remove protocol events, values, or unexplained differences.
- New lifecycle-only summaries, schema metadata, and detailed lifecycle records have no old-version counterpart. Validate them against schema, generation, balance, terminal-outcome, and causal-order invariants while independently requiring existing network artifacts to remain byte-identical.
- Maintain an explicit exception manifest for intentional behavior changes. Each exception records the old bytes, new bytes, Specification or characterization evidence, and approval; an unexplained mismatch always fails acceptance.
- Minimal bring-up cases should isolate one behavior with the smallest topology and event set that can trigger it. Medium-complexity acceptance cases should each combine only a few interacting risks, such as shared-TP READ/WRITE concurrency, multi-TP traffic with deterministic loss/retransmission, CTP Entity/VL queue pressure, or a small cross-owner runtime. Do not replace these with one large scenario that is difficult to diagnose.
- The first tracer bullet is a single-threaded, single-RTP-TP operation that covers packet birth, queue/link transfer without recounting, endpoint consumption or explicit drop, generation-ledger balance, and unchanged network results.
- Existing module-level tests are characterization seams for behavior that is not practical to isolate at full simulation level: TP reservation and inbound validation, WQE completion, transaction response semantics, RTP PSN/retransmission behavior, CTP admission and priority, routing selection, switch allocator order, VOQ allocation, PFC/CBFC thresholds, queue occupancy, and trace gating.
- Tests assert protocol outcomes, manager-visible lifecycle state, typed counters, callbacks, and emitted evidence. They do not assert private container layout, iterator identity, allocation addresses, or helper call order.
- Every lifecycle counter has underflow, stale-generation, retired-resource mutation, and non-zero drain-blocking coverage. Every finite child has success, failure/cancel/drop where modeled, duplicate terminal, and simulation-stop coverage.
- Packet tests cover DATA and control packets, endpoint consume, route failure, ingress/egress admission failure, fault drop, invalid endpoint, delayed transfer, physical fork, retransmission attempt, local link, remote link, and forced stop. Transfer never changes the count; physical birth/fork changes it once; terminal changes it once.
- WQE tests cover WRITE, READ, response-bearing operations, out-of-order response progress, success, failure, cancellation, flush, duplicate outcome, stale Jetty generation, and atomic pre-admission failure.
- RTP tests cover direct TP, multi-segment TPG, shared direct-plus-TPG use, temporary capacity exhaustion, no eligible member, member retirement, GBN, selective retransmission, ACK/SACK retirement, loss/reorder/timeout, CAQM, DCQCN, and long-idle reuse without resetting generation or required protocol continuity.
- Explicit teardown tests cover idle endpoint, active work, repeated request, mode escalation, pending callback exactly once, late old-generation packet, drain-only endpoint, simultaneous two-sided request, peer failure, and forced simulation stop.
- CTP tests cover lossless-lower-layer WRITE/READ completion, operation-specific single-packet constraints, Entity/congestion-key isolation and sharing, queue priority, queue registration balance, transaction cleanup, and the explicit unresolved behavior when packet loss has no modeled recovery/error outcome.
- LDST tests cover LOAD/STORE completion, duplicate/unknown ACK rejection, tracked-event resolution, map balance, and forced-stop outcomes without changing thread selection or address semantics.
- Routing and Datalink tests preserve route tie-breaks, packet-spray/adaptive outcomes, CBFC/PFC thresholds, control-frame behavior, allocator ordering, queue occupancy, TypeId/attribute names, and existing trace timing while measuring allocation count on hot paths.
- `【待验证】` MTP/MPI/hybrid tests compare single-thread results, inject delayed/duplicate/out-of-order control records, verify owner sequence and exactly-once terminal application, reject remote mutation, and verify teardown completion cannot overtake drain-only slot creation.
- `【待验证】` Descriptor-based RTP Tx buffer tests compare reconstructed packet bytes and headers across supported opcodes and extension headers, then exercise loss, reorder, timeout, cumulative ACK, selective ACK, and range retirement.
- `【待验证】` Ready-source scheduling tests preserve current short-operation start distribution, source fairness, backpressure, capacity wakeup coalescing, throughput, and starvation bounds while measuring event rate.
- `【待验证】` Routing/flow-control responsibility moves require old-versus-new trace/result characterization, allocation counts, and forwarding benchmarks before old owners are removed.
- `【待验证】` Performance validation measures correctness accounting, deep verification, lifecycle detail trace, storage policies, and writer settings separately across sequential-unique, burst-concurrent, loss/retransmission, CTP, LDST, multi-TP, MTP, MPI, and mixed workloads. Report wall time, event count, allocation/object count, peak RSS, trace bytes, syscall count, descriptor pressure, and scaling.
- Trace-off, summary, event, and detail runs must produce identical network results. Parsers reject missing metadata, unsupported schema versions, unknown enum mappings, and inconsistent MPI-rank schemas.

## Out of Scope

- Defining a new UB wire-level TP creation, close, reset, or teardown protocol.
- Automatically closing TP channels from workload completion, idle timeout, host RSS, allocator pressure, or C++ reference count.
- Automatically reclaiming topology-scoped nodes, switches, ports, links, routing tables, queue managers, allocators, or base flow-control objects during a simulation.
- Inventing CTP channel generations, TPNs, PSNs, transport acknowledgements, retransmission, or a CTP root destroy API.
- Implementing missing CTP lower-layer reliability, a new transaction error policy, or complete CTP congestion control as part of lifecycle refactoring.
- Implementing ROT, ROL, UNO, new Jetty-group scheduling, new LDST thread/address behavior, or new transport/routing/queue algorithms.
- Making every empty cacheable object release its capacity. Storage retention remains local and benchmark-driven.
- Changing existing trace file names, parser input contracts, network attributes, or TypeId names unless a separately approved artifact-contract migration requires it.
- Treating minimum peak RSS as the sole success criterion or trading protocol correctness, failure visibility, or parallel causality for memory reduction.

## Further Notes

- The UB Base Specification is authoritative for protocol terminology and modeled constraints. WQE, generation tags, lifecycle states, coordinator leases, explicit simulator-local teardown, and storage policies are ns-3-UB modeling constructs and must not be presented as wire protocol requirements.
- Current modeled behavior is the compatibility baseline, not proof that every current behavior is normative. Any intentional change requires a separate record of old behavior, new behavior, Specification evidence or characterization evidence, and regression results.
- Packet loss without a modeled recovery or error outcome can legitimately keep an operation active. Lifecycle code must expose that unresolved work rather than force balance with a timeout.
- Normal teardown requires finite operation and packet balance. Forced simulator stop records explicit residual outcomes and must not report them as successful completion.
- No source implementation is part of this specification publication task.
