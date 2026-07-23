# Experiment Spec: mixed-bidir-2g

- Goal: cover both CTP request/response directions with mixed READ and WRITE traffic.
- Topology: frozen two-device, three-switch unequal-delay CTP fixture.
- Workload: eight WRITEs and eight READs, evenly split across both directions and distinct Entity
  identities, 128 MiB each.
- Routing: existing per-packet shortest-path configuration.
- Transport channel mode: CTP Entity/VL; the retained CSV is setup-only.
- Observability: 1 MiB trace-on correctness canary, then trace-off 2 GiB performance run.
- Oracle: production task/output artifacts; all shared bytes must match next.
