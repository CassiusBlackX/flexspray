#!/usr/bin/env python3
"""Interleave next, formal HEAD, and typed-tracked LDST in the same blocks."""

from __future__ import annotations

import importlib.util
import json
import statistics
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
NEXT_ROOT = Path("/Users/ytxing/workspace/ns-3-ub-routing-modes")
EVIDENCE_ROOT = HERE / "evidence/t34-main-gap/three-role-typed-tracked-v1"
SIZES = (16_384_009, 16_384_000)
ORDERS = (
    ("next", "formal", "typed"),
    ("typed", "next", "formal"),
    ("formal", "typed", "next"),
    ("next", "typed", "formal"),
    ("formal", "next", "typed"),
    ("typed", "formal", "next"),
)
ROLES = {
    "next": {
        "revision": "next@cf8ee683",
        "source_root": NEXT_ROOT,
        "binary": NEXT_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": NEXT_ROOT / "build/lib/libns3.44-unified-bus.dylib",
    },
    "formal": {
        "revision": "HEAD@51c60c5",
        "source_root": REPO_ROOT,
        "binary": REPO_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": HERE
        / "evidence/t34-main-gap/typed-tracked-ablation/formal-root/build/lib"
        / "libns3.44-unified-bus.dylib",
    },
    "typed": {
        "revision": "diagnostic typed-tracked LDST variant from HEAD@51c60c5",
        "source_root": REPO_ROOT,
        "binary": REPO_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": HERE
        / "evidence/t34-main-gap/typed-tracked-ablation/variant-root/build/lib"
        / "libns3.44-unified-bus.dylib",
    },
}


spec = importlib.util.spec_from_file_location(
    "p01_diagnosis", HERE / "run_p01_detailed_diagnosis.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the p01 diagnosis runner")
diagnosis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnosis)

original_materialize = diagnosis.materialize


def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
    return original_materialize(destination, "main" if role == "next" else "current", sizes)


diagnosis.materialize = materialize
diagnosis.t28.ROLES = {
    role: {"source_root": config["source_root"], "revision": config["revision"]}
    for role, config in ROLES.items()
}
diagnosis.t28.binary_for = lambda role: ROLES[role]["binary"]
diagnosis.t28.library_for = lambda role: ROLES[role]["library"]


def block_delta(block: dict, treatment: str, control: str) -> dict:
    treatment_result = block[treatment]
    control_result = block[control]
    return {
        "wall_delta_seconds": treatment_result["wall_seconds"]
        - control_result["wall_seconds"],
        "wall_delta_percent": diagnosis.t28.delta_percent(
            treatment_result["wall_seconds"], control_result["wall_seconds"]
        ),
        "user_delta_seconds": treatment_result["user_seconds"]
        - control_result["user_seconds"],
        "user_delta_percent": diagnosis.t28.delta_percent(
            treatment_result["user_seconds"], control_result["user_seconds"]
        ),
        "rss_delta_bytes": treatment_result["peak_rss_bytes"]
        - control_result["peak_rss_bytes"],
        "rss_delta_percent": diagnosis.t28.delta_percent(
            treatment_result["peak_rss_bytes"], control_result["peak_rss_bytes"]
        ),
    }


def summarize_comparison(items: list[dict]) -> dict:
    return {
        key: statistics.median(item[key] for item in items)
        for key in items[0]
    }


def main() -> int:
    if EVIDENCE_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite three-role evidence: {EVIDENCE_ROOT}")
    EVIDENCE_ROOT.mkdir(parents=True)
    manifest = {
        "schema_version": 1,
        "roles": {
            role: {
                "revision": config["revision"],
                "binary_sha256": diagnosis.t28.sha256(config["binary"]),
                "unified_bus_library_sha256": diagnosis.t28.sha256(config["library"]),
            }
            for role, config in ROLES.items()
        },
        "sizes": SIZES,
        "orders": ORDERS,
        "observability": "packet, port, per-hop, and lifecycle output OFF",
        "execution": "one warm-up per role and six counterbalanced blocks, strictly serial",
    }
    EVIDENCE_ROOT.joinpath("manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    warmups = {}
    for role in ROLES:
        warmups[role] = diagnosis.run_once(
            role, EVIDENCE_ROOT / f"warmup-{role}", SIZES
        )
        print(f"warmup {role}: {warmups[role]['wall_seconds']:.6f}s", flush=True)

    results = {role: [] for role in ROLES}
    comparisons = {
        "formal_vs_next": [],
        "typed_vs_next": [],
        "typed_vs_formal": [],
    }
    semantic = []
    for index, order in enumerate(ORDERS, 1):
        block = {}
        for position, role in enumerate(order, 1):
            item = diagnosis.run_once(
                role,
                EVIDENCE_ROOT / f"block-{index}-{position}-{role}",
                SIZES,
            )
            block[role] = item
            results[role].append(item)
            print(
                f"block {index}.{position} {role}: {item['wall_seconds']:.6f}s",
                flush=True,
            )

        next_hashes = block["next"]["artifact_hashes"]
        semantic.append(
            {
                "block": index,
                "formal_exact": block["formal"]["artifact_hashes"] == next_hashes,
                "typed_exact": block["typed"]["artifact_hashes"] == next_hashes,
            }
        )
        comparisons["formal_vs_next"].append(block_delta(block, "formal", "next"))
        comparisons["typed_vs_next"].append(block_delta(block, "typed", "next"))
        comparisons["typed_vs_formal"].append(block_delta(block, "typed", "formal"))

    summary = {
        "schema_version": 1,
        "roles": {
            role: diagnosis.median_fields(items) for role, items in results.items()
        },
        "comparisons": {
            name: {
                "median": summarize_comparison(items),
                "blocks": items,
            }
            for name, items in comparisons.items()
        },
        "semantic": semantic,
        "semantic_exact": all(
            item["formal_exact"] and item["typed_exact"] for item in semantic
        ),
    }
    EVIDENCE_ROOT.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
