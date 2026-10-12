# CTP full TA ticket 05: safe requester retirement

Implementation: `0eb4248f82944af4faad4a579480843789def7b5`. Ticket05 accepted; ticket06 follows.

Baseline: `7db1b86876acb9a2f726e700ddbea965efe1f266`.
Existing CTP source queue entries now carry optional exact local Jetty identity. Only admitted
local requests receive it; target responses and CNP remain unowned. Abort/force remove that owner's
unsent requests, preserve other requesters and reverse responses even with the same numeric RCID,
and retain FIFO within CNP/ACK/DATA priority. Manager drain includes this owner's queued entries.
Existing dispatch cancellation, typed FAILED/FLUSHED terminals and tracked completion events remain
the cleanup authority. Running callbacks retain their lease until unwind; graceful drain retains
accepted work and modeled delays. Work already handed off may still execute remotely.

Delayed completion rechecks captured RCID/generation and exact operation/segment. Resolved stale
work is ignored; conflicting live identity asserts. Captured absolute TASSN prevents a delayed old
duplicate from being reinterpreted after a full wire16 reuse cycle. Common lookup validates its
record key against backing sequence. No packet bytes, target executor or RTP/LDST algorithms changed.

## Validation and review

`python3.12 ./ns3 build -j 2` passed in the configured release build, tests ON,
unified-bus/iodie/ddie and MPI/MTP OFF. All eight required suites passed via
`python3.12 ./ns3 run --no-build 'test-runner --test-name=NAME --verbose'`:

- unified-bus-ctp-lifecycle, unified-bus-ctp-transaction
- unified-bus-lifecycle, unified-bus-lifecycle-storage
- iodie-urma, ddie-termination, ddie, ddie-rtp

New existing-fixture tests cover graceful held target with another requester still live; dispatch
cancellation; pending/running delayed completion for graceful, abort and force; queue isolation for
two local requesters plus same-RCID reverse ACK/READ response/CNP; new-generation protection;
remote execution and real late wire response after abort; recreation before canceled callback's
old deadline; resolved duplicate crossing wire wrap; and explicit live-identity SIGABRT.
Once-only terminals, exact owner retirement, zero event residue and completed operation/segment
storage are asserted. The selected full WRITE64 bytes equal ticket04 M3 exactly.

The initial red test proved canceled source packets remained queued. Intermediate fixture failures
(stale snapshot after generation reuse, assertion macro formatting, ACK injection before handoff)
were corrected without weakening production semantics. Those logs remain preserved.
A separate root-agent standards/spec review against the baseline found no remaining defect.
`git diff --check` passed. MPI/MTP are SKIP; final integration and all eight cases remain ticket06.

Evidence root: `.scratch/ctp-full-tah/execution/05/`, including task-brief.md, reviewed-code.json/diff,
review.md, build-ledger.json, final-build/commands.jsonl, acceptance/commands.jsonl and m4-evidence.json.

## Knowledge Sources

- ADR0020/0028/0029/0030 and design D08: exact owner retirement, RCID and local cleanup boundaries.
- Existing CTP queue, JettyManager, OperationRecord and UbTrackedEventSet production paths.
- Existing BuildLocalTpTopology, target executor, lifecycle snapshots and child-process fault fixtures.
