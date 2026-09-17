# S03-default-local

## Goal

Verify explicit-on versus implicit-default lifecycle equivalence for `S03-dwrr-fc-block`.

## Frozen Scenario

- Fixture: `../20260802-main-next-controlled-comparison/fixtures/S03-dwrr-fc-block/legacy`.
- Runtime mode: `legacy-csv`; transport-channel mode remains on-demand.
- MTP threads: `0`; RNG run: `1`; dependency visibility delay: `100ns`.
- Preserve the fixture topology, traffic, routing, allocation delay, forwarding delay, link rate and link delay.

## Lifecycle Profile

- Both cleanup attributes are omitted; preflight must resolve both runtime defaults to `true`.
- The paired row uses the same source revision, binary bytes and fixture bytes.

## Oracle

- Equal accepted/completed task counts, bytes, opcodes and terminal behavior.
- No unplanned drop, deadlock, teardown error or stale driver-owned resource.

## Execution

- Status: pending; simulation execution requires the package approval gate.
