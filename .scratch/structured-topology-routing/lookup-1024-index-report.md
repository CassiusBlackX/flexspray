# 1024-NPU topology next-hop index benchmark

## Result

`UbTopologyNextHopIndex` removes repeated topology-rule scans and gives node-level destination
addresses a direct lookup path. The full routing table remains faster in this benchmark, but the
largest topology-rule bottleneck is removed:

- On the sequential exhaustive corpus, indexed topology rules are
  11.27x slower for candidate lookup and
  3.65x slower through `GetOutPort`. Relative to the
  previous rule interpreter, candidate lookup improves from 545.00 ns
  to 347.00 ns, and `GetOutPort` improves from
  677.88 ns to 414.90 ns.
- On the fixed-seed random corpus, the gaps narrow to
  2.38x and
  1.37x. Candidate lookup improves from
  996.43 ns to 581.81 ns, and
  `GetOutPort` improves from 1036.89 ns to
  775.97 ns.
- The existing CTP workload uses node-level destination addresses. The index now derives the
  aggregate next-hop set directly instead of calling port-specific lookup for every destination
  port. It is
  18.25x slower for candidate lookup and
  10.04x slower through `GetOutPort`, but candidate
  lookup improves from 7111.37 ns to
  820.40 ns and `GetOutPort` improves from
  7158.31 ns to 1047.46 ns.

All six table/index pairs completed with zero missing routes and matching full-candidate
checksums. A separate validation run covered all 9,427,968 destination-port queries and all
2,145,280 valid node-primary queries. The checksum includes every returned candidate port.

## Measurements

All latency values are medians across three independent processes, in nanoseconds per query.
Each process P50 and P99 is a nearest-rank percentile of the per-query mean within each timed
4,096-query batch; the table reports the median of those process-level values. These are not
individual packet tail latencies.

| Distribution | Path | Table mean | Table P50 | Table P99 | Table QPS | Rule mean | Rule P50 | Rule P99 | Rule QPS | Rule/table mean |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Full corpus, sequential | Candidate lookup | 30.80 | 27.74 | 68.60 | 32.47 M | 347.00 | 320.98 | 596.26 | 2.88 M | 11.27x |
| Full corpus, sequential | GetOutPort | 113.57 | 109.26 | 190.23 | 8.80 M | 414.90 | 370.99 | 742.42 | 2.41 M | 3.65x |
| Full corpus, random | Candidate lookup | 244.52 | 239.01 | 334.72 | 4.09 M | 581.81 | 580.39 | 882.78 | 1.72 M | 2.38x |
| Full corpus, random | GetOutPort | 565.79 | 553.49 | 818.23 | 1.77 M | 775.97 | 724.13 | 1,371.00 | 1.29 M | 1.37x |
| Existing workload, endpoints | Candidate lookup | 44.95 | 44.61 | 66.05 | 22.25 M | 820.40 | 797.72 | 1,112.65 | 1.22 M | 18.25x |
| Existing workload, endpoints | GetOutPort | 104.32 | 102.36 | 162.68 | 9.59 M | 1,047.46 | 1,040.62 | 1,558.41 | 954.69 K | 10.04x |

## Method

- Topology: 1,024 NPU hosts, 2,336 total nodes, 19,968 links.
- Routing inputs: 9,427,968 full-table rows versus 136 topology rules and the compact next-hop
  index built from 10,528 memberships and 19,968 physical links.
- Each row above uses 10,000,000 measured queries after 100,000 warmup queries.
- Table and topology-rule modes ran in independent processes and used identical query sequences.
- Candidate lookup reuses output vectors. `GetOutPort` uses the unmodified runtime entrypoint,
  including per-call candidate vectors, shortest-only filtering, and HASH port selection.
- Exhaustive `GetOutPort` queries use the current node primary address as `sip`; they measure
  lookup and HASH selection cost but do not claim to reproduce original per-hop packet headers.
- The sequential corpus sorts all valid `(currentNodeId, destinationIp)` keys. The random corpus
  uses a fixed, stateless SplitMix64 index with replacement.
- Median setup time was 14.749 s for the table and
  0.549 s for topology rules.
- Three independent process samples were collected per routing mode on the current macOS arm64
  host. Three samples support a median and visible range, but not a narrow confidence interval.

## Workload boundary

The third corpus comes from the existing 65,536-task 1024-NPU bidirectional CTP simulation
workload. It contains endpoint pairs only. It has no destination-port hint and no per-packet,
per-hop forwarding trace, so it cannot reconstruct intermediate switch queries and must not be
called a production or measured real-traffic distribution. A true traffic-distribution benchmark
requires a route-lookup trace containing at least current node and destination address.

Raw process measurements are in `lookup-1024-index-table-run*.json` and
`lookup-1024-index-rule-run*.json`; machine-readable comparisons are in
`lookup-1024-index-summary.json`.
