# T55 Control Experiment Spec

- Goal: establish canonical behavior for the two-phase same-identity RTP workload.
- Topology: two-layer Clos, 32 hosts, 4 leaves, 8 spines, 400 Gbps, 20 ns.
- Workload: 32 phase-0 directed ring URMA_WRITEs and 32 phase-1 WRITEs with the exact same
  source/destination/priority connection keys; phase 1 depends on phase 0 and delays 1 us.
- Runtime: Release/Ninja, MTP 16, MPI off, RNG run 1, dependency visibility 10 ns, initial source
  offset window 32 ps, seed 1.
- Transport channel mode: on-demand RTP; task-scoped Jetty.
- Observability: canonical traffic output, process guard/timing/RSS, stdout/stderr.
- Source: `e3682e593c144ab441dd7045aff5f6312efab86d`.
- Status: approved.
