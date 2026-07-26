# T38 Report: CTP transaction-state owner contraction

T38 moves `UbCtpEntityKey`, `UbCtpTransactionContext`, and `UbCtpTransactionState` from the CTP
service header into the Transaction header, where their storage and lifecycle already reside. It
also moves the two key comparison methods and the complete ACK-window implementation block into the
Transaction implementation file.

The implementation is code motion, not a redesign. A separate comparison against the pre-move
source confirmed that all branches, assertions, field defaults, ACK bitmap operations, and state
update order are unchanged. The existing CTP service still owns Entity routing, queues,
packetization, congestion signals, callbacks, and traces. Transaction still owns lazy context
creation, pending/outstanding segment state, completion-event accounting, and finite retirement.

The header dependency now follows ownership: `ub-ctp.h` includes `ub-transaction.h`, while
`ub-transaction.h` no longer includes `ub-ctp.h`. `ub-transaction.cc` retains an implementation-only
CTP include because existing Transaction completion methods call the CTP service.

Validation passed serially:

- targeted Unified Bus, Unified Bus test, and test-runner build;
- `unified-bus-ctp-transaction`;
- `unified-bus-ctp-lifecycle`;
- `unified-bus-lifecycle`;
- `git diff --check`.

T38 is an owner-only contraction and is not a defined frozen-byte milestone. The accepted T36 CTP
baseline remains the behavior reference; no new byte exception was introduced or needed.
