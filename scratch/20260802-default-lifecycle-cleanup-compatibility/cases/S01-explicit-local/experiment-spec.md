# S01-explicit-local

## Goal

Verify explicit-on versus implicit-default lifecycle equivalence for `S01-single-rtp`.

## Frozen Scenario

- Fixture: `../20260802-main-next-controlled-comparison/fixtures/S01-single-rtp/legacy`.
- Runtime mode: `legacy-csv`; transport-channel mode remains on-demand.
- MTP threads: `0`; RNG run: `1`; dependency visibility delay: `100ns`.
- Preserve the fixture topology, traffic, routing, allocation delay, forwarding delay, link rate and link delay.

## Lifecycle Profile

- Both cleanup attributes are explicitly set to `true`.
- The paired row uses the same source revision, binary bytes and fixture bytes.

## Oracle

- Equal accepted/completed task counts, bytes, opcodes and terminal behavior.
- No unplanned drop, deadlock, teardown error or stale driver-owned resource.

## Execution

- Status: pending; simulation execution requires the package approval gate.
