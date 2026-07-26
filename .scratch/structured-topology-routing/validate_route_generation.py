#!/usr/bin/env python3
"""Validate prepared route generation on real and synthetic topology corpora."""

import argparse
import json
import sys
import tempfile
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "scratch" / "ns-3-ub-tools"
sys.path.insert(0, str(TOOLS_DIR))

from case_topology_loader import load_case_topology
from structured_case import prepare_structured_case
from test_topology_recognition import (
    build_clos_with_port_extension,
    build_dimension_network,
    build_dragonfly,
    build_grouped_fullmesh_with_direct_switches,
    build_grouped_fullmesh_with_multi_plane_switches,
    build_ring_inside_dragonfly,
)


DEFAULT_CASE_ROOT = Path("/Users/ytxing/workspace/openusim-case-lab/a5superpod")


def _factories(case_root):
    return {
        "ubx-16npu-original": (
            lambda: load_case_topology(case_root / "ubx-16npu"),
            ("full", "structured", "mixed"),
        ),
        "ubx-shaped-simplified": (
            build_grouped_fullmesh_with_direct_switches,
            ("full", "structured", "mixed"),
        ),
        "1dpod-64npu-original": (
            lambda: load_case_topology(case_root / "1dpod-64npu"),
            ("full", "structured", "mixed"),
        ),
        "1dpod-shaped-simplified": (
            build_grouped_fullmesh_with_multi_plane_switches,
            ("full", "structured", "mixed"),
        ),
        # UBMesh is not treated as an external standard name here. This fixture defines
        # its physical meaning explicitly as Path(4) cartesian Path(4).
        "ubmesh-4x4-cartesian": (
            lambda: build_dimension_network((4, 4), ("line", "line")),
            ("full", "structured", "mixed"),
        ),
        "torus-4x5-cartesian": (
            lambda: build_dimension_network((4, 5), ("ring", "ring")),
            ("full", "structured", "mixed"),
        ),
        "classic-dragonfly": (
            lambda: build_dragonfly(routers_per_group=3, global_links_per_router=1),
            ("full", "structured", "mixed"),
        ),
        "port-extension-mixed": (
            build_clos_with_port_extension,
            ("full", "mixed"),
        ),
        "ring-inside-dragonfly-mixed": (
            build_ring_inside_dragonfly,
            ("full", "mixed"),
        ),
    }


def _measure(factory, generation, output_dir):
    graph = factory()
    started = time.perf_counter()
    prepared = prepare_structured_case(
        graph,
        output_dir,
        route_generation=generation,
        use_cache=False,
        verify_routes=True,
    )
    elapsed = time.perf_counter() - started
    summary = json.loads(prepared.summary_path.read_text(encoding="utf-8"))
    return {
        "nodeCount": len(graph.nodes),
        "linkCount": len(graph.get_physical_topology().links),
        "seconds": elapsed,
        "topologyBytes": prepared.topology_path.stat().st_size,
        "compressedRoutingBytes": prepared.compressed_routing_path.stat().st_size,
        "routeLookupCount": summary["verification"]["routeLookupCount"],
        "mismatchCount": summary["verification"]["mismatchCount"],
        "effectiveRoutingMode": summary["effectiveRoutingMode"],
        "topologyRuleCount": summary["topologyRuleCount"],
        "routingRuleCount": summary["routingRuleCount"],
        "routingRangeRuleCount": summary["routingRangeRuleCount"],
        "tableLookupCount": summary["tableLookupCount"],
        "routeClassCount": summary["compressedRouting"]["routeClassCount"],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case-root", type=Path, default=DEFAULT_CASE_ROOT)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    results = {}
    with tempfile.TemporaryDirectory() as directory:
        temporary_root = Path(directory)
        for name, (factory, generations) in _factories(args.case_root).items():
            print(f"validating {name}", flush=True)
            results[name] = {
                generation: _measure(
                    factory,
                    generation,
                    temporary_root / name / generation,
                )
                for generation in generations
            }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(results, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
