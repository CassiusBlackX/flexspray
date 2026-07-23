#!/usr/bin/env python3
"""Compare the T29 allocator hot-path change with its exact pre-change library."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path


HERE = Path(__file__).resolve().parent
CONTROL_ROOT = HERE / "evidence/t29-allocator"
PRE_ROOT = CONTROL_ROOT / "pre-root"
T29_ROOT = CONTROL_ROOT / "t29-root"
RUN_ROOT = CONTROL_ROOT / "runs-v2"
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
    "revision": "db335a0068b27b46c4cddd6b04365ac21d3edc0b",
}
diagnosis.t28.ROLES["current"] = {
    "source_root": T29_ROOT,
    "revision": "T29 working tree",
}


original_materialize = diagnosis.materialize


def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
    case_dir = original_materialize(destination, role, sizes)
    if role == "main":
        attributes = case_dir / "network_attribute.txt"
        text = attributes.read_text(encoding="utf-8")
        if "UB_LIFECYCLE_TRACE_LEVEL" in text:
            raise RuntimeError("pre-T29 input unexpectedly defines lifecycle output")
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
        raise RuntimeError(f"refusing to overwrite T29 evidence: {RUN_ROOT}")
    RUN_ROOT.mkdir(parents=True)
    diagnosis.EVIDENCE_ROOT = RUN_ROOT

    manifest = {
        "schema_version": 1,
        "pre_t29_revision": diagnosis.t28.ROLES["main"]["revision"],
        "pre_t29_binary_sha256": diagnosis.t28.sha256(diagnosis.t28.binary_for("main")),
        "pre_t29_library_sha256": diagnosis.t28.sha256(diagnosis.t28.library_for("main")),
        "t29_binary_sha256": diagnosis.t28.sha256(diagnosis.t28.binary_for("current")),
        "t29_library_sha256": diagnosis.t28.sha256(diagnosis.t28.library_for("current")),
        "sizes": SIZES,
        "orders": diagnosis.ORDERS,
        "execution": "one warm-up per role and five counterbalanced pairs, strictly serial",
        "observability": "explicit lifecycle OFF; packet, port, and per-hop trace OFF",
    }
    RUN_ROOT.joinpath("manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    scale = diagnosis.run_scale("16m", SIZES)
    summary = {
        "schema_version": 1,
        "pre_t29": scale["roles"]["main"],
        "t29": scale["roles"]["current"],
        "paired_wall_delta_seconds": scale["median_paired_wall_delta_seconds"],
        "paired_wall_delta_percent": scale["median_paired_wall_delta_percent"],
        "paired_user_delta_seconds": scale["median_paired_user_delta_seconds"],
        "paired_rss_delta_bytes": scale["median_paired_rss_delta_bytes"],
        "paired_rss_delta_percent": scale["median_paired_rss_delta_percent"],
        "pairs": scale["pairs"],
        "semantic": scale["semantic"],
        "semantic_exact": scale["semantic_exact"],
        "useful_gain": scale["median_paired_wall_delta_percent"] <= -5.0,
    }
    RUN_ROOT.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
