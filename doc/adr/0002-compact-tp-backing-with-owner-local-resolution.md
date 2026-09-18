# Compact TP Backing With Owner-Local Resolution

## Status

Accepted

## Context

Phased RTP workloads can retain a materialized TP endpoint for every completed communication pair.
The logical endpoint must remain valid because its reservation, TPN, lifecycle identity, generation,
and protocol continuity may be used again. Destroying the endpoint or tearing down its connection
when one workload task completes would change those semantics.

The simulator therefore needs to separate three lifetimes:

- a logical TP endpoint exists while its reservation generation is valid;
- live TP backing exists only while the endpoint needs its complete runtime object graph; and
- explicit destroy ends the endpoint generation after the destroy drain contract is satisfied.

Task completion also has two observations. `LOCAL_COMPLETE` is the owner-side completion of the
task's final transport work and is the observation used for transport latency and FCT.
`COMPLETE_VISIBLE` is the committed workload-state transition used for canonical completion,
dependency release, completed-task accounting, and lifecycle cleanup. These observations may have
the same simulation timestamp, but their causal order must not be collapsed.

## Decision

### Backing lifecycle

Use **TP backing compaction** for the storage operation and **compact TP endpoint** for a logical
endpoint without live backing. The implementation enum may call this state `DORMANT`, but it is a
simulator-local storage state, not disk paging, protocol teardown, or logical closure.

The live/compact transition preserves the reservation, TPN, endpoint lifecycle identity,
generation, binding identities, and session continuity. It does not reuse or renumber a TPN, create
a new reservation, or start a new RTP session. A compact snapshot retains the transport and
congestion-control state required to make later use equivalent to reuse after a long idle period.

Reclaim is fail-closed. The manager compacts backing only when the owner-local lifecycle, packet,
transaction, allocator, transport-work, timer, retransmission, queue, and congestion-control checks
all prove that the endpoint is eligible. A failed check leaves the live backing unchanged. Duplicate
attempts are harmless, and retries are driven by later owner-local state transitions rather than a
polling timer.

### Completion and cleanup order

The required order is:

1. observe `LOCAL_COMPLETE` on the task owner;
2. submit and commit `COMPLETE_VISIBLE`;
3. record canonical completion and release dependent work; and
4. deliver task-scoped Jetty cleanup to its owner, which may then trigger TP backing compaction.

Submitting a completion event is not `COMPLETE_VISIBLE`; the transition is visible only when the
completion application path commits the workload state. Cleanup is asynchronous with respect to
dependency progress and is never a completion barrier.

In a non-partitioned run, cleanup is a later owner-context event at the same simulation timestamp.
In partitioned MTP, completion publication schedules cleanup through causal owner delivery using the
owner node context and the runtime's causal delivery delay. The receiving owner event resolves the
application again from stable node and application identifiers. A worker must not retain or borrow
a remote LP's mutable application or TP object.

### Query and resolution interfaces

The TP channel manager separates observation from use:

| Interface | Contract |
| --- | --- |
| `Contains(tpn)` | Reports whether the logical endpoint record exists, including compact backing. |
| `PeekLiveTp(tpn)` | Observes live backing without restoration; returns null for missing or compact endpoints. |
| `GetTp(tpn)` | Legacy source-compatible alias of `PeekLiveTp`; it is also observation-only. |
| `GetTpnMap()` | Returns an observation snapshot; a compact endpoint is represented by its TPN with null backing. |
| `ResolveLiveTp(tpn, identity)` | Returns or restores live backing only for the exact active identity on the endpoint execution owner. |

`ResolveLiveTp` is the only synchronous restoration seam. Sender, inbound-packet, direct-TP, and TPG
consumer paths resolve before admitting work or consuming a packet. A wrong or stale identity, a
draining endpoint, disposal, or non-owner execution returns null without mutating storage.

