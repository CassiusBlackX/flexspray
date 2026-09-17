# Model Switch Allocation as a Wakeup Snapshot

## Status

Accepted

## Context

The switch allocator has a configurable `AllocationTime`. A wakeup starts an allocator episode,
and the delayed callback represents completion of the arbitration computation. Selecting only in
that callback lets packets arriving during the delay enter the arbitration that was already in
progress and adds a redundant zero-delay scheduling boundary when `AllocationTime` is zero.

The allocator also advances RR and DWRR scheduling state while selecting a candidate. If the
candidate cannot enter the egress queue, treating that attempted selection as service creates
phantom progress and changes the queue retried after backpressure clears.

## Decision

An allocator wakeup snapshots one grant before `AllocationTime` begins. The callback after
`AllocationTime` commits that grant.

- A higher-priority packet arriving during `AllocationTime` participates in the next allocator
  round and cannot preempt the current grant.
- A flow-control or credit change during `AllocationTime` does not revoke the current grant. The
  eligibility check performed while selecting the grant is authoritative for that round.
- RR commits the selected ingress queue.
- DWRR freezes the selected VL and starting ingress queue, but not an entire burst. After the first
  packet, additional same-VL candidates are selected using the state visible at commit time.
- Egress capacity remains a commit-time physical constraint. If the first packet cannot enter the
  egress queue, no service occurred: RR restores its ingress cursor, while DWRR restores its
  quantum addition, ingress cursor, and VL cursor. A later wakeup retries the same logical grant.
- If DWRR commits at least one packet before a later packet in the burst encounters a full egress,
  that partial service is retained and scheduling state is not rolled back.

This allocator decision is independent of the owner-local resource-lifecycle contract in ADR-0020.
It intentionally changes allocator timing and packet ordering relative to `next`; it does not relax
the lifecycle requirement that existing successful cases remain byte-identical when only lifecycle
implementation changes.

## Consequences

The arbitration point is explicit and deterministic: packet arrivals after wakeup cannot influence
the in-progress computation. Zero `AllocationTime` can commit directly without an extra scheduled
event while preserving the same wakeup boundary.

Flow control may become limited after a grant is made, so one already granted packet can commit
during the delay. This represents completion of the decision already in flight, rather than a new
eligibility check.

Backpressure no longer advances fairness state without service. This may differ from historical
packet order after a full egress queue, but preserves RR/DWRR accounting semantics.

## Acceptance Criteria

- RR and DWRR commit the queue selected at wakeup when a higher-priority packet arrives during
  `AllocationTime`.
- RR and DWRR commit the wakeup grant when flow control becomes limited during `AllocationTime`.
- RR and DWRR retain all ingress packets and retry the same selected queue when the first commit is
  rejected by egress capacity.
- DWRR continues to evaluate later packets in a burst at commit time.
- Focused allocator, flow-control, lifecycle, TP-reservation, serial, MTP, MPI, and hybrid
  regressions pass. Performance comparisons use the current `next` base and report instructions,
  cycles, wall time, event/task sets, and completion counts separately from packet-byte identity.
