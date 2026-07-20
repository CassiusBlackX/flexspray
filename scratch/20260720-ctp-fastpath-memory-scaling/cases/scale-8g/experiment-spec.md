# Experiment Spec: scale-8g

- Goal: determine whether the lifecycle RSS excess remains material at 8 GiB total payload.
- Topology: frozen two-device, three-switch unequal-delay CTP fixture.
- Workload: sixteen simultaneous 512 MiB `URMA_WRITE` operations on Entity pair `0 -> 0`; this
  preserves 8 GiB total payload without crossing the parser's signed per-task boundary.
- Routing: existing per-packet shortest-path configuration.
- Transport channel mode: CTP Entity/VL; the retained CSV is setup-only.
- Observability: all trace output OFF for performance.
- Correctness: inherited from the frozen byte-identical `ctp-win4` differential.
