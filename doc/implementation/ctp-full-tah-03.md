# CTP full TA ticket 03: CFG6 wire cutover and response matching

Implementation: `eda5d678`. Baseline: `8ca8f1c8d6366c65b2f91a6a71e39bd564890bd6`.
All CFG6 requests/responses now use the shared full BTAH/ATAH codec and full MAETAH where planned.
The UPI/EID wrapper and enabled operation set remain. Transaction prepares exact per-segment
memory address/length, preserving Atomic N/P and zero-payload READ requests. CTP appends explicit
0–3 zero bytes for NTH alignment, validates/removes padding, and queues actual serialized sizes.

Response matching resolves live RCID, exact Jetty generation, checked 16-bit TASSN and existing
OperationRecord/SegmentRecord before decoding request-dependent extensions. Peer, Entity, VL,
endpoint, opcode, status, TAID and payload checks precede completion. Response FlowTag is only
observational. Delayed completion rechecks captured operation/segment identity. There is no compact
fallback or lookup-miss creation. Target accepts SQ types00/01 and echoes them; local SQ emits01.

ExperimentTaAckPacketBytes and its member/helper/setter are removed. Nineteen maintained configs
lose only this key; historical evidence is unchanged. Atomic reference docs now describe full CFG6.

## Validation

Current release configuration: unified-bus/iodie/ddie, tests ON, MPI/MTP OFF. Full configured builds
passed, including final formatting-only build: `python3.12 ./ns3 build -j 2`.
The 13 suites in the union of tickets01/02 passed through
`python3.12 ./ns3 run --no-build 'test-runner --test-name=NAME --verbose'`:

- unified-bus-transaction-header-codecs, unified-bus-transaction-rules, unified-bus-wire-format
- unified-bus-transaction-urma, unified-bus-atomic, unified-bus-transaction-extensions
- unified-bus-ldst-cfg
- unified-bus-ctp-transaction, unified-bus-ctp-lifecycle, unified-bus-ctp-input, unified-bus-ctp-endpoint
- iodie-urma, unified-bus-examples (original scratch entry included)

Coverage includes real5000-byte Read/Write (4096+904), accurate offset/length, all padding widths,
zero-fill, aligned extra response payload rejection, Atomic N/P, BE layouts, Atomic_sync0/N,
trace-off/on bad-response matrices, missing/wrong response tags, type00 echo, unsupported request
profiles, admission overflow/length rejection, old attribute0/128 rejection and wire TASSN wrap.
Existing once-only and progress assertions remain. Iodie's CTP address assertion now expects
base+offset; the production adapter and RTP address assertion are unchanged.

Six original input copies passed with `--test=1`, exact task sets/counts, once-only completion,
phase dependencies and parser first-send/last-response timestamps checked:

| Case | Tasks |
| --- | ---: |
| 2nodes_single-tp | 4 |
| 2nodes_atomic_ctp | 25 |
| 2nodes_transaction_extensions/ctp-existing | 18 |
| 2nodes_transaction_extensions/ctp-writes | 19 |
| 2nodes_transaction_extensions/ctp-queue | 16 |
| ctp-endpoint-ports | 7 |

M2 captures a108-byte WRITE64 full packet and28-byte TAACK header stack, full wire hex, padding
and phase timings. The original RTP single-TP case's35 raw trace files equal sessionM0 byte for byte.
CTP full headers, padding, exact tails and resulting serialization times are approved differences.

New-build PrintTypeIds/ClassName/PrintUbGlobals queries succeeded; the CTP attribute catalog
excludes the old key. Actual config files with0 and128 both fail explicitly at that missing attribute.
`python3.12 -m unittest discover -s tests/openusim_skills -p test_skill_docs.py` passed;
the earlier pytest invocation failed because pytest is absent, and is retained as such.

Evidence root `.scratch/ctp-full-tah/execution/03/`: command manifests and commands.jsonl logs,
case-mapping.json, case-evidence.json, config-migration.json, m2-evidence.json, review.md and diffs.
Initial failures exposed missing MA normalization, nonzero padding and obsolete fixture assumptions;
all were corrected and affected suites rerun. One early Atomic run was interrupted and is not PASS.

## Review and remaining scope

A separate root-agent standards/spec pass found no remaining blocking defect. It checked codec/
owner reuse, validation-before-mutation, header/tag order, exact delayed identity, queue bytes,
original completion/target behavior and config scope. Final review-driven edge tests passed.
Entity progress and the known DDIE stall are still ticket04 work; ticket05 extends requester cleanup.
MPI/MTP are SKIP and repository-wide test.py remains ticket06 work. This is ticket03 closeout only.

## Knowledge Sources

- ADR0024/0026: shared executor, enabled layouts and response policies.
- ADR0028/0030/0031: RCID, wire matching, full CFG6 and padding contracts.
- `.scratch/ctp-full-tah/design.md`: D03–D05, A04–A06 and M2.
- `dev-knowledge/specification/` is absent locally; approved design and existing production Header
  serialization are the implementation sources, without a claim of reading absent PDFs.
