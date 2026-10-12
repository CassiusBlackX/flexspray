# Allow Failure-specific Routing Detours

Status: Accepted and implemented on 2026-09-24 (ticket 03).

## Context

In the selected two-tier Clos topology, a spine loses its only shortest output toward a leaf when
their direct link fails. Filtering that output alone leaves no route for retained packets, new
traffic, or LastAck. A legal alternate route first uses a non-shortest output through another leaf.
Ordinary shortest-only lookup currently rejects that output, consistently with ADR 0002.

## Decision

Add a shared recovery-aware routing entry point. It reuses candidate generation and final selectors,
first tries the ordinary permitted route, and permits a configured healthy non-shortest candidate
only when locally detected failed outputs removed every otherwise legal shortest candidate.

Apply this rule to retained recovery traffic, new traffic affected by the failed output, and routed
LastAck. Preserve original-ingress exclusion, exact endpoint constraints and explicit output
allowlists. After a detour, retain the existing mechanism that constrains subsequent hops to shortest
paths. Keep the selected output across admission retries so capacity wakeups do not select again.

Missing route records or constraints that would forbid forwarding even without the failure do not
authorize a detour. Preparation must validate reachability and absence of forwarding loops under
the actual route candidates and packet routing state, including both LastAck directions.

## Relationship to ADR 0002

This is an explicit extension for detected-failure recovery. Ordinary shortest-only APIs retain
their existing behavior and tests. No routing representation acquires different semantics through
an implicit fallback; each supported representation feeds the shared recovery-aware policy.

## Consequences

The two-tier Clos acceptance case can recover a leaf-spine failure without adding parallel backup
links or enabling unrestricted non-shortest routing. Cases must provide and validate the necessary
non-shortest candidates, including routes whose destinations are switches. A physical alternate
path alone is insufficient evidence that the configured packet forwarding can use it.

## Implementation and evidence

`UbRoutingProcess::GetOutPortAfterFailure` explicitly opts into the policy; ordinary
`GetOutPort` and exact/restricted APIs preserve ADR 0002. Both entries share candidate lookup,
failed-output filtering and the final selector. Table/CSV, compressed next-hop index and rule
candidates pass the same endpoint/allow-list/ingress tests. Switch URMA, LDST, CTP and LastAck
call the common entry. Existing force-shortest behavior applies after data detours.

The production fixture uses three leaves and two spines: with only two leaves, a packet
arriving at the failed spine from the other leaf would have to return through its ingress,
which remains forbidden. Six actual LastAck copies serialize across three hops each,
and retained plus newly arriving data complete in both directions.
`validate_routes.py` checks the supported single-port CSV cases before execution; it explores
every configured candidate and rejects missing routes, host transit and forwarding loops.
It does not replace actual packet-path acceptance evidence.
