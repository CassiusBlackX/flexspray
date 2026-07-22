# 1024-NPU routing lookup benchmark

## Result

The full routing table remains faster per lookup. The size of the difference depends strongly
on memory locality and destination-address form:

- On the sequential exhaustive corpus, topology rules are
  21.54x slower for candidate lookup and
  5.14x slower through `GetOutPort`.
- On the fixed-seed random corpus, the gaps narrow to
  5.83x and
  1.72x. Random access makes the table
  6.75x slower than its sequential candidate lookup, while
  topology rules slow by 1.83x. Through `GetOutPort`, the
  random-access penalties are 4.56x and
  1.53x respectively.
- The existing CTP workload uses node-level destination addresses. In the current topology-rule
  implementation that path expands a destination NPU into its physical port adjacencies, runs
  port-specific rule lookup for each one, then merges and normalizes candidates. It is
  174.53x slower for candidate lookup and
  49.24x slower through `GetOutPort`. This is the
  clearest next runtime optimization target.

All six table/rule pairs completed with zero missing routes and matching checksums. The earlier
9,427,968-key full comparison remains the correctness proof for complete candidate sets; this
benchmark checksum is only a runtime guard.

## Measurements

All latency values are medians across three independent processes, in nanoseconds per query.
Each process P50 and P99 is a nearest-rank percentile of the per-query mean within each timed
4,096-query batch; the table reports the median of those process-level values. These are not
individual packet tail latencies.

| Distribution | Path | Table mean | Table P50 | Table P99 | Table QPS | Rule mean | Rule P50 | Rule P99 | Rule QPS | Rule/table mean |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Full corpus, sequential | Candidate lookup | 25.30 | 23.72 | 58.76 | 39.53 M | 545.00 | 515.61 | 959.99 | 1.83 M | 21.54x |
| Full corpus, sequential | GetOutPort | 131.98 | 126.74 | 212.38 | 7.58 M | 677.88 | 625.51 | 1,207.37 | 1.48 M | 5.14x |
| Full corpus, random | Candidate lookup | 170.86 | 166.88 | 289.95 | 5.85 M | 996.43 | 957.42 | 1,384.14 | 1.00 M | 5.83x |
| Full corpus, random | GetOutPort | 601.89 | 591.62 | 766.42 | 1.66 M | 1,036.89 | 993.93 | 1,589.86 | 964.42 K | 1.72x |
| Existing workload, endpoints | Candidate lookup | 40.74 | 36.77 | 105.47 | 24.54 M | 7,111.37 | 6,808.33 | 9,833.55 | 140.62 K | 174.53x |
| Existing workload, endpoints | GetOutPort | 145.39 | 135.89 | 303.29 | 6.88 M | 7,158.31 | 7,008.21 | 9,340.10 | 139.70 K | 49.24x |

## Method

- Topology: 1,024 NPU hosts, 2,336 total nodes, 19,968 links.
- Routing inputs: 9,427,968 full-table rows versus 136 topology rules.
- Each row above uses 10,000,000 measured queries after 100,000 warmup queries.
- Table and topology-rule modes ran in independent processes and used identical query sequences.
- Candidate lookup reuses output vectors. `GetOutPort` uses the unmodified runtime entrypoint,
  including per-call candidate vectors, shortest-only filtering, and HASH port selection.
- Exhaustive `GetOutPort` queries use the current node primary address as `sip`; they measure
  lookup and HASH selection cost but do not claim to reproduce original per-hop packet headers.
- The sequential corpus sorts all valid `(currentNodeId, destinationIp)` keys. The random corpus
  uses a fixed, stateless SplitMix64 index with replacement.
- Median setup time was 14.893 s for the table and
  0.684 s for topology rules.
- Three independent process samples were collected per routing mode on the current macOS arm64
  host. Three samples support a median and visible range, but not a narrow confidence interval.

## Workload boundary

The third corpus comes from the existing 65,536-task 1024-NPU bidirectional CTP simulation
workload. It contains endpoint pairs only. It has no destination-port hint and no per-packet,
per-hop forwarding trace, so it cannot reconstruct intermediate switch queries and must not be
called a production or measured real-traffic distribution. A true traffic-distribution benchmark
requires a route-lookup trace containing at least current node and destination address.

Raw process measurements are in `lookup-1024-table*.json` and
`lookup-1024-topology-rule*.json`; machine-readable comparisons are in
`lookup-1024-summary.json`.
