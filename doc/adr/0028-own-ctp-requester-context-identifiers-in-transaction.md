# Own CTP requester-context identifiers in the transaction layer

Status: Implemented; see [integration evidence](../implementation/ctp-full-tah-06.md).

CFG6 CTP requires full transaction headers with a 20-bit requester-context
identifier, while the existing DDIE/Iodie interface accepts 24-bit caller Jetty
IDs. For the CTP correction, preserve the public Jetty-ID range and let
`UbTransaction` own a separate RCID-to-requester-context/Jetty association;
DDIE/Iodie continue submitting Jetty IDs, and the CTP carrier encodes and decodes
the wire identity. This keeps protocol resource allocation inside unified-bus
and avoids silently truncating caller IDs or reducing their supported range.

Allocate the RCID at the first successful CTP preparation and retain the
association for that Jetty generation, across WQEs, idle periods, different
destinations and idle unprepare/reprepare. Explicit Jetty destruction ends the
association after outstanding work has been drained or cleaned up through the
existing lifecycle owners. Recreating the same caller Jetty ID creates a new
requester context. This separates requester identity from temporary transport
preparation and avoids changing identity during ordinary reuse.

Use one CTP RCID namespace per node-local `UbTransaction`. Different Jetty
generations with retained associations have distinct RCIDs across source
Entities, VLs and destinations; different nodes may use the same numeric value.
This simulator allocation policy keeps requester lookup independent of transport
bindings, while sharing the 20-bit space across the node's CTP requesters.

Do not reuse an allocated CTP RCID within one simulation run. Destroying a
Jetty releases the drained live association and its backing, while preserving
the fact that its numeric RCID has been used. New Jetty generations require
previously unused values. Exhaustion explicitly rejects new CTP preparation;
it neither wraps the allocator nor waits for an identifier to become reusable.
Existing requester associations remain usable.

This bounds cumulative allocations per node per run, not only concurrently
live contexts. It avoids matching a late response to a new requester with the
same RCID and TASSN, because the wire identity does not carry the local resource
generation. This follows ADR 0020's existing reasoning for retired TPNs without
adding a simulator-global packet ledger or a guessed quarantine interval.

Allocate the full 20-bit SQ RCID range from `0` through `0xfffff` in increasing
order. Keep a wider local next-ID cursor, using `0x100000` for exhaustion;
represent a missing association separately from valid wire IDs. This allows
1,048,576 successful new associations per node per run. Numeric zero is usable;
the specification's SQ RCID field description does not reserve it.

Complete recoverable validation before consuming a new ID. Publish the requester
association and CTP preparation together without exposing partial state through
callbacks. Rejected preparation leaves the cursor and existing associations and
bindings unchanged. Repeated preparation or valid idle re-preparation uses the
retained RCID even after new IDs are exhausted. A successful preparation commits
the allocation: a later rejected WQE does not undo that allocation or make its
ID reusable.

Require successful keyed CTP preparation before WQE admission across UbApp,
Iodie and direct UB callers. Preparation establishes the validated requester
parameters, RCID and source Entity/VL binding; submission then commits WQE
identity, sequence reservations and dispatch work. Unprepared submission is
explicitly rejected before those side effects, without implicit preparation or
rebinding. Repeated valid preparation remains idempotent.

Adapt UbApp's original submit-then-StartJetty sequence to this contract. Its
one-argument null-check helper does not establish preparation. Retained
convenience helpers must use the same owner path. Preserve the user's original
ub-quick-example workload/topology, CLI and task-completion behavior, and require its
existing system tests and CTP reference cases to pass alongside DDIE/Iodie
regressions. The user's later parameter-retirement decision in ADR 0031 permits
removing the obsolete key from current case snapshots. Update direct-call test
setup without weakening progress assertions.

Return a structured preparation result distinguishing RCID exhaustion, invalid
requester-window configuration and binding/lifecycle failures. Any retained bool
entry point wraps that same result-producing path. Iodie maps RCID exhaustion
to its existing `IdSpaceExhausted` error with a specific CTP RCID message;
DDIE consumes it through the existing rejection path. Retain cleanup of a newly
created but unsuccessfully prepared Jetty without disturbing preexisting
requesters or accepted work. Exact helper and result names remain implementation
details.

The monotonic cursor preserves used-ID history without keeping every retired
context: an ID below the cursor and absent from the retained association map
after normal destruction is retired; an ID at or above the cursor has never
been allocated. Idle unprepare retains the association. Contradictory owner
indexes remain internal errors under ADR 0030. TASSN wrap and requester progress
are specified separately in ADR 0029; RTP mapping changes remain outside scope.

Protocol basis: UB Base Specification 2.1.0, section 7.2.1 (pp. 222, 226),
section 8.2.3 (p. 279), and appendix B.2.4.1 (p. 406). Related ownership boundaries
are recorded in ADRs 0024 and 0027.
