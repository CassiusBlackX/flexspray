# RTP/TP Runtime Storage Owner Attribution

Status: 可执行

## Problem

The guarded MTP16 prefix probe shows retained RSS scaling with completed unique endpoint pairs, but
RSS alone cannot identify the retaining owner. A case-end diagnostic snapshot is required before
choosing endpoint teardown, backing-state compaction, or no additional reclamation.

## Scope

- Capture one read-only process snapshot after `Simulator::Run()` returns and before runtime
  destruction.
- Report canonical RTP connections, resident TP endpoints, resident Function WQEs, resident
  Transaction operations and segments, active LDST tasks and segments, retained TrafficGen task
  definitions, lifecycle storage by resource kind, and trace-writer storage/statistics.
- Write a versioned CSV only when an explicit quick-example option supplies an output path.
- Keep all counters owner-derived. The snapshot must not scan traffic rows or become a second
  lifecycle ledger.
- Keep default output, network behavior, event order, trace behavior, and destroy behavior unchanged.

## Semantics

- `canonical_connections` counts only the canonical connection owner container, never endpoint
  views.
- `resident_tp_endpoints` includes active and draining materialized endpoints; it does not mean
  transmitting TPs.
- `resident_wqes`, `resident_operations`, and `resident_segments` include manager-owned state that
  has not yet reached terminal erase. Terminal-pending operations remain resident.
- TrafficGen task definitions and completed task states are retained workload storage and are not
  labeled active WQEs.
- Lifecycle totals retain the existing compact-history semantics and add the same storage fields per
  `UbLifecycleResourceKind`.
- Trace writer current bytes are sampled before teardown flushing; cumulative and peak statistics are
  copied without changing writer state.

## Acceptance

- Focused tests cover the promoted owner getters, lifecycle kind breakdown, trace-writer snapshot,
  and quick-example CSV output.
- A focused MTP build and the relevant UnifiedBus test suites pass.
- Fresh `p001`, `p004`, and `p016` MTP16 runs execute serially with the existing 10 GiB RSS and
  900-second guards, each writing a non-colliding owner snapshot.
- Results compare owner counts and per-pair slopes before any reclamation policy is proposed.

## Out Of Scope

- Automatic TP endpoint or connection destruction.
- Cross-LP scans while workers are running.
- Estimating object bytes from container counts.
- Running `p064` solely to obtain another RSS point.
