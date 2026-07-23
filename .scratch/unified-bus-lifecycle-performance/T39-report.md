# T39 Report: WQE terminal policy ownership

T39 removes Transaction's reverse dependency on Function-owned Jetty cleanup policy while preserving
the existing WQE terminal implementation. `UbJettyManager::DestroyJetty` now translates its canonical
cleanup mode into one typed Transaction command: active and graceful progress continue accepted WQEs,
abort stops successor work without force-removing retained transport state, and simulation stop also
forces retained cleanup.

The full `WqeFinish` body remains in Transaction. Segment terminal progress, transport cancellation,
lease release, typed lifecycle outcomes, operation erasure, ordered-successor scheduling, manager
WQE erasure, Jetty callback delivery, and drain progress execute in the same order. Transaction still
reports only `{jettyNum, wqeId, identity, outcome}` through the existing Function-to-manager seam.

The original three-argument `WqeFinish` and `CompleteWqeOperation` APIs remain intact. This matters
for existing direct callers and for ns-3 scheduled member-function binding, which does not apply C++
default arguments. The manager alone uses `CompleteWqeOperationForDrain` to supply the typed mode.
Owner-side policy queries remain as characterization surfaces but are no longer called by
Transaction.

Validation passed serially:

- targeted Unified Bus, Unified Bus test, and test-runner build;
- `unified-bus-transaction-urma`;
- `unified-bus-transport-pipeline`;
- `unified-bus-lifecycle`;
- `git diff --check` and a caller-by-caller review.

The lifecycle suite's fail-fast child-process abort messages were expected; the parent runner passed.
T39 changes policy ownership plumbing, not packet or protocol behavior, and is not a defined
frozen-byte milestone.