Keeping `GetTp` observation-only preserves a deterministic `const` query and prevents an observer
from restoring mutable state on a remote owner. This preserves the existing C++ signature, but code
that treats `Contains(tpn) == true` as proof that `GetTp(tpn)` is non-null must migrate to the
identity-bearing resolution path when it intends to use the TP.

### Congestion-control contract

Congestion-control participation is explicit and fail-closed. The base implementation reports
`INELIGIBLE`; an unsupported or future algorithm continues to simulate with live backing and simply
does not receive this memory benefit.

An algorithm may report eligibility only after it defines:

- the state snapshot required for later control decisions;
- restoration of that state into a new backing object;
- any sender idle-reset semantics already defined by the algorithm;
- drain or retained-deadline behavior for scheduled events and timers; and
- continuity tests across compact backing and restoration.

A stateless algorithm must still opt in explicitly. Algorithms with windows, history, counters, or
scheduled events must not infer eligibility from temporary idleness or partially reset their state.

## Considered Alternatives

- **Destroy the endpoint or tear down the connection at task completion**: rejected because task
  lifetime is not TP session lifetime and this would permit identity, generation, TPN, and protocol
  continuity changes.
- **Restore backing implicitly from `GetTp`**: rejected because an observation-only call could mutate
  storage, take owner-only locks, or attempt restoration from a public or foreign LP. Callers that
  consume a TP use identity-bearing `ResolveLiveTp` instead.
- **Return a stable handle or facade from every TP query**: rejected for this change because it would
  replace the existing pointer API across setup, diagnostics, tests, and protocol consumers. It can
  be considered separately if stronger C++ behavior compatibility is required.
- **Borrow a remote LP object or send a new lifecycle wire message**: rejected because backing
  compaction is owner-local simulator storage policy, not modeled protocol control.
- **Expose a supported user enable flag**: rejected after acceptance because the optimization is
  intended to be transparent. An internal rollout switch does not become a case contract.

## Consequences

Normal case directories, CSV schemas, attributes, commands, trace schemas, routing, flow control,
wire behavior, TPN assignment, and endpoint generations do not change. Simulation users do not need
a migration or a new option.

Protocol extensions have a narrower compatibility boundary. `GetTp` remains source-compatible but
may return null for a compact logical endpoint; code that needs a usable channel must carry the
expected lifecycle identity and call `ResolveLiveTp` on the owner. Observation and diagnostic code
should use `PeekLiveTp` so that it cannot accidentally materialize backing.

Eligible idle RTP endpoints consume compact continuity records instead of full backing object
graphs. Ineligible endpoints remain live, so an unsupported congestion-control implementation loses
memory benefit rather than correctness. Restoration and reclaim checks add runtime work and may
contend on manager state. This decision therefore makes no claim of stable speedup.

This decision does not reduce trace volume and does not solve large-log storage. It also does not
reclaim topology-scoped resources or aggregate packet and event processing.

## Acceptance

The implementation is accepted only when tests and fixed-input differential evidence establish all
of the following:

- `LOCAL_COMPLETE` precedes committed `COMPLETE_VISIBLE`, and cleanup begins only afterward;
- dependency release does not wait for cleanup, and duplicate completion or cleanup executes once;
- partitioned MTP cleanup runs through causal owner delivery without remote mutable-object access;
- compact endpoints remain visible to `Contains` and `GetTpnMap`, while `PeekLiveTp` and legacy
  `GetTp` remain non-mutating observations;
- exact-identity owner resolution restores the same TPN and generation exactly once, while stale
  identity and non-owner resolution fail without mutation;
- drain and congestion-control ineligibility retain live backing unchanged;
- CAQM and DCQCN continuity tests pass, and an algorithm without a contract remains ineligible;
- canonical `START` and `COMPLETE_VISIBLE` outputs, task traces, and reservation digests match the
  fixed baseline byte-for-byte; and
- runtime storage snapshots satisfy logical-endpoint, live-backing, and compact-record accounting.

RSS and wall time are reported separately from correctness. RSS demonstrates storage leverage;
wall-time measurements do not become an acceptance gate or a speedup claim.
