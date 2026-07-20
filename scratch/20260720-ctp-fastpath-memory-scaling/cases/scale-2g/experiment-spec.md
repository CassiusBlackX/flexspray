# Experiment Spec: scale-2g

- Goal: obtain the primary nine-block three-role CTP CPU and RSS comparison.
- Topology: frozen two-device, three-switch unequal-delay CTP fixture.
- Workload: four simultaneous 512 MiB `URMA_WRITE` operations on Entity pair `0 -> 0`.
- Routing: existing per-packet shortest-path configuration.
- Transport channel mode: CTP Entity/VL; the retained CSV is setup-only.
- Observability: all trace output OFF for performance.
- Correctness: inherited from the frozen byte-identical `ctp-win4` differential.
