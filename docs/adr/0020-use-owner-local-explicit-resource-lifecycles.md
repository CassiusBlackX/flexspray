# Use Owner-Local Explicit Modeled-Resource Destroy

## Status

Accepted

## Context

The simulator currently uses a node-scoped lifecycle coordinator, packet tags, cross-owner packet
terminal messages, and resource leases to prove that TP channels and related resources may be
reclaimed. It also compacts idle TP backing and later restores the same logical endpoint. This adds
work to every packet, introduces non-protocol control across MTP logical processes and MPI ranks,
and makes simulator storage policy part of modeled resource lifetime.

The application already has explicit `DestroyJetty` and `DestroyConnection` interfaces, but policy
and ownership are mixed. Task-scoped Jetty cleanup is invoked automatically. The legacy
`RemoveUselessTp` option tries to infer connection lifetime from task completion and aborts when
enabled. Delayed-destroy experiments also show that a connection view can disappear before valid
network traffic completes and that a retired TPN can be reused even though packets carry no
generation.

## Decision

Modeled resource destruction is explicit, owner-local, and driven only by network, task, phase,
collective, connection, or management semantics declared by an entity lifetime authority. Resource
idleness, host RSS, memory pressure, elapsed time, a C++ reference count, or simulator-global packet
location never authorizes destruction.

The supported explicit destroy modules are Jetty, TP connection, and the existing RTP TPG. They keep
separate interfaces and entity-specific readiness predicates:

- `UbFunction::DestroyJetty` closes WQE admission and lets `UbJettyManager` finish accepted WQEs,
  transaction work, events, and bindings before final removal.
- `TpConnectionManager::DestroyConnection` closes new transport admission and privately coordinates
  both endpoint owners until their transport-specific protocol state is safe to retire. A slot is
  not close-ready merely because its endpoint backing has not been materialized.
- `UbController::DestroyRtpTpGroup` closes new group selection and uses transaction operations,
  admitted segments, selection activity, admission bindings, and member records before removal.

WQEs, transaction operations and segments, CTP and LDST state, allocator work, ports, queue
entries, timers, and scheduled actions remain state owned by their existing modules. When relevant,
the Jetty, TP, or TPG owner queries that state. They do not implement a universal lifecycle
participant interface, root-operation framework, lineage ledger, or manual lifecycle reference
counting.

A separate customer policy exposes `ns3::UbApp::EnableTaskScopedJettyDestroy` and
`ns3::UbApp::EnablePlannedTpConnectionDestroy`, both default-off. It may let the standard
application or traffic driver call the explicit interfaces. It acts only on a declared
`TASK_SCOPED` Jetty or on a TP reservation that the standard driver created and owns from its
complete task plan. The driver may close that
connection only after the last planned referencing task completes. It cannot infer lifetime from
temporary idleness, a phase boundary alone, current bindings, or last observed packet activity.
Preconfigured and externally managed entities remain active unless their customer explicitly
destroys them.

Ordinary workloads leave both policy attributes disabled. Their narrow intended use is a large
multi-phase planned workload that would otherwise retain many semantically finished task-owned
Jetties and driver-owned materialized TP connections.

Packets are not globally tracked from birth to terminal. While a TP connection closes, valid data,
ACK, CNP, retransmission, and other protocol traffic continues through the normal ingress path.
Packet ingress uses only target-owner views and does not acquire the process-global connection
control lock. Passive packet-operation logging is allowed but cannot affect protocol correctness or
resource lifetime.

The TP connection manager's cross-owner close messages are private implementation. The reservation
is retired only after both endpoint owners satisfy their owner-local transport close predicates and
atomically install full-key quarantine. Only exact packets for a retired key are stale; unknown or
mismatched keys remain errors. Retired TPNs are not reused during the simulation because the wire
header has no generation field.

TP backing dormancy, compaction, and rehydration are removed. Host-memory reduction comes from
compact data structures or from actual explicit destruction after modeled lifetime ends.

## Considered Options

- Optimize the global packet and lease ledger: rejected because observation remains part of
  correctness and every packet continues to pay bookkeeping cost.
- Keep transparent TP backing dormancy: rejected because suspend/rehydrate is a simulator memory
  behavior rather than modeled network resource management.
- Apply one participant state machine to every resource: rejected because the interface would be as
  complex as the underlying protocol state and would duplicate owner facts.
- Infer TP close from task completion or idleness: rejected because tasks can share or later reuse a
  connection and a task completion is not a network close.
- Use explicit entity-specific destroy with an opt-in scoped driver policy: accepted because the
  caller supplies lifetime semantics while each owner retains protocol authority.
- Reject valid packets as soon as close begins: rejected because traffic already admitted into the
  modeled network may be required to complete reliable transport.

## Consequences

The Jetty and TP connection owners become deeper modules: their implementations contain drain and
cross-owner close complexity, while ordinary packet and protocol callers see only normal admission,
progress, and explicit destroy interfaces.

RTP TPG destroy remains explicit but no longer depends on simulator-global use or binding leases.

