# Topology next-hop index validation

## Result

`UbTopologyNextHopIndex` compiles topology memberships and physical links into a compact runtime
index. Port-specific lookup no longer scans all rules attached to the destination, and node-primary
lookup no longer invokes port-specific lookup once per destination port.

The index preserves the existing `UbRuleRoutingTable` lookup order and falls back to the reference
topology interpreter when it cannot return a proven candidate set. Exact rules, range rules, the
full routing table, HASH/ADAPTIVE selection, and shortest/non-shortest policy are unchanged.

## Correctness

Independent table and topology-rule processes produced matching full-candidate checksums with no
missing routes for:

- all 9,427,968 valid destination-port queries;
- all 2,145,280 valid node-primary queries;
- the existing 65,536-task endpoint workload corpus.

The checksum includes every returned candidate port, not only the candidate count or first port.
The existing `unified-bus-structured-routing` test suite also passes for dimension, layered-switch,
Dragonfly, exact/range precedence, parser rejection, and table fallback behavior.

## Lookup performance

All values are medians of three independent processes with 10,000,000 measured queries and 100,000
warmup queries per row.

| Distribution | Path | Table | Previous rules | Next-hop index | Index/table |
| --- | --- | ---: | ---: | ---: | ---: |
| Port-specific sequential | Candidate lookup | 30.80 ns | 545.00 ns | 347.00 ns | 11.27x |
| Port-specific sequential | `GetOutPort` | 113.57 ns | 677.88 ns | 414.90 ns | 3.65x |
| Port-specific random | Candidate lookup | 244.52 ns | 996.43 ns | 581.81 ns | 2.38x |
| Port-specific random | `GetOutPort` | 565.79 ns | 1,036.89 ns | 775.97 ns | 1.37x |
| Existing workload endpoints | Candidate lookup | 44.95 ns | 7,111.37 ns | 820.40 ns | 18.25x |
| Existing workload endpoints | `GetOutPort` | 104.32 ns | 7,158.31 ns | 1,047.46 ns | 10.04x |

The largest improvement is the node-primary path: candidate lookup is 8.67x faster than the
previous rule interpreter, and `GetOutPort` is 6.83x faster. The complete table remains faster,
especially for the endpoint workload, so online lookup optimization is improved but not complete.

## Memory and setup

Three independent topology-rule RSS runs produced a median RSS after config release of
236,797,952 bytes (225.83 MiB). The previous rule runtime median was 221.48 MiB, so the new index
adds about 4.34 MiB. It remains about 86.3% below the 1,646.47 MiB full-table process.

The index is built from 10,528 topology memberships and 19,968 physical links. It does not contain
entries for the Cartesian product of current nodes, destination nodes, and destination ports.

In the dedicated RSS process, median topology-rule installation increased from about 0.794 s to
0.910 s. In the lookup benchmark, median parse plus install was 0.548 s for topology rules and
14.827 s for the full table. These setup numbers come from separate process groups and should not
be used as a narrow index-only timing estimate.

## Remaining work

- Replace remaining rule-distance and `HasUpstreamSource` work with type-specific compiled
  relationships where that reduces measured latency without per-destination expansion.
- Add explicit counters for fast-index hits and reference-interpreter fallbacks.
- Keep non-shortest candidate generation separate. The first index version accelerates topology
  shortest candidates and preserves exact/range non-shortest behavior.
- Fix source injection so selecting a non-shortest port consumes the UB one-detour allowance before
  the next switch receives the packet.

Detailed current measurements are in `lookup-1024-index-report.md` and
`lookup-1024-index-summary.json`.
