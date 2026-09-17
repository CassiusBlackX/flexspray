import gc
import json
import sys
import tempfile
import time
import tracemalloc
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "scratch" / "ns-3-ub-tools"
sys.path.insert(0, str(TOOLS_DIR))

from structured_case import compile_structured_routes, serialize_structured_config
from test_topology_recognition import (
    build_dimension_fullmesh,
    build_fullmesh_with_multi_plane_clos,
)


def measure_compile(builder, routing_mode, cache_dir=None, prime_cache=False):
    if prime_cache:
        compile_structured_routes(builder(), routing_mode=routing_mode, cache_dir=cache_dir)
        gc.collect()

    graph = builder()
    tracemalloc.start()
    started = time.perf_counter()
    result = compile_structured_routes(
        graph,
        routing_mode=routing_mode,
        cache_dir=cache_dir,
        use_cache=cache_dir is not None,
    )
    elapsed = time.perf_counter() - started
    current_bytes, peak_bytes = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    payload_bytes = len(serialize_structured_config(graph, result.config).encode())
    return {
        "seconds": elapsed,
        "pythonCurrentBytes": current_bytes,
        "pythonPeakBytes": peak_bytes,
        "protocolBytes": payload_bytes,
        "cacheHit": result.cache_hit,
        "effectiveMode": result.config.mode,
        "ruleCoverage": result.config.rule_coverage,
        "tableLookups": len(result.config.table_lookups),
        "routingRules": len(result.config.routing_rules),
        "portGroups": len(result.config.port_groups),
    }


def main():
    builders = {
        "dimension-fullmesh-4x4": lambda: build_dimension_fullmesh(4, 4),
        "internal-fullmesh-with-multi-plane": build_fullmesh_with_multi_plane_clos,
    }
    results = {}
    for name, builder in builders.items():
        results[name] = {
            "tableCold": measure_compile(builder, "table"),
            "mixedCold": measure_compile(builder, "mixed"),
        }
        with tempfile.TemporaryDirectory() as cache_dir:
            results[name]["mixedCacheHit"] = measure_compile(
                builder,
                "mixed",
                cache_dir=cache_dir,
                prime_cache=True,
            )
    print(json.dumps(results, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