The standard driver no longer performs destroy by default. Customers may opt into task-scoped Jetty
destroy and planned destroy of driver-owned TP connections; reusable or externally managed
connections see no lifetime behavior change.

Local, MTP, MPI, and hybrid execution must retain raw-byte-identical canonical output for existing
successful cases. Internal lifecycle trace schemas, packet attempt identities, compact-backing
counters, and coordinator snapshots are not compatibility contracts.

The earlier `43.10%` p004 peak-RSS reduction does not quantify Jetty or TP connection cost. A
controlled no-packet sweep measured current release-build retained-RSS slopes of approximately
`17,254 B` per empty Jetty, `1,197 B` per reservation, and `42,785 B` per reservation with two
materialized empty endpoints. The last value is occupied active state, not post-retirement
quarantine size or RSS guaranteed to return after destroy. These results justify opt-in scoped
destroy interfaces for large multi-phase workloads, not automatic or default-on reclamation. ABI
`sizeof` values are reported only as inline size and never as total resident memory.

This decision supersedes ADR-0002.

## Branch Relationship and Performance Interpretation

This section records the repository state inspected on 2026-07-30. Commit counts and branch tips
are historical evidence, not permanent properties of the branch names.

The inspected tips were:

| Branch | Commit |
| --- | --- |
| `main` | `c044d5bf1016da47ddc66ac8573eff44d228a143` |
| `next` | `b59c6872dfbef7f661a6d8a8582d9db131d1fe87` |
| `next-speedup` | `35899b11769526eba734a2753a2db2dd1f880d71` |

`main` and `next` diverged at `ccf41b58b0ba3b9711efea595732a2908e3e07cf`; at the inspected
tips, `main` had three unique commits and `next` had 178. `next-speedup` diverged from the `next`
line at `68bbce1310dad539a0c053cac6cd14ac69dda5cb`; it was one commit behind `next` and fourteen
commits ahead. The missing `next` commit was
`b59c6872 feat(traffic): integrate semantic collective schedules`.

Therefore, `next-speedup` is not a rollback to `main`, and commit distance does not make it closer
to `main`. It retains the protocol, routing, transport, owner/generation, MTP, MPI, and explicit
cross-owner close capabilities developed on `next`. Before landing, it must also absorb the latest
`next` semantic-collective change.

The narrower lifecycle and packet-hot-path interpretation is different:

| Behavior | `main` | `next` | `next-speedup` |
| --- | --- | --- | --- |
| Simulator-global packet birth-to-terminal ledger | absent | present | removed |
| Packet attempt bitmap and terminal owner response | absent | present | removed |
| TP backing dormancy and rehydration | absent | present | removed |
| Owner-local TP ingress and exact generation validation | incomplete | present | retained |
| Drain-before-destroy and paired endpoint retirement | incomplete | present | retained |
| TPN quarantine against wire-invisible generation reuse | absent | present | retained |
| Explicit default-off Jetty and planned TP destroy policy | absent | absent | present |

In this limited sense, `next-speedup` is closer to the simpler `main` execution model: ordinary
packets do not update a simulator-global lifecycle proof, and active TP backing is not suspended
and reconstructed as a host-memory optimization. It is deliberately not equivalent to `main`:
`main` can erase a TP from the allocator, transaction layer, and local map without the complete
owner-local drain, generation, peer coordination, and quarantine contract retained by
`next-speedup`.

The removable work in `next` was specifically the duplicated global packet-location proof:

```text
packet birth
  -> packet lifecycle tag and attempt identity
  -> global in-flight count and active-attempt bitmap
  -> cross-owner transfer and terminal observation
  -> terminal lookup, bitmap clear, count decrement, and callback
```

The endpoint already has protocol-authoritative destroy evidence: transaction references, pending
transport work, WQE segments, ACK and CNP queues, unacknowledged PSNs, retransmission timers,
allocator work, tracked events, and TP-group bindings. `next` checked both this owner-local state
and the simulator-global packet count. Once transparent backing reclaim and rehydration were
removed, the packet-location proof was no longer required for explicit network-semantic destroy.

Not all lifecycle bookkeeping in `next` was unnecessary. `next-speedup` retains resource identity
and generation, accepted-work and child state, tracked events, transaction and transport bindings,
paired endpoint drain, causal MPI/MTP owner control, stale-notification rejection, and TPN
quarantine. These mechanisms close correctness gaps in `main`; they are not packet observation or
automatic memory reclamation.

A same-build fixed-workload A/B for removal of the packet lifecycle hot path measured median
retired instructions down by approximately 3.89%, with byte-identical canonical output. This is
evidence for one redundant cost, not evidence that the full historical performance gap between
`next` and `main` has been eliminated. The remaining gap may include protocol features, data
structures, allocator scheduling, routing, tracing, and other changes unrelated to lifecycle.

The correct final performance comparison is a three-way, same-build experiment over `main`,
`next`, and `next-speedup` after the latter absorbs the missing `next` commit. It must hold workload,
compiler configuration, MPI/MTP mode, and run order fixed; report retired instructions, cycles,
and wall time separately; and require raw-byte-identical canonical output before attributing a
performance difference to the lifecycle design.
