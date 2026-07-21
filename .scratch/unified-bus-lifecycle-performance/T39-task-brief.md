# T39 Task Brief: Narrow the Transaction-to-Function WQE terminal seam

## Batch Reuse Map

- Pre-refactor commit: `main@ccf41b58`; architecture reference: two-authority WQE completion in
  the lifecycle design.
- Semantic batch: Function/Jetty owns terminal policy; Transaction owns operation/segment terminal
  mechanics and emits one typed outcome.
- Old active symbols: `UbTransaction::WqeFinish`, `UbTransaction::CompleteWqeOperation`,
  `UbJettyManager::DestroyJetty`, `UbFunction::ApplyWqeOutcomeFromTransaction`,
  `UbJettyManager::ApplyWqeOutcome`, `ShouldContinueAcceptedWqes`, and
  `ShouldForceDrainAcceptedWqes`.
- Old call path: a normal response or Jetty cleanup enters `WqeFinish`; Transaction terminalizes
  segments and operation state, updates ordered progress, then sends the typed outcome through
  Function to the manager-owned WQE record and upper callback.
- Continuous code blocks reused: the complete `WqeFinish` segment cancellation, lease release,
  lifecycle terminal, operation erase, ordered-successor wake, and typed-outcome blocks remain in
  place and in their current order.
- Reuse mode: direct reuse plus minimal typed-parameter extraction.
- Required lifecycle-only adaptation: replace Transaction's two reverse Function policy queries
  with one typed terminal mode selected by the Jetty manager at its existing cleanup call seam.
- Preserved ordering and observable behavior: child terminal, operation/WQE ledger terminal,
  operation erase, ordered successor scheduling, manager WQE erase, Jetty terminal callback, and
  drain progress keep their existing order.
- Existing characterization: transaction URMA, transport pipeline, strong-order terminal progress,
  graceful/abort/forced Jetty drain, stale/duplicate WQE terminal, and lifecycle suites.
- Explicit behavior changes: none.
- Excluded paths: segmentation, CTP/RTP completion algorithms, TP cancellation and retained-state
  algorithms, Jetty scheduling, packet builders/parsers, callbacks, and lifecycle identities.
- Focused validation: targeted Unified Bus/test build, `unified-bus-transaction-urma`,
  `unified-bus-transport-pipeline`, and `unified-bus-lifecycle`, followed by `git diff --check`.
- Re-approval triggers: a new owner model, changed outcome/callback ordering, a new cancellation
  algorithm, an unclassified caller, or an unexplained test/output difference.

## Characterization Decision

The existing seam is not narrow enough to close by documentation alone. Typed outcome delivery is
already correct, but `WqeFinish` directly asks Function whether to continue accepted WQEs and
whether to force retained segment cleanup. Those values are derived exclusively from the
manager-owned Jetty cleanup mode. T39 will pass that existing decision as a typed command at the
manager-to-Transaction cleanup call and remove the reverse policy calls from Transaction. The
owner-side query methods remain available to existing characterization tests.

## Result

- Implementation revision: `cbc0567b1792f45c078279c19f013038de21d1cf`.
- `WqeFinish` now delegates to one reused `FinishWqe` body with a typed terminal mode. The public
  three-argument method remains unchanged for normal, legacy, and fail-fast callers.
- `CompleteWqeOperation` retains its original three-argument signature for direct and scheduled
  callers. `CompleteWqeOperationForDrain` is used only by the Jetty manager cleanup loop.
- ACTIVE and graceful drain preserve successor progress; abort drain stops accepted successors
  without forcing retained transport state; simulation-stop drain stops successors and preserves the
  existing forced-retained cleanup.
- The first build exposed that an ns-3 scheduled member-function pointer does not apply C++ default
  arguments. The fix retained the old three-argument entry and moved the typed mode to the dedicated
  drain entry; the next targeted build passed.
- `unified-bus-transaction-urma`, `unified-bus-transport-pipeline`, and `unified-bus-lifecycle` all
  passed with the current no-suffix test runner. Expected fail-fast child abort logs appeared inside
  the passing lifecycle suite.
- Independent review found no changed branch, cancellation result, lifecycle terminal, successor
  ordering, manager erase, callback, trace, or timer behavior. No frozen comparison was required.
