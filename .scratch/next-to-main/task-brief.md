# GitCode next to main integration

Base next: 61eea8e6142bf8f6729cca50ddfed3714301a1d5
Merge source main: 7f558d9408ed24c3c3c35c72817a67253170a042

## Batch Reuse Map

Semantic batch: retain main GBN timeout CAQM rollback with next descriptor retransmission.
Old active symbols: UbGbnRetransStrategy::OnTimeout, GetRetransmissionPayloadBytesFrom,
UbTransportChannel::ReTxTimeout, UbHostCaqm::OnSenderRetransmissionRollback.
Reuse mode: reuse main CC rollback and next descriptor byte accounting. Preserve next PSN,
segment progress, descriptor ownership and retry dispatch. TPNAK already has an adapted
implementation in next (9402d4c); do not restore main segment rewind implementation.
Explicit behavior changes relative to next: timeout byte rollback; reserved opcode drop
counter from main; current main contact QR code; main RR knowledge card.
Existing characterization: transport-retrans and congestion-control suites; add focused
GBN timeout integration test using existing fixture and actual CAQM accounting.
Excluded paths: GitHub atomic, new protocol features, performance policy changes.
Validation: configure release UB+tests with Python 3.12/Ninja, MPI/MTP off; full configured
build, focused transport/CC suites, unified-bus suite, documentation tests, diff check.
Re-approval triggers: protocol semantics outside these merge adaptations.

## Status

Conflict contents resolved in merge commit aa1d21d (parents 61eea8e and 7f558d9).
Read-only integration review found no semantic omissions or duplicate legacy
retransmission paths. No push or remote merge has been performed.

## Validation

- Release configure, UB with tests, MPI/MTP disabled, Ninja, FAST_LINKERS=OFF: PASS.
- Full configured `ninja -C cmake-cache -j6`: PASS after guarding two existing
  MTP-only references in the admitted-write drain fixture.
- `unified-bus`: 234 cases PASS.
- `unified-bus-transport-retrans`: 40 cases PASS.
- `unified-bus-congestion-control`: 28 cases PASS.
- `unified-bus-retrans-cc-regression`: 5 cases PASS, including the new timeout case.
- `unified-bus-lifecycle`: 89 cases PASS; lifecycle-trace: 3 cases PASS.
- `unified-bus-identity-connection-destroy`: 1 case PASS.
- `unified-bus-switch-voq`: 32 cases PASS; create-node-delay-mapping: 4 cases PASS.
- `unified-bus-examples-smoke`: 6 cases PASS.
- `python3.12 -m unittest discover -s tests/openusim_skills -p test_skill_docs.py`:
  20 tests PASS.
- `git diff --check`: PASS.
- `unified-bus-tp-reservation`: 30 consecutive runs passed, 15 cases each, after
  limiting its concurrent Ptr-copy fixture to NS3_MTP (atomic reference counts).
  Raw stdout/stderr and exit files verified in `/tmp/next-main-reservation-repeat/`.
  The main 234-case suite and five-case retransmission/CC suite passed again.
  LLDB affects fork-based tests; debugger runs with skipped functions are diagnostic
  only and are not counted as passing tests. Temporary core diagnostic logging removed.

Logs: `/tmp/next-main-*.log`. The historical main-next performance matrix was not rerun;
this candidate has not been tested with MPI/MTP or Windows.
