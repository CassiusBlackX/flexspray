# CFG6 CTP full transaction headers: integration evidence

Port note: this record and tickets 01–05 preserve historical source-branch evidence, including
Iodie/DDIE. For the GitCode main port's scope, omitted device-module adaptations and fresh
validation, see [the main-port record](ctp-full-tah-main-port.md).

Status: all six tickets completed and accepted. This document update is ticket06 closeout.
Session baseline: `a4e7025844dd400cf2c0644699de1b6950489377` on `feat/ub-2.1-ddie`.
Final production revision: `56b369e734e95d11a2670958bbe79a45f66a923c`; ticket06 adds regression
fixtures and documentation only. Actual command revisions, source fingerprint and dirty diffs are
recorded under `.scratch/ctp-full-tah/execution/`. Historical test results are not counted as PASS.

CFG6 CTP now uses full BTAH/ATAH and applicable full MAETAH through the shared TA codec.
Each Jetty generation gets a separate monotonic20-bit RCID while retaining the24-bit caller ID.
UbJetty alone owns requester sequence/window/progress; the shared CTP service rotates ready
requesters and retains existing routing, queue priorities and congestion domains. Wire identity and
request-dependent response validation precede completion. Segment address/length and zero padding
are exact; existing Atomic N/P rules, target executor and unrelated RTP/LDST behavior remain.
Requester drain/abort/recreate uses existing lifecycle owners and exact queue/event ownership.
There is no compact fallback, aggregate quota, device workaround or remote rollback guarantee.

## Ticket commits

| Ticket | Implementation | Closeout | Evidence summary |
| --- | --- | --- | --- |
| 01 shared codec | `85667a83` | `2ca1645b` | [codec and RTP](ctp-full-tah-01.md) |
| 02 requester preparation | `e8eb31e6` | `8ca8f1c8` | [RCID and admission](ctp-full-tah-02.md) |
| 03 full CFG6 wire | `eda5d678` | `40df13d8` | [wire, matching and config migration](ctp-full-tah-03.md) |
| 04 independent progress | `8d99d105` | `7db1b868` | [Jetty, DDIE and window boundaries](ctp-full-tah-04.md) |
| 05 safe retirement | `0eb4248f` | `56b369e7` | [drain, abort and recreate](ctp-full-tah-05.md) |
| 06 integration | `7d62c68a` | this record's closeout commit | this record |

Each ticket recorded its task brief/Batch Reuse Map before editing, actual validation, a separate
root-agent review, implementation commit and separate closeout. No subagents or remote publication.

## Original entry and all eight cases

The original `unified-bus-examples` suite passed all24 tests compiled in this configuration,
including the scratch alias, catalog/CLI/config, required/optional files, RTP Read/Write/dependencies,
error paths and rejection of MTP requests when MTP is disabled. It was not replaced by another runner.
All cases used fresh input copies and the original command:

```text
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=PATH --test=1'
python3.12 scratch/ns-3-ub-tools/trace_analysis/parse_trace.py PATH
```

| Original case under scratch/ | Exact task IDs | Count | Result |
| --- | --- | ---: | --- |
| 2nodes_single-tp | 0–3 | 4 | PASS |
| 2nodes_atomic_ctp | 0–24 | 25 | PASS |
| 2nodes_transaction_extensions/ctp-existing | 0–17 | 18 | PASS |
| 2nodes_transaction_extensions/ctp-writes | 0–18 | 19 | PASS |
| 2nodes_transaction_extensions/ctp-queue | 0–15 | 16 | PASS |
| ctp-endpoint-ports | 0–6 | 7 | PASS |
| 2nodes_ctp_multi_jetty_repro/evidence/cached-original | 0–1 | 2 | PASS, same-Jetty control |
| clos_32hosts-4leafs-8spines_ctp-entities | 0–15 | 16 | PASS, approved window migration |

All107 tasks have exactly one start/completion, valid phase causality, runner PASSED, matching parser
rows and finite ordered first-send/last-response timestamps. Source input hashes remain unchanged
through the runs. The cached historical directory remains untouched; its fresh copy removes only
ExperimentTaAckPacketBytes. It is never used as the independent multi-Jetty witness.

The original Clos configuration was rejected because inflight10000 exceeded ACK capacity2048.
The user explicitly approved UbJettyInflightMax10000→2048 on2026-10-07; ACK capacity remains2048,
with the original topology/workload and all other parameters. The failed run and zero-task parser
output remain recorded as FAIL in ticket04. Both approved04 and final06 copies pass.

## Acceptance matrix

