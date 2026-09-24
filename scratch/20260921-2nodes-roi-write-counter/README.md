# ROI target write completion counter

Fixed two-node RTP/ROI experiment. The approved design is locally tracked at
`.scratch/roi-write-counter/design.md`; this package preserves its executable result.
Control submits 64 B relaxed WRITE plus 8 B strong WRITE on the same Jetty before
simulation dispatch. The target reads the actual big-endian post payload.
Treatment submits one 64 B RO WRITE_WITH_ATOMIC_STORE_ADD (`0x19`). The target
commits the data range then adds the decoded notify.data at notify.address in the
same callback. Set `--variant=treatment` and use its own output directory.

The only data range is `[0x1000, 0x1040)` and counter address is `0x2000`.
Both link directions run at 400 Gbps with 1 us propagation. The three specified
protocol delays are zero. All other parameters are discovered from the running model.
The 1 ms stop is a watchdog, not a measured completion time.

Run from the repository root using Python 3.12. On a fresh checkout, configure
with the command in [command-manifest.yaml](command-manifest.yaml), then:

```sh
python3.12 ./ns3 build -j 2 ub-quick-example
python3.12 ./ns3 build -j 2 ub-roi-write-counter
python3.12 scratch/20260921-2nodes-roi-write-counter/record-run.py --variant=control
python3.12 scratch/20260921-2nodes-roi-write-counter/record-run.py --variant=treatment
python3.12 scratch/20260921-2nodes-roi-write-counter/compare-results.py
python3.12 scratch/20260921-2nodes-roi-write-counter/compare-results.py --self-test
python3.12 scratch/20260921-2nodes-roi-write-counter/test-recording.py
```

Run these serially in separate processes. The recorder executes exactly the simulator
command in the manifest, captures console/UTC/return code/HEAD/dirty/source and binary
hashes, and adds `program_identity` to the driver's parameter snapshot. It automatically
preserves prior case output in timestamped `cases/<variant>/archives/` and records the
association in `output/run-history.jsonl`. A nonzero control exit does not cancel an
independently runnable treatment: the checkpoint policy is `continue_full_matrix`.

The comparator never runs a simulation. It refuses to replace existing comparison
files: archive `output/comparison.json` and `output/comparison.md` and record the reason
in the ledger before explicitly regenerating them. Missing/invalid evidence produces
a diagnostic report and nonzero exit; valid zero/negative speedup remains valid with
hypothesis `mismatched`. Self-tests mutate copies of evidence, not network inputs.

Runtime parameter discovery uses `ub-quick-example --case-path=scratch/2nodes_single-tp`
with `--PrintTypeIds`, `--PrintUbGlobals` and `--ClassName=ns3::...` for the registered UB
components. The actual discovery commands/results are retained under local
`output/runtime-catalog/`. No Python topology tool or CSV input is required. The
experiment driver also queries its own runtime defaults, globals and instantiated
attributes, so the resolved snapshot accompanies every run.

See [experiment-plan.md](experiment-plan.md), [run-ledger.md](run-ledger.md) and the
[measured comparison](output/comparison.md) for the fixed contract and evidence.

Packet observation uses the existing fault classifier only for read-only packet-kind
inspection and the production RTP rule-driven decoder exposed by
`ParseReceivedDataPacketForTest`. No fault rules are installed. Port transmit events
are accepted transmission **starts**, and receive events are complete arrivals before
switch processing. Sizes include the modeled datalink and protocol headers, but not
unmodeled physical overhead. TPACK is separate from TAACK.

`runtime_values` contains registered scalar UB defaults, all globals, and actual
attributes of both endpoint ports, controllers, transactions, RTP channels, switches,
routing processes, allocators and queue managers. Pointer relationships are excluded.
Target execution delay is obtained through the checked setter, not a TypeId attribute.

## Knowledge Sources

- `doc/adr/0024-unify-target-transaction-execution.md`: reuse the registered executor
  and standard response path.
- `doc/adr/0025-single-target-execution-delay.md`: one target execution delay.
- `doc/adr/0026-extend-transaction-rules-with-response-length-policy.md`: use existing
  transaction field/header rules.
- `src/unified-bus/test/ub-target-execution-test.cc`: direct RTP pair API precedent;
  no test fixture is imported into this driver.
- UB Base Specification 2.1.0, sections 7.4.2.1.6 and 7.3.3.2, as recorded in the
  approved design: write with atomic store add and ROI ordering.

This measures target model readiness, without software polling, data byte memory
modeling, multi-packet atomicity, retransmission deduplication or statistical claims.
