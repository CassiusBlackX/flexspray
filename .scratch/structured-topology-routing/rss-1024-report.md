# 1024-NPU structured routing memory benchmark

## Scope

The benchmark installs the same 1DPOD topology in two independent C++ processes:

- 1,024 NPU hosts, 2,336 total nodes, and 19,968 physical links.
- Full table: 9,427,968 table rows.
- Topology rules: 136 rules and 10,528 node memberships.

The measured path is the real structured-input runtime path:

1. UbUtils::ReadStructuredTopologyConfig
2. UbUtils::InstallStructuredTopology
3. destruction of UbStructuredTopologyConfig

The normal simulation and MPI entrypoint is deliberately not used. It would add unrelated
MPI initialization and application state to the measurement.

## Results

| Metric | Full table | Topology rules | Change |
| --- | ---: | ---: | ---: |
| Serialized input | 154.61 MiB | 0.75 MiB | -99.51% |
| RSS after config release, median of 3 | 1,646.47 MiB | 221.48 MiB | -1,424.98 MiB (-86.55%) |
| Peak RSS, median of 3 | 1,646.47 MiB | 221.48 MiB | -1,424.98 MiB (-86.55%) |
| Live heap after config release, 1 diagnostic run | 874.52 MiB | 201.05 MiB | -673.48 MiB (-77.01%) |
| Parse time, median of 3 | 6.168 s | 0.016 s | 384.9x faster |
| Install time, median of 3 | 10.703 s | 0.794 s | 13.5x faster |
| Parse plus install, median total | 16.871 s | 0.812 s | 20.8x faster |

RSS ranges across the three independent processes were 1,572.09-1,647.34 MiB for the full
table and 221.38-221.53 MiB for topology rules.

## Interpretation

The live-heap difference, 673.48 MiB, is the best estimate of memory eliminated from active
C++ objects by replacing the full table with shared topology rules. Both modes still contain
roughly 200 MiB of common ns-3 topology, node, port, channel, and switch state.

The larger RSS difference, 1,424.98 MiB, is the actual process-level benefit on the current
input path. The full-table parser first materializes 9.43 million UbStructuredTableRoute
objects. After those objects are destroyed, macOS malloc retains much of their address space
for reuse instead of returning it to the OS. malloc_zone_pressure_relief returned zero in
these runs. Topology rules avoid both the live routing maps and this parsing high-water mark.

This proves a cold-start and resident-memory improvement. It does not imply faster per-packet
lookup. The existing release lookup microbenchmark still measures the direct table at about
4.50 ns and the 1DPOD topology rule at about 139.73 ns per lookup.

## Method

- Current RSS: task_info with MACH_TASK_BASIC_INFO.
- Peak RSS: getrusage with RUSAGE_SELF.
- Live heap: malloc_zone_statistics over all malloc zones.
- Three independent processes per routing mode for the primary RSS and timing samples.
- The OS page cache was not flushed; independent process state, not cold storage, was tested.
- The 162,121,150-byte table protocol was streamed one destination at a time to a temporary
  file. The Python generator ran outside the measured C++ process.

Raw samples are stored beside this report as rss-1024-*.json. The aggregate machine-readable
result is rss-1024-summary.json.
