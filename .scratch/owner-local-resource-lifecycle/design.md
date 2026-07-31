# Owner-local explicit modeled-resource destroy

Status: confirmed

## Goal

Preserve UnifiedBus network behavior across local, MTP, MPI, and hybrid execution while removing
simulator-global packet bookkeeping and simulator-only TP backing dormancy from correctness and
resource lifetime.

Resource closure must represent a network, task, phase, collective, or management decision. Host
memory is a reason for offering a useful explicit interface, but never evidence that an entity is
safe to close.

## Deliberately Small Scope

The first implementation adds customer convenience policy for two public destroy modules and
migrates the existing RTP TPG destroy module off simulator-global lifecycle state:

| Entity | Lifetime authority | Public interface | Owner-specific readiness |
| --- | --- | --- | --- |
| Jetty | URMA client or a driver honoring a declared task scope | `UbFunction::DestroyJetty` | `UbJettyManager::IsDrainReady` over accepted WQEs, transaction work, events, and bindings |
| TP connection | Connection owner or the standard driver for reservations it created from a complete task plan | `TpConnectionManager::DestroyConnection` | both transport endpoint owners complete their transport-mode-specific close |
| RTP TPG | TPG owner | `UbController::DestroyRtpTpGroup` | transaction operations, admitted segments, selection activity, admission bindings, and member records |

WQE, transaction operation/segment, CTP, LDST, allocator work, port/VL state, queue entries, timers,
and scheduled actions are not promoted into generic lifecycle participants. Existing owner state
contributes to the relevant Jetty, TP connection, or TPG readiness query. This keeps lifecycle
complexity behind the owning modules instead of spreading a new interface through packet and
protocol handlers.

## First-Principles Model

Packet movement is represented by packet objects and scheduled network events. The packet origin
does not need a simulator-global record of the packet's location or terminal result. Optional logs
record enqueue, dequeue, transmit, receive, consume, or drop after the local action and are never a
destroy gate.

Resource idleness is ordinary active state. Closure begins only when an entity lifetime authority
calls an explicit destroy interface. A close request has two effects:

1. reject new externally initiated work for that entity;
2. continue all packet and protocol work required by work accepted before close.

This distinction is semantic, not a runtime `root operation` or lineage abstraction. Existing WQE,
transaction, PSN, ACK, retransmission, timer, queue, allocator, and binding state already tells each
owner whether accepted work remains.

No generic `BeginWithdrawal`, `IsQuiescent`, `OnRetired`, participant registry, or
`MarkQuiescent` state is introduced. Each owner recomputes its own readiness predicate. State-change
notifications only ask that owner to reevaluate; they carry no lifecycle reference count.

## Interface Shape

### Jetty

The existing public seam is retained:

```cpp
UbDestroyStatus DestroyJetty(uint32_t jettyNum,
                             UbDestroyReason reason,
                             UbDestroyCallback callback = {});
```

`UbJettyManager` owns admission closure, drain evaluation, final map/index removal, and callback
ordering. Ordinary transaction code continues to perform normal work and calls
`NotifyJettyProgress(jettyNum)` at existing progress points. Callers do not inspect manager states.

The owner-specific predicate is conceptually:

```cpp
bool UbJettyManager::IsDrainReady(const JettyRecord& record) const
{
    return record.wqes.empty() &&
           !transaction->HasOperationForJetty(record.jetty->GetJettyNum()) &&
           !trackedEvents.HasPending(record.identity) &&
           record.bindingsAreReleased;
}
```

The implementation must reuse actual existing state and may have more transport-mode-specific
details. It must not mirror those facts into another ledger.

### TP connection

The existing public seam is retained:

```cpp
UbDestroyStatus DestroyConnection(uint32_t localNodeId,
                                  uint32_t localTpn,
                                  UbDestroyReason reason,
                                  UbDestroyCallback callback = {});
```

The TP connection manager owns the complete cross-owner close implementation. Its internal messages,
state ordering, tombstone installation, MTP/MPI routing, and callback aggregation are private. No
packet, transaction, allocator, or application caller understands them.

The endpoint transport owner exposes a narrow internal seam, named in transport language rather
than generic lifecycle language:

```cpp
void FreezeNewTransportWork(tpn, generation);
bool IsSafeForExplicitDestroy(tpn, generation) const;
void DestroyEndpoint(tpn, generation, callback);
```

`IsSafeForExplicitDestroy` queries real transport and related owner-local state. It includes the
transport-mode-specific combination of accepted transaction references, send and receive PSN
closure, required ACK/CNP responses, retransmission state and timers, local queues, allocator work,
bindings, and scheduled actions. It never queries a global packet-location count.

An unmaterialized endpoint slot is not automatically close-ready. The endpoint owner must still
prove that no accepted ingress, response, reservation, or scheduled obligation remains and create a
drain-only endpoint when existing traffic requires normal transport handling.

The exact internal close message names are implementation details. Semantically the sequence is:

```text
explicit DestroyConnection
  -> both endpoint owners close new transport admission
  -> valid existing packet/protocol work continues
  -> each endpoint owner reaches its own close predicate
  -> reservation owner authorizes final retirement
  -> both owners replace live view with full-key quarantine
  -> callback completes after both acknowledgements
```

Control delivery must be causally ordered with the owner-local transport work required for the
close predicate. A control message arriving earlier than an outstanding modeled obligation is not
permission to retire.

### RTP TPG

The existing public seam is retained:

```cpp
UbDestroyStatus DestroyRtpTpGroup(groupIdentity, reason);
```

`UbController` owns new-operation closure, group membership, final map removal, and transaction
coordination. Destroy readiness comes from the transaction owner's operations, admitted segments,
selection activity, admission-domain bindings, and the group's member records. Existing global use
and binding leases are removed rather than replaced with another lifecycle counter. Destroying a
TPG never authorizes destruction of its member TP connections.

## Customer Policy Surface

The explicit C++ interfaces always exist. A separate default-off convenience policy may be exposed
through ordinary ns-3 attributes so a customer can ask the standard driver to invoke them.

The initial policy has two independent Boolean attributes on `ns3::UbApp`, both defaulting to
`false`:

```text
EnableTaskScopedJettyDestroy
EnablePlannedTpConnectionDestroy
```

The task-scoped Jetty policy applies only to Jetties created as `TASK_SCOPED`. It invokes destroy
after task completion is published and never touches `REUSABLE` Jetties.

The TP policy applies only to reservations created and owned by the standard traffic driver. During
input loading the driver already has the complete task plan, so it records the exact connection key
used by each accepted task. It calls `DestroyConnection` only after completion is visible for the
last task in that plan that references the connection. This is an explicit close by the application
owner, not an idle heuristic or a new runtime lineage system.

A connection merely appearing in one phase, having no current Jetty binding, or being temporarily
idle is insufficient. Any later planned use keeps it active. Preconfigured, externally created, and
customer-managed connections are never touched by this convenience policy and require a direct
destroy call. No new traffic CSV lifetime field or generic entity lifetime enum is introduced.

Policy disabled means the standard driver makes no convenience destroy calls. Direct customers may
still call the explicit interfaces. Policy enabled does not run a scanner and does not change the
owner's readiness checks.

Normal workloads should leave both policy attributes disabled. The convenience path is narrowly
intended for large multi-phase collectives or similar planned workloads that would otherwise retain
many semantically finished task-owned Jetties and materialized TP connections.

Do not retain or reinterpret `RemoveUselessTp`. Its name and task-based inference imply automatic
reclamation and its current enabled path aborts. Remove it when the scoped TP interface is added.

## Parallel Contract

- Each simulation node and its protocol entities have one execution owner.
- Packet ingress resolves the target endpoint from owner-local reservation and endpoint views.
- Packet ingress does not take the process-global TP connection control lock.
- MTP cross-owner actions use node-context scheduling and the established causal delivery rule.
- MPI serializes the modeled packet once and sends no terminal bookkeeping response to its origin.
- TP connection-control messages carry stable identifiers and immutable values, never remote
  pointers.
- Management-plane reservation and explicit-close serialization may remain low frequency and is
  separate from the packet hot path.
- Per-node, LP, and rank logs may be merged offline by simulation time and wire-visible identity.

## Ingress And Retirement

Inbound resolution has three semantic outcomes:

```text
LIVE(endpoint)  - ACTIVE or closing; process normally
RETIRED_EXACT   - exact full-key quarantine match; classify stale
INVALID         - unknown or mismatched key; preserve fatal validation
```

The quarantine record contains the full wire-visible key, not only the TPN. It contains no live TP
backing and cannot rehydrate. Because the packet header contains no generation, a quarantined TPN
is never allocated again during the same simulation. Exhaustion fails deterministically.

## Non-Blocking Memory Appendix

Memory is not the objective, trigger, implementation gate, ticket-order input, or acceptance
criterion for this feature. The measurements below only explain why narrow explicit destroy
interfaces remain useful after automatic resource reclamation is removed.

Existing completed-run evidence says:

| Checkpoint | Connections | Endpoint records | Task-scoped Jetties at end | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| p001 MTP16 compaction candidate | 1,024 | 2,048 | 0 | 203,735,040 B |
| p004 MTP16 compaction candidate | 4,096 | 8,192 | 0 | 283,820,032 B |

The completed-state increment is `80,084,992 B / 3,072 = 26,069 B` per additional directed pair.
This run had already destroyed task-scoped Jetties and compacted live TP backing, so the remaining
slope is dominated by a mixture of connection records, endpoint continuity records, secondary
indexes, lifecycle history, simulator events, and allocator/container retention. It is not the
size of `Connection`, a TP endpoint, or a Jetty.

