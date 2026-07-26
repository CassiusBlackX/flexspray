# Experiment Spec: multi-entity-2g

- Goal: test whether fast-path CPU and lifecycle RSS behavior generalize across Entity/Jetty owners.
- Topology: frozen two-device, three-switch unequal-delay CTP fixture.
- Workload: sixteen simultaneous 128 MiB `URMA_WRITE` operations on distinct Entity pairs.
- Routing: existing per-packet shortest-path configuration.
- Transport channel mode: CTP Entity/VL; the retained CSV is setup-only.
- Observability: 1 MiB trace-on correctness canary, then trace-off 2 GiB performance run.
- Oracle: production task/output artifacts; all shared bytes must match next.

## Execution Record: queue-retirement-disabled diagnostic

- Changed variable: omitted only the current empty-queue callback binding; no allocator, packet,
  Transaction, ACK/window, routing, parser, or trace logic changed.
- Command: `python3.12 scratch/20260720-ctp-fastpath-memory-scaling/scripts/run_experiment.py queue-retirement-ablation`.
- Status: success; input hashes identical, all 16 shared artifacts byte-identical to
  `next@cf8ee683`, and parsed task rows identical.
- Evidence: `results/diagnostics/queue-retirement-disabled/summary.json`.
- Cleanup: callback binding restored and original T36 source/binary/library hashes verified.

## Analysis Notes

- Classification: `matched`.
- The canary confirms empty-queue retirement and allocator-slot churn as the cause of the earlier
  complex CTP divergence. It does not by itself approve permanent queue retention as the repair.
