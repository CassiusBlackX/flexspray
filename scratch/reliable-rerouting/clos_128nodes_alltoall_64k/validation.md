# Ticket 04 acceptance — 2026-09-26

Implementation: `1f8a49f2`; base commit: `6d071472`. Tools submodule: `0fec3aedb7fe778b8282a5a1257a262a80d3029d`.
All acceptance items passed. The local tracker contains the detailed batch ledger and raw logs;
this document and [acceptance-summary.json](acceptance-summary.json) preserve the durable results.

## Actual case results

The approved 128-host, 16-leaf, 8-spine topology contains 152 nodes and 256 links.
All 16,256 directed 65,536-byte tasks completed, totaling 1,065,353,216 payload bytes.
The last completion was at 417.783760 us. The final reproduction exited 0 in 37.665145 wall seconds;
the runner reported zero runtime packet drops. Ordinary and fast TP retransmission were disabled
in the exact hashed runtime snapshot. No workload, topology or fault-time calibration was needed.

The permanent bidirectional fault was injected at 100 us on leaf 128 port 8–spine 144 port 0.
Detection completed at 105 us. Both directions contained retained attempts:

| Failed endpoint | Retry at detection | First valid LastAck / local drain | Recovery transfers |
| --- | ---: | ---: | ---: |
| Leaf 128:8 | 116 | 105.164520 us | 116 |
| Spine 144:0 | 117 | 105.225480 us | 117 |

Six source copies produced 18 actual serializations over three physical hops per copy.
The shared checker verified receive-before-recovery ordering, unique owner transfers, both directions,
and zero retry/pending/processing work at first drain. It rejected all 13 common evidence mutations;
the macro checker also rejected four exit/provenance/configuration mutations.

Preflight checked all 49,272 reachable candidate states with the real ingress/fault constraints;
both LastAck directions require three hops. This is preparation evidence. Actual hop records,
handshake and task completion provide the runtime evidence. The case gate verified 22,952 final
route rows and the complete topology/workload inventory.

## Executed validation

All final commands below ran serially from the repository root. `$case` below abbreviates
`scratch/reliable-rerouting/clos_128nodes_alltoall_64k`.

```bash
case=scratch/reliable-rerouting/clos_128nodes_alltoall_64k
python3.12 ./ns3 build -j 4
python3 scratch/reliable-rerouting/validate_routes.py "$case"
python3 scratch/ns-3-ub-tools/ub_tools.py case check "$case" --json
python3 scratch/reliable-rerouting/run_case.py "$case"
python3 "$case/check_results.py" --self-test
```

The wrapper invokes exactly `python3.12 ./ns3 run --no-build
'scratch/ub-quick-example --case-path=scratch/reliable-rerouting/clos_128nodes_alltoall_64k'`.
It binds exit status and output hashes to inputs, source state, scripts and binaries captured
before execution, and rejects changes during execution. The recorded run has
`exit_code=0` and `inputs_unchanged=true`.

`build/utils/ns3.44-test-runner --suite=<suite> --verbose` passed for all eight suites:

| Suite | Passing cases |
| --- | ---: |
| unified-bus-reliable-rerouting | 37 |
| unified-bus-link-fault-input | 3 |
| unified-bus-routing | 2 |
| unified-bus-routing-modes | 13 |
| unified-bus-structured-routing | 6 |
| unified-bus-transaction-header-codecs | 3 |
| unified-bus-lifecycle-trace | 3 |
| unified-bus-trace-writer | 3 |
| Total | 70 |

The full integration build passed; the final build reported no work remaining.
The trace-writer suite intentionally exercises a fatal invalid-output-path child process;
its parent test and suite passed. Repository-wide `./test.py` was not run or claimed.

The preserved small case ran through the same wrapper, followed by
`python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py --self-test`:
four 2 MiB tasks completed, 382 transfers covered both directions, and all 13 negative checks passed.

Reproduction command:

```bash
python3 scratch/reliable-rerouting/clos_128nodes_alltoall_64k/generate_case.py \
  --output-dir .scratch/ub21-reliable-rerouting-lastack/regenerated-lf-case
```

All nine generated CSV files and the full 144-entry `network_attribute.txt` matched the checked-in
inputs byte for byte. The LF generation matches repository checkout policy; staged CSV bytes were also verified identical
to the executed files. Before normalization, Git staging warned that CRLF inputs would change on
checkout. The generator now writes LF before hashing; a full rerun passed with only line endings
changed. The prior run is preserved as `04-before-lf-output` and matching runlog. Successful macro
executions before and after this normalization produced byte-identical
`task_statistics.csv`, `reliable_rerouting.csv` and `throughput.csv`; their hashes are preserved
in the acceptance summary. With full port tracing disabled, `throughput.csv` contains only its
header and is not used to claim throughput or line-rate performance.

## Review and preserved failures

Standards review: no remaining blocking findings. The two provenance suggestions—capture state
before execution and include untracked scripts—were implemented, reproduced and independently
rechecked. Spec review: no remaining blocking findings against ticket 04 and the approved design.
The implementation reuses the existing topology/traffic generators, runtime catalog, runner,
buffered trace sink and small-case checker.

The first macro execution completed all business tasks but failed evidence checking because
Rerouting state trace binding was nested under disabled port tracing. Its raw evidence is retained
locally as `04-missing-state-output` and `04-missing-state-runlog`. The fix connects the existing
Rerouting trace under packet tracing independently of full port logs; identical inputs then passed.
The earlier successful run is preserved as `04-verified-before-provenance-output` and matching
runlog. No failed run is counted as a passing acceptance result.

Ticket 03 had one recorded orchestration deviation: routing suites briefly overlapped after a
running session was mistaken for completion. The affected suites were rerun serially; all final
ticket 04 builds, suites and simulations were serial. No builds overlapped.

## Scope and knowledge sources

This validates the accepted complete-packet model and one permanent bidirectional link failure.
It does not implement or claim flit/CRC recovery, multiple concurrent fault epochs, MPI/MTP recovery,
or general topology routing synthesis. The CSV preparation validator is restricted to its declared
single-port RTP URMA_WRITE / shortest-only inputs; table/index/rule runtime constraints are covered
separately by C++ tests. The 8 MiB reserve is an explicit functional-case parameter, not a hardware
buffer recommendation. No throughput or performance comparison is claimed.

- ADR 0027 records the accepted packet-boundary interpretation of UB 2.1 §5.3.9.
- ADR 0028 records failure-only detour authorization and preserved routing constraints.
- `dev-knowledge/specification/` supplies the repository protocol baseline; the approved local
  design records the external UB 2.1 source and section references.
- `.agents/skills/openusim-references/trace-observability.md` defines the trace gates and evidence
  semantics; it was updated with the independent Rerouting trace binding.
