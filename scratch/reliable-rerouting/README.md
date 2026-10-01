# Reliable rerouting cases

- [2nodes_reliable-rerouting](2nodes_reliable-rerouting/): preserved two-host, two-switch parallel-link regression with real three-copy LastAck.
- [clos_128nodes_alltoall_64k](clos_128nodes_alltoall_64k/): 128 hosts, 16 leaves, 8 spines, real three-hop LastAck and 16,256 directed 64 KiB transfers.

Run from repository root:

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/reliable-rerouting/2nodes_reliable-rerouting'
python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py
python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py --self-test
python3 scratch/reliable-rerouting/run_case.py scratch/reliable-rerouting/clos_128nodes_alltoall_64k
python3 scratch/reliable-rerouting/clos_128nodes_alltoall_64k/check_results.py --self-test
```

Each case owns its configuration, `runlog/`, and `output/` directories.

Before running a CSV case, validate the configured post-failure candidate paths:

```bash
python3 scratch/reliable-rerouting/validate_routes.py scratch/reliable-rerouting/2nodes_reliable-rerouting
```

This preparation check currently accepts single-port hosts with explicit destination-port0 route
rows. It checks all candidates under ingress exclusion and the configured bidirectional fault.
Runtime LastAck hop evidence remains necessary.

## Recovery occupancy outputs

Armed endpoints write `output/reliable_rerouting_occupancy_summary.csv` before cleanup by default.
Set `default ns3::UbReliableRerouting::TraceOccupancyEvents "true"` to also record occupancy events.
Both use integer `time_ps`, node/origin port, stage, current and peak packets/bytes, state, event and explicit source (`retry`/`failed_egress` for handoff).
Stages are `in_flight`, `retry`, `failed_egress`, `recovery_voq`; the last is already included in actual
VOQ backlog. Retry zero or `DRAINED` means handoff, not final delivery. Early stops can retain VOQ
recovery occupancy even after drain. Disabled and ineligible endpoints have no summary rows.

The small/Clos checkers require current stage summaries as well as actual LastAck/task evidence;
their self-tests reject missing summaries and nonempty recovery VOQs despite retry zero. Historical
outputs remain historical evidence; regenerate into a separate case directory for the new schema.
See `src/unified-bus/doc/reliable-rerouting-buffer-accounting-validation.md` for implementation runs.
