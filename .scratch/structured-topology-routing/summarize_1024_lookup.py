import argparse
import json
import statistics
from pathlib import Path


def _load(path):
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def _measurement_key(measurement):
    return measurement["distribution"], measurement["lookupPath"]


def _round(value, digits=6):
    return round(float(value), digits)


def _median(results, field):
    return _round(statistics.median(result[field] for result in results))


def _format_ns(value):
    return f"{value:,.2f}"


def _format_qps(value):
    if value >= 1_000_000:
        return f"{value / 1_000_000:,.2f} M"
    if value >= 1_000:
        return f"{value / 1_000:,.2f} K"
    return f"{value:,.2f}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--table", action="append", type=Path, required=True)
    parser.add_argument("--topology-rule", action="append", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    table_runs = [_load(path) for path in args.table]
    topology_rule_runs = [_load(path) for path in args.topology_rule]
    if len(table_runs) != len(topology_rule_runs):
        raise RuntimeError("routing modes must contain the same number of process samples")
    if any(run["mode"] != "table" for run in table_runs) or any(
        run["mode"] != "topology-rule" for run in topology_rule_runs
    ):
        raise RuntimeError("benchmark files do not contain the expected routing modes")
    table = table_runs[0]
    topology_rule = topology_rule_runs[0]

    shared_fields = (
        "nodeCount",
        "exhaustiveCorpusQueries",
        "workloadCorpusQueries",
        "iterationsPerMeasurement",
        "warmupIterationsPerMeasurement",
        "randomSeed",
        "latencyStatistic",
    )
    all_runs = table_runs + topology_rule_runs
    for run in all_runs[1:]:
        for field in shared_fields:
            if run[field] != table[field]:
                raise RuntimeError(f"benchmark samples disagree on {field}")

    def index_measurements(runs):
        return [
            {
                _measurement_key(measurement): measurement
                for measurement in run["measurements"]
            }
            for run in runs
        ]

    table_measurement_runs = index_measurements(table_runs)
    rule_measurement_runs = index_measurements(topology_rule_runs)
    expected_keys = table_measurement_runs[0].keys()
    if any(run.keys() != expected_keys for run in table_measurement_runs) or any(
        run.keys() != expected_keys for run in rule_measurement_runs
    ):
        raise RuntimeError("benchmark samples measured different query paths")

    comparisons = []
    for key in expected_keys:
        table_results = [run[key] for run in table_measurement_runs]
        rule_results = [run[key] for run in rule_measurement_runs]
        results = table_results + rule_results
        if any(result["queryCount"] != results[0]["queryCount"] for result in results):
            raise RuntimeError(f"query count differs for {key}")
        if any(result["checksum"] != results[0]["checksum"] for result in results):
            raise RuntimeError(f"lookup checksum differs for {key}")
        if any(result["missing"] != 0 for result in results):
            raise RuntimeError(f"missing route detected for {key}")
        table_result = {
            "meanNs": _median(table_results, "meanNs"),
            "p50BatchMeanNs": _median(table_results, "p50BatchMeanNs"),
            "p99BatchMeanNs": _median(table_results, "p99BatchMeanNs"),
            "queriesPerSecond": _median(table_results, "queriesPerSecond"),
        }
        rule_result = {
            "meanNs": _median(rule_results, "meanNs"),
            "p50BatchMeanNs": _median(rule_results, "p50BatchMeanNs"),
            "p99BatchMeanNs": _median(rule_results, "p99BatchMeanNs"),
            "queriesPerSecond": _median(rule_results, "queriesPerSecond"),
        }
        comparisons.append(
            {
                "distribution": key[0],
                "lookupPath": key[1],
                "queryCount": results[0]["queryCount"],
                "batchSize": results[0]["batchSize"],
                "table": table_result,
                "topologyRule": rule_result,
                "topologyRuleOverTableMean": _round(
                    rule_result["meanNs"] / table_result["meanNs"]
                ),
                "tableMeanNsSamples": [
                    _round(result["meanNs"]) for result in table_results
                ],
                "topologyRuleMeanNsSamples": [
                    _round(result["meanNs"]) for result in rule_results
                ],
                "checksum": results[0]["checksum"],
                "missing": 0,
            }
        )

    summary = {
        "case": {
            "name": "1dpod-1024npu",
            "npuCount": 1024,
            "nodeCount": table["nodeCount"],
            "fullTableRoutes": table["exhaustiveCorpusQueries"],
            "topologyRules": 136,
        },
        "queryCorpora": {
            "exhaustive": {
                "queryCount": table["exhaustiveCorpusQueries"],
                "sequential": "sorted by currentNodeId and destinationIp",
                "random": "stateless SplitMix64 index with a fixed seed and replacement",
            },
            "existingWorkloadEndpoints": {
                "queryCount": table["workloadCorpusQueries"],
                "source": (
                    "lianqiuhu-ctp-8plane/scratch/"
                    "20260512-8plane-ctp-taack-bidir-load/traffic.csv"
                ),
                "classification": "existing synthetic simulation workload",
                "realTrafficTrace": False,
                "limitation": (
                    "traffic.csv has endpoint tasks but no destination-port hint or "
                    "intermediate forwarding-node sequence"
                ),
            },
        },
        "method": {
            "independentProcessPerRoutingMode": True,
            "processSamplesPerMode": len(table_runs),
            "aggregate": "median of independent process samples for each reported metric",
            "iterationsPerMeasurement": table["iterationsPerMeasurement"],
            "warmupIterationsPerMeasurement": table[
                "warmupIterationsPerMeasurement"
            ],
            "batchSize": comparisons[0]["batchSize"],
            "latencyStatistic": table["latencyStatistic"],
            "percentiles": "nearest-rank percentile over timed batch means",
            "candidateLookup": (
                "table GetShortestCandidates versus topology-rule "
                "TryGetRuleCandidates with reused output vectors"
            ),
            "getOutPort": (
                "both modes call UbRoutingProcess::GetOutPort with HASH routing and "
                "shortest-path-only selection"
            ),
            "routingKey": (
                "sip is the current node primary address, dip is the corpus destination, "
                "and sport varies with query index; exhaustive queries are not packet traces"
            ),
        },
        "setupSeconds": {
            "table": {
                "parseMedian": _median(table_runs, "parseSeconds"),
                "installMedian": _median(table_runs, "installSeconds"),
            },
            "topologyRule": {
                "parseMedian": _median(topology_rule_runs, "parseSeconds"),
                "installMedian": _median(topology_rule_runs, "installSeconds"),
            },
        },
        "rawSamples": {
            "table": [str(path) for path in args.table],
            "topologyRule": [str(path) for path in args.topology_rule],
        },
        "correctness": {
            "allChecksumsMatch": True,
            "missingQueries": 0,
            "fullPortCandidateEquivalenceQueries": 9_427_968,
            "fullNodeCandidateEquivalenceQueries": 2_145_280,
            "checksumCoversAllCandidatePorts": True,
        },
        "comparisons": comparisons,
    }
    args.summary.write_text(
        json.dumps(summary, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )

    display_names = {
        "exhaustive-sequential": "Full corpus, sequential",
        "exhaustive-random": "Full corpus, random",
        "existing-workload-endpoints": "Existing workload, endpoints",
        "candidate-lookup": "Candidate lookup",
        "get-out-port": "GetOutPort",
    }
    rows = []
    for comparison in comparisons:
        table_result = comparison["table"]
        rule_result = comparison["topologyRule"]
        rows.append(
            "| "
            + " | ".join(
                [
                    display_names[comparison["distribution"]],
                    display_names[comparison["lookupPath"]],
                    _format_ns(table_result["meanNs"]),
                    _format_ns(table_result["p50BatchMeanNs"]),
                    _format_ns(table_result["p99BatchMeanNs"]),
                    _format_qps(table_result["queriesPerSecond"]),
                    _format_ns(rule_result["meanNs"]),
                    _format_ns(rule_result["p50BatchMeanNs"]),
                    _format_ns(rule_result["p99BatchMeanNs"]),
                    _format_qps(rule_result["queriesPerSecond"]),
                    f'{comparison["topologyRuleOverTableMean"]:.2f}x',
                ]
            )
            + " |"
        )

    by_key = {
        (comparison["distribution"], comparison["lookupPath"]): comparison
        for comparison in comparisons
    }
    sequential_candidate = by_key[("exhaustive-sequential", "candidate-lookup")]
    sequential_out = by_key[("exhaustive-sequential", "get-out-port")]
    random_candidate = by_key[("exhaustive-random", "candidate-lookup")]
    random_out = by_key[("exhaustive-random", "get-out-port")]
    workload_candidate = by_key[("existing-workload-endpoints", "candidate-lookup")]
    workload_out = by_key[("existing-workload-endpoints", "get-out-port")]
    table_random_penalty_candidate = (
        random_candidate["table"]["meanNs"]
        / sequential_candidate["table"]["meanNs"]
    )
    rule_random_penalty_candidate = (
        random_candidate["topologyRule"]["meanNs"]
        / sequential_candidate["topologyRule"]["meanNs"]
    )
    table_random_penalty_out = (
        random_out["table"]["meanNs"] / sequential_out["table"]["meanNs"]
    )
    rule_random_penalty_out = (
        random_out["topologyRule"]["meanNs"]
        / sequential_out["topologyRule"]["meanNs"]
    )

    table_setup_median = statistics.median(
        run["parseSeconds"] + run["installSeconds"] for run in table_runs
    )
    rule_setup_median = statistics.median(
        run["parseSeconds"] + run["installSeconds"] for run in topology_rule_runs
    )

    old_sequential_candidate_ns = 545.00
    old_random_candidate_ns = 996.43
    old_workload_candidate_ns = 7111.37
    old_sequential_out_ns = 677.88
    old_random_out_ns = 1036.89
    old_workload_out_ns = 7158.31

    report = f"""# 1024-NPU topology next-hop index benchmark

## Result

`UbTopologyNextHopIndex` removes repeated topology-rule scans and gives node-level destination
addresses a direct lookup path. The full routing table remains faster in this benchmark, but the
largest topology-rule bottleneck is removed:

- On the sequential exhaustive corpus, indexed topology rules are
  {sequential_candidate['topologyRuleOverTableMean']:.2f}x slower for candidate lookup and
  {sequential_out['topologyRuleOverTableMean']:.2f}x slower through `GetOutPort`. Relative to the
  previous rule interpreter, candidate lookup improves from {old_sequential_candidate_ns:.2f} ns
  to {sequential_candidate['topologyRule']['meanNs']:.2f} ns, and `GetOutPort` improves from
  {old_sequential_out_ns:.2f} ns to {sequential_out['topologyRule']['meanNs']:.2f} ns.
- On the fixed-seed random corpus, the gaps narrow to
  {random_candidate['topologyRuleOverTableMean']:.2f}x and
  {random_out['topologyRuleOverTableMean']:.2f}x. Candidate lookup improves from
  {old_random_candidate_ns:.2f} ns to {random_candidate['topologyRule']['meanNs']:.2f} ns, and
  `GetOutPort` improves from {old_random_out_ns:.2f} ns to
  {random_out['topologyRule']['meanNs']:.2f} ns.
- The existing CTP workload uses node-level destination addresses. The index now derives the
  aggregate next-hop set directly instead of calling port-specific lookup for every destination
  port. It is
  {workload_candidate['topologyRuleOverTableMean']:.2f}x slower for candidate lookup and
  {workload_out['topologyRuleOverTableMean']:.2f}x slower through `GetOutPort`, but candidate
  lookup improves from {old_workload_candidate_ns:.2f} ns to
  {workload_candidate['topologyRule']['meanNs']:.2f} ns and `GetOutPort` improves from
  {old_workload_out_ns:.2f} ns to {workload_out['topologyRule']['meanNs']:.2f} ns.

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
{chr(10).join(rows)}

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
- Median setup time was {table_setup_median:.3f} s for the table and
  {rule_setup_median:.3f} s for topology rules.
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
"""
    args.report.write_text(report, encoding="utf-8")


if __name__ == "__main__":
    main()
