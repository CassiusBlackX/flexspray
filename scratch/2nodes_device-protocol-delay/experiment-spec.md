# Two-Node Device Protocol Delay Example

## Goal

Provide one minimal runnable case that applies fixed delays at the three device protocol stage
boundaries.

## Model

- Topology: two directly connected `DEVICE` nodes.
- Link: `400Gbps`, `100ns` one-way propagation.
- Workload: one `64B URMA_READ` from node 0 to node 1.
- Transport: RTP with on-demand TP creation.
- Routing: per-flow shortest path.
- Device protocol profile:
  - initiator dispatch: `188ns`
  - target processing: `216ns`
  - initiator completion visibility: `143ns`

The three delays sum to `547ns`. Together with the modeled round-trip link propagation, this case
is intended to reproduce the approximately `757ns` short-message path discussed in the OpenURMA
reference. It is a simulator reference profile, not a claim that all hardware endpoints have these
values.

## Run

```bash
python3.12 ./ns3 run --no-build \
  'scratch/ub-quick-example --case-path=scratch/2nodes_device-protocol-delay'
```

## Expected Evidence

- The simulation prints `Simulation finished.`.
- `output/task_statistics.csv` contains one completed task.
- The expected task FCT is approximately `751ns` with the current RTP model.

## Execution Record

- Runtime: Python 3.12, single-thread simulator.
- Command: the command in the `Run` section with `--no-build`.
- Result: success; the simulator printed `Simulation finished.` and parsed one completed task.
- Observed task interval: `0.0100us` to `0.7614us`.
- Observed task FCT: `751.40ns`.
- Generated evidence: `output/task_statistics.csv`, `output/throughput.csv`, and detailed traces
  under `runlog/`. These generated directories are ignored by Git.
