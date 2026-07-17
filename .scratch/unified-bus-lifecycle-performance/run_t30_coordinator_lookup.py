#!/usr/bin/env python3
"""Compare the T30 coordinator lookup with the exact pre-T30 library."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path


HERE = Path(__file__).resolve().parent
PRE_ROOT = HERE / "evidence/t29-allocator/pre-root"
T30_ROOT = HERE / "evidence/t30-coordinator-lookup/t30-root"
RUN_ROOT = HERE / "evidence/t30-coordinator-lookup/runs-v1"
SIZES = (16_384_009, 16_384_000)


spec = importlib.util.spec_from_file_location(
    "p01_diagnosis", HERE / "run_p01_detailed_diagnosis.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the p01 diagnosis runner")
diagnosis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnosis)


diagnosis.t28.ROLES["main"] = {
    "source_root": PRE_ROOT,
    "revision": "9d53f25 source / db335a0 frozen library",
}
diagnosis.t28.ROLES["current"] = {
    "source_root": T30_ROOT,
    "revision": "T30 working tree",
}


original_materialize = diagnosis.materialize


def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
    case_dir = original_materialize(destination, role, sizes)
    if role == "main":
        attributes = case_dir / "network_attribute.txt"
        text = attributes.read_text(encoding="utf-8")
        if "UB_LIFECYCLE_TRACE_LEVEL" in text:
            raise RuntimeError("pre-T30 input unexpectedly defines lifecycle output")
        attributes.write_text(
            text + 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n', encoding="utf-8"
        )
    return case_dir


diagnosis.materialize = materialize
original_run_once = diagnosis.run_once


def run_once(role: str, destination: Path, sizes: tuple[int, int]) -> dict:
    library_dir = diagnosis.t28.library_for(role).parent
    previous = os.environ.get("DYLD_LIBRARY_PATH")
    os.environ["DYLD_LIBRARY_PATH"] = str(library_dir)
    try:
        return original_run_once(role, destination, sizes)
    finally:
        if previous is None:
            os.environ.pop("DYLD_LIBRARY_PATH", None)
        else:
            os.environ["DYLD_LIBRARY_PATH"] = previous


diagnosis.run_once = run_once


def main() -> int:
    if RUN_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite T30 evidence: {RUN_ROOT}")
    RUN_ROOT.mkdir(parents=True)
    diagnosis.EVIDENCE_ROOT = RUN_ROOT

    manifest = {
        "schema_version": 1,
        "pre_t30_revision": diagnosis.t28.ROLES["main"]["revision"],
        "pre_t30_binary_sha256": diagnosis.t28.sha256(diagnosis.t28.binary_for("main")),
        "pre_t30_library_sha256": diagnosis.t28.sha256(diagnosis.t28.library_for("main")),
        "t30_binary_sha256": diagnosis.t28.sha256(diagnosis.t28.binary_for("current")),
        "t30_library_sha256": diagnosis.t28.sha256(diagnosis.t28.library_for("current")),
        "sizes": SIZES,
        "orders": diagnosis.ORDERS,
        "execution": "one warm-up per role and five counterbalanced pairs, strictly serial",
        "observability": "explicit lifecycle OFF; packet, port, and per-hop trace OFF",
        "retention_gate": {
            "paired_median_wall_percent": -1.0,
            "minimum_non_regressing_pairs": 4,
            "maximum_user_cpu_regression_percent": 1.0,
            "maximum_rss_regression_percent": 1.0,
        },
    }
    RUN_ROOT.joinpath("manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    scale = diagnosis.run_scale("16m", SIZES)
    non_regressing_pairs = sum(item["wall_delta_percent"] <= 0 for item in scale["pairs"])
    pre_user = scale["roles"]["main"]["median_user_seconds"]
    user_delta_percent = (
        100.0 * scale["median_paired_user_delta_seconds"] / pre_user if pre_user else 0.0
    )
    summary = {
        "schema_version": 1,
        "pre_t30": scale["roles"]["main"],
        "t30": scale["roles"]["current"],
        "paired_wall_delta_seconds": scale["median_paired_wall_delta_seconds"],
        "paired_wall_delta_percent": scale["median_paired_wall_delta_percent"],
        "paired_user_delta_seconds": scale["median_paired_user_delta_seconds"],
        "paired_user_delta_percent": user_delta_percent,
        "paired_rss_delta_bytes": scale["median_paired_rss_delta_bytes"],
        "paired_rss_delta_percent": scale["median_paired_rss_delta_percent"],
        "non_regressing_pairs": non_regressing_pairs,
        "pairs": scale["pairs"],
        "semantic": scale["semantic"],
        "semantic_exact": scale["semantic_exact"],
    }
    summary["retain"] = (
        summary["semantic_exact"]
        and summary["paired_wall_delta_percent"] <= -1.0
        and summary["non_regressing_pairs"] >= 4
        and summary["paired_user_delta_percent"] <= 1.0
        and summary["paired_rss_delta_percent"] <= 1.0
    )
    RUN_ROOT.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