| Requirement | Current evidence |
| --- | --- |
| A01 codec/RTP preservation | Header-codec/wire-format/URMA tests; eight frozen TA+payload literals;35 original RTP trace files exactly equal sessionM0 |
| A02 RCID boundary | Real preparation at0/max, retained association after exhaustion, no reuse,24-bit caller ID and atomic rejection tests |
| A03 preparation before acceptance | UB/UbApp/Iodie preflight and original cases; rejected admission leaves no operation/progress |
| A04 full CFG6 | All enabled operation/extension suites and original Atomic/extension cases; selected WRITE64 wire equalsM4 |
| A05 exact segments/padding | Real5000-byte Read/Write tails, offsets, all padding widths, Atomic N/P and variable response tests |
| A06 guarded matching | Trace-off/on bad-response matrix, wrong/missing tags, peer/Entity/port/opcode/status/sequence/layout/payload rejection before progress |
| A07 independent requesters | Live two-Jetty fixture: distinct RCIDs, initialTASSN0, held0/1 vs independent2/2, each completes once after release; shared queue2→0 |
| A08 per-Jetty frontier | Same-Jetty cross-WQE/cross-destination hole retained; other requester continues; existing events wake scheduling |
| A09 sequence/config limits | Actual65535→0, queued whole-WQE exhaustion at UINT32_MAX, invalid/busy/equal/idle guards; complete wire cycle from0 below |
| A10 safe retirement | Grace, abort/force, pending/running callbacks, dispatch cancellation, queue collision and recreate/late-wire tests |
| A11 DDIE Stars | Original held RTSQ/fast RTSQ/compute/next AICPU assertions pass; RTP variant passes |
| A12 original examples/config |24 original example tests plus all8 cases; catalog excludes deleted key and0/128 explicitly reject |
| A13 integration |30 required suites pass; final configured build/test.py result below; independent review and conditional coverage recorded |

The complete-cycle fixture sends65,538 actual64-byte segments through BuildLocalTpTopology and the
common target executor, starting from logical0 without cursor positioning. Each wire value2..65535
appears once,0 and1 twice, logical una/nxt reaches65538/65538, one WQE completes once, and operation,
segment and source queue state drains. This complements the near-boundary real-outstanding tests.
A delayed resolved duplicate crossing a full wire cycle cannot be mistaken for new work; a
contradictory same-logical live identity fails explicitly.

The old DDIE blocker is preserved in `.scratch/ddie-redesign/ctp-sequence-conflict.md` and its original
logs. Current `ddie` evidence shows held RCID0 at0/1 while fast RCID1 reaches1/1; release completes
cpu, next and compute with all original assertions. No DDIE scheduling workaround was added.

## Commands, failures and limits

Configuration: release, unified-bus/iodie/ddie plus dependencies, tests ON, examples OFF, MPI/MTP OFF.
Full configured builds passed with `python3.12 ./ns3 build -j 2`. Tests/builds ran sequentially.
The30 ticket-defined suites pass via `python3.12 ./ns3 run --no-build
 'test-runner --test-name=NAME --verbose'`; exact list/results are in `06/suite-evidence.json`.
Repository test command: `python3.12 ./test.py --no-build --jobs 1 --retain
 --text=.scratch/ctp-full-tah/execution/06/final-all-tests` (default QUICK; all feature tests are QUICK).
Both the first and final full runs passed190/190, with zero failures/crashes. The final run includes
the complete-cycle fixture; all30 required suite XML results were independently rechecked.

The first broad suite failed in an obsolete fixture requiring continuation after requester
abandonment. It was migrated to the approved whole-generation close/new-RCID contract, retaining
progress and once-only assertions, and added to focused CTP tests. Focused and broad suites then
passed. Production semantics and duplicate-terminal errors were preserved. Initial failure logs
remain visible; they are not relabeled successful.

Runtime PrintTypeIds, ClassName and PrintUbGlobals queries pass. The deleted parameter has no
production member/helper/alias/no-op and is absent from maintained configs. Negative config values0
and128 both fail explicitly at the missing attribute. Skill documentation checks pass20/20.
MPI/MTP execution is SKIP because those paths were not compiled. General ns-3 examples were disabled;
the required original scratch entry and its system suite were explicitly built and tested.
The feature does not claim complete UB2.1 operation/mode coverage beyond the approved enabled subset.

## Evidence and review

Final evidence root: `.scratch/ctp-full-tah/execution/06/`:
`case-mapping.json`, `case-evidence.json`, `case-runs/commands.jsonl`, `suite-evidence.json`,
`catalog-evidence.json`, `rtp-raw-comparison.json`, `m5-evidence.json`, `validated-source-sha256.txt`,
`final-build-command.json`, `final-all-tests-command.json`, logs, task-brief.md and review.md.
Prior ticket directories preserve all red/failed/intermediate results and individual milestone data.
The independent root review checked owner/helper reuse, validation ordering, live/stale distinctions,
callback/event/queue lifetime, unchanged RTP/LDST algorithms, documentation scope and exact input
migrations. Final configured build/test.py, source fingerprint, source-input hashes and git diff --check pass.
No unresolved review defect or required current-configuration acceptance gap remains.

## Knowledge Sources

- ADR0020/0022/0024/0026/0027 and implemented ADR0028–0031: owner, transport, rule and full-header boundaries.
- Approved `.scratch/ctp-full-tah/design.md`, tickets and initial task briefs: acceptance authority.
- Existing production header/codec, Jetty, transaction record, queue and lifecycle event implementations.
- `dev-knowledge/specification/` PDFs are absent locally; no claim is made of reading missing PDFs.
