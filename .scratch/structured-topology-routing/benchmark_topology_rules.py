import argparse
import gc
import json
import sys
import time
import tracemalloc
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "scratch" / "ns-3-ub-tools"
sys.path.insert(0, str(TOOLS_DIR))

from case_topology_loader import load_case_topology
from route_split import (
    CompiledRouteConfig,
    build_shortest_route_lookups_for_destination,
    compare_topology_rules_to_brute_force,
    group_route_candidates,
)
from structured_case import compile_structured_routes, serialize_structured_config
from test_topology_recognition import build_dimension_network, build_dragonfly


DEFAULT_CASE_ROOT = Path(
    "/Users/ytxing/workspace/lianqiuhu-ctp-8plane/a5superpod"
)
DEFAULT_LARGE_CASE = (
    REPO_ROOT / ".scratch" / "structured-topology-routing" / "1dpod-1024npu"
)


def _measure_compile(factory, routing_mode, track_memory=True):
    gc.collect()
    graph = factory()
    if track_memory:
        tracemalloc.start()
    started = time.perf_counter()
    result = compile_structured_routes(
        graph,
        routing_mode=routing_mode,
        use_cache=False,
    )
    elapsed = time.perf_counter() - started
    if track_memory:
        current_bytes, peak_bytes = tracemalloc.get_traced_memory()
        tracemalloc.stop()
    else:
        current_bytes, peak_bytes = None, None
    payload_bytes = len(serialize_structured_config(graph, result.config).encode())
    return graph, result, {
        "seconds": elapsed,
        "pythonCurrentBytes": current_bytes,
        "pythonPeakBytes": peak_bytes,
        "protocolBytes": payload_bytes,
        "tableLookups": len(result.config.table_lookups),
        "exactRules": len(result.config.routing_rules),
        "rangeRules": len(result.config.routing_range_rules),
        "topologyRules": len(result.config.topology_rules),
        "topologyNodeMemberships": sum(
            len(rule.nodes) for rule in result.config.topology_rules
        ),
    }


def _verify(factory):
    graph, rule_result, _rule_metrics = _measure_compile(factory, "mixed")
    started = time.perf_counter()
    last_reported = 0

    def report(completed, total):
        nonlocal last_reported
        if total >= 256 and (completed == total or completed - last_reported >= 64):
            print(f"verified {completed}/{total} destinations", flush=True)
            last_reported = completed

    comparison = compare_topology_rules_to_brute_force(
        graph,
        rule_result.config.topology_rules,
        progress_callback=report,
    )
    verify_seconds = time.perf_counter() - started
    return {
        "bruteForceLookups": comparison.brute_force_lookups,
        "ruleLookups": comparison.rule_lookups,
        "missing": comparison.missing_lookups,
        "extra": comparison.extra_lookups,
        "mismatched": comparison.mismatched_lookups,
        "mismatchExamples": comparison.mismatch_examples,
        "seconds": verify_seconds,
    }


def _measure_streaming_brute_force(factory, track_memory=True):
    gc.collect()
    graph = factory()
    empty_config = CompiledRouteConfig(
        port_groups=(),
        routing_rules=(),
        routing_range_rules=(),
        topology_rules=(),
        table_lookups={},
        mode="table",
        rule_coverage=0.0,
        rule_lookup_count=0,
        baseline_lookup_count=0,
    )
    empty_payload_bytes = len(
        serialize_structured_config(graph, empty_config).encode()
    )
    table_rows_bytes = 0
    lookup_count = 0
    if track_memory:
        tracemalloc.start()
    started = time.perf_counter()
    for destination in sorted(graph.host_ids):
        lookups = build_shortest_route_lookups_for_destination(graph, destination)
        lookup_count += len(lookups)
        for (current, destination_node, destination_port), metrics in lookups.items():
            candidates = group_route_candidates(metrics)
            shortest = " ".join(map(str, candidates.shortest_ports))
            non_shortest = " ".join(map(str, candidates.non_shortest_ports))
            line = (
                f"{current} {destination_node} {destination_port} "
                f"{len(candidates.shortest_ports)}"
                f"{(' ' + shortest) if shortest else ''} "
                f"{len(candidates.non_shortest_ports)}"
                f"{(' ' + non_shortest) if non_shortest else ''}\n"
            )
            table_rows_bytes += len(line.encode())
    elapsed = time.perf_counter() - started
    if track_memory:
        current_bytes, peak_bytes = tracemalloc.get_traced_memory()
        tracemalloc.stop()
    else:
        current_bytes, peak_bytes = None, None
    table_header_delta = len(f"TABLE {lookup_count}\n") - len("TABLE 0\n")
    return {
        "seconds": elapsed,
        "pythonCurrentBytes": current_bytes,
        "pythonPeakBytes": peak_bytes,
        "protocolBytes": empty_payload_bytes + table_header_delta + table_rows_bytes,
        "tableLookups": lookup_count,
        "exactRules": 0,
        "rangeRules": 0,
        "topologyRules": 0,
        "topologyNodeMemberships": 0,
        "memoryMode": "streamed-one-destination-at-a-time",
    }


def _case_factories(case_root, large_case):
    return {
        "ubx-16npu": (
            lambda: load_case_topology(case_root / "ubx-16npu"),
            False,
        ),
        "1dpod-64npu": (
            lambda: load_case_topology(case_root / "1dpod-64npu"),
            False,
        ),
        "1dpod-1024npu": (
            lambda: load_case_topology(large_case),
            True,
        ),
        "nd-fullmesh-4x4x4": (
            lambda: build_dimension_network(
                (4, 4, 4),
                ("fullMesh", "fullMesh", "fullMesh"),
            ),
            False,
        ),
        "dragonfly-a4-h1": (
            lambda: build_dragonfly(
                routers_per_group=4,
                global_links_per_router=1,
            ),
            False,
        ),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case-root", type=Path, default=DEFAULT_CASE_ROOT)
    parser.add_argument("--large-case", type=Path, default=DEFAULT_LARGE_CASE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    results = {}
    for name, (factory, stream_brute_force) in _case_factories(
        args.case_root,
        args.large_case,
    ).items():
        print(f"benchmarking {name}", flush=True)
        if stream_brute_force:
            table_metrics = _measure_streaming_brute_force(factory)
            pure_table_metrics = _measure_streaming_brute_force(
                factory,
                track_memory=False,
            )
        else:
            _table_graph, _table_result, table_metrics = _measure_compile(factory, "table")
            _pure_table_graph, _pure_table_result, pure_table_metrics = _measure_compile(
                factory,
                "table",
                track_memory=False,
            )
        _rule_graph, _rule_result, rule_metrics = _measure_compile(factory, "mixed")
        _pure_rule_graph, _pure_rule_result, pure_rule_metrics = _measure_compile(
            factory,
            "mixed",
            track_memory=False,
        )
        table_metrics["secondsWithoutMemoryTracking"] = pure_table_metrics["seconds"]
        rule_metrics["secondsWithoutMemoryTracking"] = pure_rule_metrics["seconds"]
        results[name] = {
            "bruteForce": table_metrics,
            "topologyRules": rule_metrics,
            "equivalence": _verify(factory),
        }

    encoded = json.dumps(results, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")


if __name__ == "__main__":
    main()