The earlier non-compacted slope was approximately `93,525 B` per pair. The difference and the
`43.10%` p004 peak-RSS reduction prove that TP-associated retained state matters, but do not isolate
which explicit destroy interface delivers how many bytes.

The attribution experiment uses a small no-packet microprobe built once and run in fresh processes
at several counts:

- Jetty sweep: fixed topology and zero TP connections; create N empty task-scoped Jetties and take
  one current retained-state snapshot.
- Reservation sweep: no Jetties and no packet execution; create N reservations without endpoint
  materialization and take one current retained-state snapshot.
- Endpoint sweep: create N reservations with two materialized empty endpoints and take one current
  retained-state snapshot to separate reservation metadata from transport backing.

Five reversed-count repetitions produced these fitted current-RSS slopes on the current macOS arm64
release build:

| Retained state | Fitted bytes per entity | R-squared | High-count endpoint-slope range |
| --- | ---: | ---: | ---: |
| Empty task-scoped Jetty through the real manager/coordinator path | 17,254 B/Jetty | 0.99999999 | 17,254-17,256.5 B |
| Reservation plus two local views and fourteen secondary-index entries | 1,197 B/connection | 0.99990868 | 1,200.75-1,202.25 B |
| Reservation plus two materialized empty TP endpoints | 42,785 B/connection | 0.99999907 | 42,798-42,801 B |

Subtracting the reservation slope gives approximately `(42,785 - 1,197) / 2 = 20,794 B` per
materialized empty TP endpoint. This is an attribution estimate, not an allocator-exact object size.

At this measured slope, 1,024 active two-ended connections retain about `41.8 MiB`, and 10,000 retain
about `408 MiB`, before payload-bearing WQEs, packets, queues, or retransmission state. Similarly,
1,024 empty Jetties retain about `16.8 MiB`, and 10,000 retain about `165 MiB`. These examples explain
why explicit lifetime management can matter in large multi-phase collectives without turning host
memory into a destroy trigger.

ABI inline sizes are only `208 B` for `UbJetty`, `48 B` for `Connection`, and `768 B` for
`UbTransportChannel`. The main measured cost is heap backing. Both Jetty's TA SSN window and TP's
receive PSN window currently allocate 2,048 `uint64_t` slots, or `16,384 B`, even when empty. That
single allocation explains about 95 percent of the empty-Jetty slope and about 79 percent of the
endpoint-only estimate; manager records, lifecycle records, congestion control, retransmission,
maps, and allocator registration account for the remainder.

Container nodes, capacity, allocator retention, ns-3 object metadata, and referenced heap objects
are not included in `sizeof`. The probe does not execute explicit destroy and does not measure RSS
returned afterward. The fresh-process slope quantifies occupied active state and provides an upper
bound on growth that timely explicit destruction may avoid. Actual post-destroy process RSS depends
on which owner state is removed, the lightweight quarantine retained, and allocator page retention.

## Compatibility And Test Seam

Every existing successful compatibility case is compared with frozen baseline commit
`68bbce1310dad539a0c053cac6cd14ac69dda5cb`. All manifest-listed canonical files, including empty
rank files, are compared as raw bytes for the same mode, case, seed, build type, and checkpoint.

The delayed-destroy old path has no successful baseline. A new no-destroy control and destroy
treatment admit identical work and must emit byte-identical canonical network output. Focused tests
then assert callback completion, close ordering, exact retired-key handling, fatal mismatch, and TPN
quarantine through production parser and ingress seams.

Policy-on and policy-off runs for scoped resources must be byte-identical when the resource has no
later declared use. This proves destroy is after semantic lifetime, not that destroy is invisible in
arbitrary reuse workloads.

## Prototype Evidence

Removing packet birth/tag/attempt bookkeeping, packet terminal resolution, and origin-return
control preserved the exercised normal canonical outputs in local, MTP, MPI, and hybrid modes.
Two reversed-order long pairs reduced retired instructions by `3.77%` and `4.01%`, median `3.89%`.

Owner-local RTP ingress worked on normal paths. Delayed destroy then exposed early reservation view
erasure and immediate TPN reuse. That failure is why connection close remains a private,
entity-specific protocol rather than a nullable lookup or generic participant framework.

## Implementation Order

1. Implement owner-local three-result TP ingress, full-key quarantine, and TPN non-reuse while
   retaining existing packet bookkeeping as a temporary compatibility scaffold.
2. Repair private TP connection close so valid closing traffic drains in local, MTP, MPI, and hybrid.
3. Migrate Jetty, RTP TPG, CTP, and LDST correctness dependencies to their existing owner-local state.
4. Remove TP backing dormancy and automatic backing reclaim before its packet-idle safety gate is
   removed.
5. Remove packet birth, transfer, terminal, origin-return, and in-flight bookkeeping across all packet
   families, then delete the now-unused global lifecycle machinery.
6. Add the default-off standard-driver destroy policy and run final byte-level compatibility plus
   approximately 30-second retired-instruction measurements.
