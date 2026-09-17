#!/usr/bin/env python3
"""Compare runtime route outputs across generation and lookup modes."""

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "scratch" / "ns-3-ub-tools"
sys.path.insert(0, str(TOOLS_DIR))

from case_topology_loader import load_case_topology
from route_split import build_shortest_route_lookups_for_destination
from structured_case import prepare_structured_case
from test_topology_recognition import (
    build_clos_with_port_extension,
    build_dimension_network,
    build_dragonfly,
    build_grouped_fullmesh_with_direct_switches,
    build_grouped_fullmesh_with_multi_plane_switches,
    build_ring_inside_dragonfly,
)


QUERY_HEADER = struct.Struct("<8sQ")
QUERY_RECORD = struct.Struct("<II")
QUERY_MAGIC = b"UBQRY001"
DEFAULT_CASE_ROOT = Path("/Users/ytxing/workspace/openusim-case-lab/a5superpod")


def _node_ip(node_id, port_id):
    return (
        0x0A000000
        + (node_id // 256) * 0x00010000
        + (node_id % 256) * 0x00000100
        + port_id
        + 1
    )


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


def _write_queries(graph, path):
    records = []
    for destination in sorted(graph.host_ids):
        lookups = build_shortest_route_lookups_for_destination(graph, destination)
        records.extend(
            (current, _node_ip(destination_node, destination_port))
            for current, destination_node, destination_port in sorted(lookups)
        )
    with path.open("wb") as output:
        output.write(QUERY_HEADER.pack(QUERY_MAGIC, len(records)))
        for record in records:
            output.write(QUERY_RECORD.pack(*record))
    return len(records)


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _run(benchmark, mode, prepared, queries, output_dir, label, iterations):
    signature = output_dir / f"{label}.signature.bin"
    common_signature = output_dir / f"{label}.common-signature.bin"
    metrics = output_dir / f"{label}.json"
    binary = (
        prepared.compressed_routing_path
        if mode == "compressed-table-lookup"
        else "-"
    )
    subprocess.run(
        [
            str(benchmark),
            mode,
            str(prepared.topology_path),
            str(binary),
            str(queries),
            str(iterations),
            str(signature),
            str(common_signature),
            str(metrics),
            "20260722",
        ],
        check=True,
    )
    measurements = json.loads(metrics.read_text(encoding="utf-8"))
    return {
        "signatureSha256": _sha256(signature),
        "signatureBytes": signature.stat().st_size,
        "commonSignatureSha256": _sha256(common_signature),
        "commonSignatureBytes": common_signature.stat().st_size,
        "metrics": measurements,
    }


def _run_main(benchmark, graph, queries, output_dir, iterations):
    case_dir = output_dir / "main-csv"
    case_dir.mkdir(parents=True, exist_ok=True)
    graph.output_dir = str(case_dir)
    graph.gen_route_table(write_file=True)
    graph.write_config(include_transport=False)
    signature = output_dir / "main.common-signature.bin"
    metrics = output_dir / "main.json"
    subprocess.run(
        [
            str(benchmark),
            str(case_dir / "node.csv"),
            str(case_dir / "topology.csv"),
            str(case_dir / "routing_table.csv"),
            str(queries),
            str(iterations),
            str(signature),
            str(metrics),
            "20260722",
        ],
        check=True,
    )
    return {
        "commonSignatureSha256": _sha256(signature),
        "commonSignatureBytes": signature.stat().st_size,
        "metrics": json.loads(metrics.read_text(encoding="utf-8")),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", type=Path, required=True)
    parser.add_argument("--main-benchmark", type=Path)
    parser.add_argument("--case-root", type=Path, default=DEFAULT_CASE_ROOT)
    parser.add_argument("--iterations", type=int, default=100_000)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    results = {}
    with tempfile.TemporaryDirectory() as directory:
        temporary_root = Path(directory)
        for name, (factory, generations) in _factories(args.case_root).items():
            print(f"checking runtime output for {name}", flush=True)
            graph = factory()
            topology_dir = temporary_root / name
            queries = topology_dir / "queries.bin"
            topology_dir.mkdir(parents=True, exist_ok=True)
            query_count = _write_queries(graph, queries)
            prepared = {
                generation: prepare_structured_case(
                    graph,
                    topology_dir / generation,
                    route_generation=generation,
                    use_cache=False,
                    verify_routes=True,
                )
                for generation in generations
            }
            runs = {
                "full-text-table": _run(
                    args.benchmark,
                    "text-table-lookup",
                    prepared["full"],
                    queries,
                    topology_dir,
                    "full-text-table",
                    args.iterations,
                ),
                "full-compressed-table": _run(
                    args.benchmark,
                    "compressed-table-lookup",
                    prepared["full"],
                    queries,
                    topology_dir,
                    "full-compressed-table",
                    args.iterations,
                ),
                "mixed-compressed-table": _run(
                    args.benchmark,
                    "compressed-table-lookup",
                    prepared["mixed"],
                    queries,
                    topology_dir,
                    "mixed-compressed-table",
                    args.iterations,
                ),
                "mixed-rule-compute": _run(
                    args.benchmark,
                    "structured-rule-compute",
                    prepared["mixed"],
                    queries,
                    topology_dir,
                    "mixed-rule-compute",
                    args.iterations,
                ),
            }
            if "structured" in prepared:
                runs.update(
                    {
                        "structured-compressed-table": _run(
                            args.benchmark,
                            "compressed-table-lookup",
                            prepared["structured"],
                            queries,
                            topology_dir,
                            "structured-compressed-table",
                            args.iterations,
                        ),
                        "structured-rule-compute": _run(
                            args.benchmark,
                            "structured-rule-compute",
                            prepared["structured"],
                            queries,
                            topology_dir,
                            "structured-rule-compute",
                            args.iterations,
                        ),
                    }
                )
            portable_runs = {
                label: run for label, run in runs.items() if label != "full-text-table"
            }
            signatures = {run["signatureSha256"] for run in portable_runs.values()}
            common_signatures = {
                run["commonSignatureSha256"] for run in portable_runs.values()
            }
            if len(signatures) != 1 or len(common_signatures) != 1:
                hashes = {
                    label: {
                        "signatureSha256": run["signatureSha256"],
                        "commonSignatureSha256": run["commonSignatureSha256"],
                    }
                    for label, run in runs.items()
                }
                raise RuntimeError(
                    f"runtime route output differs for {name}: "
                    f"{json.dumps(hashes, sort_keys=True)}"
                )
            main_run = None
            if args.main_benchmark is not None:
                main_run = _run_main(
                    args.main_benchmark,
                    graph,
                    queries,
                    topology_dir,
                    args.iterations,
                )
                if main_run["commonSignatureSha256"] not in common_signatures:
                    raise RuntimeError(f"main runtime route output differs for {name}")
            results[name] = {
                "nodeCount": len(graph.nodes),
                "queryCount": query_count,
                "byteIdentical": True,
                "signatureSha256": next(iter(signatures)),
                "commonSignatureSha256": next(iter(common_signatures)),
                "embeddedTextMatchesPortable": (
                    runs["full-text-table"]["signatureSha256"]
                    == next(iter(signatures))
                ),
                "mainMatchesPortable": main_run is not None,
                "main": main_run,
                "runs": runs,
            }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
