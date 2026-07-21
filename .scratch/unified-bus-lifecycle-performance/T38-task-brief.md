# T38 Task Brief: Move CTP transaction state to its canonical owner

## Batch Reuse Map

- Pre-refactor commit: `main@ccf41b58`; runtime behavior reference: accepted T36 candidate.
- Semantic batch: CTP transaction identity, ACK-window context, and finite state type ownership.
- Old active symbols: `UbCtpEntityKey`, `UbCtpTransactionContext`,
  `UbCtpTransactionState`, `UbTransaction::{GetOrCreate,Find,TryRetire}CtpTransactionState`, and
  `UbCtpTransportService::GetTransactionState`.
- Old call path: CTP service resolves an Entity key, delegates finite state storage and retirement to
  `UbTransaction`, and uses the context for send admission and TAACK advancement.
- Continuous code blocks reused: all three type definitions, both key comparison methods, and the
  complete constructor/admission/ACK/window method block.
- Reuse mode: move/extract without rewrite.
- Lifecycle-only adaptation: remove the `ub-transaction.h -> ub-ctp.h` reverse dependency and make
  `ub-ctp.h` consume the Transaction-owned domain types.
- Preserved ordering and observable behavior: field order and defaults, lazy context construction,
  ACK bitmap reset/mark/advance, pending and outstanding maps, completion-event count, and state
  retirement remain unchanged.
- Existing characterization: `unified-bus-ctp-transaction`, `unified-bus-ctp-lifecycle`, and
  `unified-bus-lifecycle`.
- Explicit behavior changes: none.
- Excluded paths: CTP packet builder/parser, headers, one-packet invariant, queue retention, routing,
  Jetty admission, congestion state, lifecycle storage representation, callbacks, traces, and timers.
- Focused validation: targeted Unified Bus/test build, the three suites above, and
  `git diff --check`.
- Re-approval triggers: any new public domain API, protocol algorithm change, owner change beyond the
  header dependency, unknown test failure, or observable output difference.

## Result

- Implementation revision: `84b03cfce9978bbdce4d5f3b291dae774f2220b5`.
- Targeted build passed after adding the pre-existing local `UbTaSsnSequence` alias to the new
  implementation file.
- All three focused suites passed with the current no-suffix test runner.
- Independent review confirmed that the comparator and ACK-window method bodies are exact moves;
  no old field, branch, mutation order, error, callback, trace, timer, packet, queue, or lifecycle
  behavior changed.
- `ub-transaction.h` no longer includes `ub-ctp.h`. The remaining reverse knowledge exists only in
  `ub-transaction.cc`, where existing Transaction methods call CTP service methods.
- T38 is an ordinary owner-only contraction, not a frozen-byte milestone; no large baseline run was
  required or executed.
