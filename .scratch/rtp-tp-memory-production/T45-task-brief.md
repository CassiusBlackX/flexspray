# T45 Task Brief: Deduplicate Canonical Connection Indexes

Status: ready-for-agent

## Authority And Scope

- Ticket: `tickets.md`, Task 45.
- Feature contract: `.scratch/rtp-tp-memory-production/PRD.md`.
- Starting commit: `8c8dccc` on `codex/rtp-tp-memory-compaction`.
- This ticket changes connection storage and its storage attribution only. It does not compact TP
  reservation records, change endpoint materialization, add dormant TP backing, or retire Jetties.
- Existing dirty application, traffic-generator, prototype, and performance-evidence files are
  excluded and must not be modified, cleaned, staged, or used as the test oracle.

## Confirmed Public Test Seam

Exercise `TpConnectionManager` only through its existing public lookup, reservation, destroy, reuse,
and storage-snapshot APIs in the Unified Bus test suites. Tests compare returned values and order;
they do not inspect private containers or depend on container addresses.

## Storage Contract

- The owner manager's `ConnectionLifecycleRecord::connection` remains the one authoritative
  canonical value for lifecycle-managed reservations.
- Each installed local endpoint view has one authoritative `Connection`, keyed by local node, local
  TPN, and validated against reservation generation.
- Node, peer, peer/priority, peer/local-port, both-port, local-TPN, and public-list ordering structures
  store only stable value references. They store no `Connection` copies, pointers, C++ references,
  or iterators into erasable or relocatable containers.
- `GetAllConnections()` keeps its public signature. Its compatibility vector is reconstructed lazily
  from authoritative values and invalidated on mutation; it is not populated by the simulation's
  normal reservation/query path.
- Existing insertion order, endpoint orientation, full-criteria matching, shortest/multipath order,
  erase behavior, TPN reuse, and generation checks remain unchanged.
- `Clear()` and `ClearNodeConnections()` preserve their existing boundary; this ticket does not add
  lifecycle retirement or alter canonical destroy state.
- Runtime storage snapshots retain `canonical_connections` and add separate local-view and secondary
  index-entry counts without changing the CSV schema columns or meanings of existing rows.

## Planned TDD Slices

1. Characterize every public lookup and ordered result across two local views and multiple paths.
2. Replace repeated index values with generation-aware local references and a single resolver.
3. Characterize erase, TPN reuse, and generation replacement through public APIs.
4. Attribute canonical records, authoritative local views, and secondary index entries separately in
   the runtime storage snapshot.

## Batch Reuse Map

Pre-refactor commit: `8c8dccc`

Semantic batch: replace duplicated connection values in lookup indexes with generation-aware local
references while preserving the existing reservation and lifecycle paths.

Old active symbols: `AddConnection`, `AddUnilateralConnection`, `BuildIndexesForNode`, public lookup
methods, `EraseConnectionView`, `ClearNodeConnections`, `Clear`, and `GetAllConnections`.

Old call paths: traffic reservation installs one owner view and one receiver view; TP resolution and
inbound validation query those views; explicit connection destroy erases both views.

Continuous code blocks reused: existing lookup loops, orientation logic, index keys, insertion order,
TPN bookkeeping, lifecycle callbacks, and canonical connection records.

Reuse mode: replace only stored index values and resolve them through one manager-local helper.

Required lifecycle adaptations: every reference carries the reservation generation; erase matches the
full connection lifecycle identity; canonical connection state and destroy sequencing are unchanged.

Preserved ordering and observable behavior: public connection orientation, insertion and multipath
order, metrics, all lookup results, TPN reuse, replacement generation, and CSV schema.

Existing characterization: TP reservation tests already cover lazy endpoint materialization,
explicit endpoint/connection destroy, TPN reuse, and replacement generation.

Explicit behavior changes: runtime storage snapshots add authoritative local-view and secondary
index-entry rows; secondary indexes no longer contain full `Connection` values.

Excluded paths: endpoint backing reclamation, reservation compaction, Jetty lifetime, app and traffic
generator behavior, scale checkpoints, and prototype cleanup.

Focused validation: `unified-bus-tp-reservation`, `unified-bus-runtime-storage-snapshot`, targeted
module build, changed-line format check, and two-axis review.

Re-approval triggers: any change to public lookup semantics, reservation ownership, generation or TPN
allocation, lifecycle destroy ordering, endpoint materialization, or CSV columns.

## Execution Ledger

- Diff scope: connection manager implementation/header, runtime snapshot implementation/header,
  Unified Bus tests, and this task brief only.
- RED: runtime snapshot expected `authoritative_local_connection_views` and
  `secondary_connection_index_entries` before the writer emitted them.
- GREEN: public lookup compatibility covers derived node managers, both `GetConnection` overloads,
  node, neighbor, peer, priority, ports, full criteria, local TPN, public orientation/order, and the
  existing `ClearNodeConnections` boundary. Existing connection destroy coverage verifies real TPN
  release, reuse, and replacement generation.
- Build: `ninja -C cmake-cache unified-bus-test` passed; only two existing unused-helper warnings.
- Focused tests: `unified-bus-tp-reservation` passed 13/13.
- System test: `unified-bus-runtime-storage-snapshot` passed 1/1 after building
  `scratch_ub-quick-example` (the configured Ninja target name).
- Scale and frozen canonical differential were not run because T45 is not an integration milestone.

## Validation Contract

Run serially:

```text
ninja -C cmake-cache unified-bus-test
DYLD_LIBRARY_PATH="$PWD/build/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
  ./build/utils/ns3.44-test-runner --test-name=unified-bus --verbose
ninja -C cmake-cache scratch_ub-quick-example
DYLD_LIBRARY_PATH="$PWD/build/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
  ./build/utils/ns3.44-test-runner --test-name=unified-bus-runtime-storage-snapshot --verbose
```

Before commit, stage only intended T45 files and run cached diff/format checks. Do not run a scale
simulation, full repository test suite, or guarded memory checkpoint in T45.
