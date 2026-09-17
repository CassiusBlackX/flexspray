# T46 Task Brief: Compact Unmaterialized TP Reservations

Status: complete

## Authority And Scope

- Ticket: `tickets.md`, Task 46.
- Feature contract: `.scratch/rtp-tp-memory-production/PRD.md`.
- Pre-refactor commit: `05dadc3` on `codex/rtp-tp-memory-compaction`.
- Blocker T45 is complete. T46 compacts canonical reservation lifecycle metadata only.
- Dormant TP backing, resolve/rehydrate, allocator behavior, Jetty lifetime, app/traffic behavior, and
  prototype cleanup are excluded.

## Confirmed Public Test Seam

Use `TpConnectionManager` reservation, lookup, materialization, destroy, lifecycle snapshot, and
storage snapshot APIs through `unified-bus-tp-reservation`. No test may inspect private containers,
object addresses, or `sizeof` implementation types.

## Batch Reuse Map

Pre-refactor commit: `05dadc3`

Semantic batch: split always-live reservation identity from lifecycle state allocated only when an
endpoint materializes or connection destroy begins.

Old active symbols: `RegisterCanonicalConnectionLocked`, `NotifyEndpointMaterializedLocked`,
`DestroyConnectionOwned`, `OnEndpointDestroyed`, `TryFinalizeConnection`,
`OnConnectionViewErased`, `CompleteFinalizeConnection`, `GetConnectionLifecycleSnapshot`, and
`GetConnectionStorageSnapshot`.

Old call paths: ordered reservation opens the connection identity and installs both local views;
materialization marks one endpoint slot; explicit destroy aggregates callbacks, drains endpoints,
erases both views, retires the identity, and publishes callbacks.

Continuous code blocks reused: lifecycle control routing, slot transition rules, destroy reason
upgrade, callback order, finalization gate, owner-local view resolution, TPN bookkeeping, and
existing public snapshot behavior.

Reuse mode: move existing fields without changing their transition code; synthesize default
unmaterialized state when no sidecar exists.

Required lifecycle adaptations: canonical records retain the resource identity. Full reservation
metadata resolves through the T45 owner local view. Lifecycle state is created on first materialize
or destroy. Callback vectors are created only for a non-null local callback or remote request.

Preserved ordering and observable behavior: reservation order, TPNs, ports, priority, owner,
generation, local views, inbound validation, late drain-only materialization, destroy callback order,
TPN reuse, replacement generation, case inputs, commands, and existing CSV rows.

Existing characterization: `unified-bus-tp-reservation` covers inbound key rejection,
materialization, multipath lookup order, destroy-before/after materialization, repeated destroy,
late drain-only work, TPN reuse, and generation replacement.

Explicit behavior changes: storage snapshots add counts for unmaterialized reservation records,
unmaterialized endpoint slots, active lifecycle sidecars, callback bundles, and callback entries.

Excluded paths: lifecycle coordinator storage, local connection views, dormant endpoint state,
transport algorithms, congestion control, application completion, MTP control, and scale evidence.

Focused validation: `ninja -C cmake-cache unified-bus-test`, `unified-bus-tp-reservation`,
changed-line format/diff checks, and two-axis review.

Re-approval triggers: inability to resolve the owner local view while canonical state exists; any
change to reservation identity, local lookup contents, destroy ordering, endpoint materialization,
or existing runtime snapshot row meaning; any need to touch transport backing state.

## Validation Contract

Run serially from the worktree root:

```text
ninja -C cmake-cache unified-bus-test
DYLD_LIBRARY_PATH="$PWD/build/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
  ./build/utils/ns3.44-test-runner --test-name=unified-bus-tp-reservation --verbose
DYLD_LIBRARY_PATH="$PWD/build/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
  ./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle --verbose
DYLD_LIBRARY_PATH="$PWD/build/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
  ./build/utils/ns3.44-test-runner --test-name=unified-bus-parallel-lifecycle --verbose
git diff --check -- .scratch/rtp-tp-memory-production/T46-task-brief.md \
  src/unified-bus/model/ub-tp-connection-manager.h \
  src/unified-bus/model/ub-tp-connection-manager.cc src/unified-bus/test/ub-test.cc
git clang-format --diff 05dadc3 -- src/unified-bus/model/ub-tp-connection-manager.h \
  src/unified-bus/model/ub-tp-connection-manager.cc src/unified-bus/test/ub-test.cc
```

Expected artifacts: the targeted dylib and test dylib rebuild successfully; the reservation suite
reports 13 passing cases, both named lifecycle suites report PASS, diff/format checks emit no patch,
and no runtime/scale evidence directory is created.

## TDD Slices

1. RED: a new reservation reports two unmaterialized slots and no active sidecar or callback bundle.
2. GREEN: canonical reservation records retain identity only and lazily create the lifecycle sidecar.
3. RED/GREEN: first endpoint materialization creates one sidecar while the peer slot stays
   unmaterialized.
4. RED/GREEN: repeated destroy callbacks allocate one callback bundle, preserve callback order, and
   release all reservation state exactly as before.

## Acceptance

- Preserve reservation order, both TPNs, selected ports, priority, owner, generation, endpoint-slot
  identity, and all local lookup views.
- Allocate callback collections and active-endpoint-only state only when first required.
- Keep an unmaterialized reserved endpoint distinguishable from an invalid or retired TPN.
- Reservation, inbound validation, materialization, multipath-order, destroy, and reuse cases pass
  without changing public inputs or commands.

## Execution Ledger

- Diff scope: connection manager implementation/header, Unified Bus reservation tests, and this
  task brief only.
- RED: the targeted build passed, then `unified-bus-tp-reservation` failed because a pure reservation
  reported zero unmaterialized records and slots before the new snapshot logic existed.
- GREEN: a pure reservation reports one compact record, two unmaterialized slots, zero sidecars, and
  zero callback allocations. First sender materialization reports one sidecar and one remaining
  reserved slot. Both materialized endpoints share one sidecar and report zero reserved-only slots.
- Destroy characterization: one remote and one local destroy callback allocate one callback bundle
  with two entries; the existing test observes both callbacks exactly once before reservation reuse.
- Build: `ninja -C cmake-cache unified-bus-test` passed with only two existing unused-helper warnings.
- Focused tests: `unified-bus-tp-reservation` passed 13/13;
  `unified-bus-parallel-lifecycle` passed 2/2; `unified-bus-lifecycle` passed its complete named suite.
- Canonical `Connection` remains the authoritative owner value established by T45. T46 removed only
  duplicate generation storage and lazy-allocated active lifecycle/callback state.
- Scale, runtime system snapshot, and frozen canonical differential were not run because T46 does not
  change runtime CSV rows and is not an integration milestone.

## Closeout

- Implementation commit: `7717b91` (`perf(unified-bus): compact TP reservation lifecycle state`).
- Standards review: passed after recording reproducible validation commands and consolidating the
  callback bundle allocation path.
- Spec review: passed with canonical connection ownership, default unmaterialized state, lazy
  sidecar/callback allocation, destroy ordering, and reuse semantics preserved.
- Final focused rerun added proof that destroy requests without callbacks neither allocate a second
  bundle nor append callback entries; the reservation suite remained 13/13 PASS.
