# CTP full TA ticket 04: independent requester progress

Implementation: `8d99d10549fe9194e316e4acf548e2ef48c50304`. Ticket04 accepted; ticket05 follows.

Baseline: `40df13d8dd376d6a1e5a5cb3cf12899fa8b430d3`.
UbJetty is now the sole sequence/window/WQE-progress authority. Entity cursors, ACK bitmap,
pending head and duplicate outstanding state are removed. The existing service round-robin admits
one segment then rotates, skips blocked requesters and wakes through existing progress events.
Whole-WQE checked reservation includes queued WQEs and reserves UINT32_MAX as an exclusive sentinel.
CTP parameter and bitmap changes require owner-local idle state; equal values preserve state.
Delayed CTP completion leases now belong to the exact Jetty generation, and drain checks those leases.
Iodie maps exhausted sequence space to its existing IdSpaceExhausted result.

## Validation and review

Full configured builds passed: `python3.12 ./ns3 build -j 2` (release, tests ON,
unified-bus/iodie/ddie, MPI/MTP OFF). All eight ticket suites passed via
`python3.12 ./ns3 run --no-build 'test-runner --test-name=NAME --verbose'`:

- unified-bus-ctp-transaction, unified-bus-jetty-inflight, unified-bus-modulo-sequence-regression
- unified-bus-transaction-urma, iodie-urma, ddie, ddie-rtp, unified-bus-examples

Additional atomic, CTP lifecycle and skill documentation checks passed. Live dual Jetty fixture
uses distinct RCIDs and equal initial TASSN: while held requester remains una/nxt0/1, the other
reaches2/2 and completes once; release drains both once. Shared queue observations are2 then0.
Original DdieStarsWaitTest passes with its fast RTSQ, compute, held-slot and next-AICPU assertions.
Tests also cover real wire65535->0, exact-record matching, same-Jetty cross-WQE/cross-destination
holes, queued reservation at UINT32_MAX, public exhaustion status and busy/equal/idle parameter guards.
Selected full WRITE64 wire bytes equal ticket03 M2.

The original Clos input had inflight10000 but ACK capacity2048, rejected by the specified window
constraint. The user explicitly approved changing only UbJettyInflightMax to2048 on2026-10-07.
ACK capacity stays2048. Fresh copied inputs passed the original scratch/ub-quick-example --test=1:
task IDs0..15 exactly once, phase dependencies, runner PASSED, parser16 rows with valid send/ACK times.
The initial failed copy and zero-task parser result are preserved and remain FAIL.

A separate root-agent review against the baseline checked production owner reuse, progress and
callback order, event lifetime, parameter atomicity, exact records, unchanged codec/Atomic behavior
and RTP preservation. No unresolved defect remains for this ticket. Comprehensive selective queue
cleanup and recreate stress remain ticket05. MPI/MTP are SKIP, full repository test.py remains06.

Evidence: `.scratch/ctp-full-tah/execution/04/` task-brief.md, review.md, reviewed-code.json/diff,
build-ledger.json, acceptance/, final-checks/, m3-evidence.json, approved-case-mapping.json,
case-runs-approved/commands.jsonl and case-evidence-approved.json. Initial red and intermediate
failed builds/tests remain recorded. All evidence is from this implementation session.

## Knowledge Sources

- ADR0028/0029/0030: requester identity, sole Jetty progress and matching-before-completion.
- ADR0020 and design D06/D07: exact owner lifetime, sequence and parameter boundaries.
- Existing UbJetty bitmap, manager drain and operation records are the reused production seams.
- Specification PDFs are absent locally; approved design and existing code are the sources.
