#!/usr/bin/env python3
"""Counterbalance two frozen historical Unified Bus builds on the p01 loop."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
SIZES = (16_384_009, 16_384_000)


spec = importlib.util.spec_from_file_location(
    "p01_diagnosis", HERE / "run_p01_detailed_diagnosis.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the p01 diagnosis runner")
diagnosis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnosis)

BASELINE_BINARY = Path()
BASELINE_LIBRARY = Path()
CURRENT_BINARY = Path()
CURRENT_LIBRARY = Path()
BASELINE_SUPPORTS_LIFECYCLE_OFF = False
CURRENT_SUPPORTS_LIFECYCLE_OFF = False


def binary_for(role: str) -> Path:
    return BASELINE_BINARY if role == "main" else CURRENT_BINARY


def library_for(role: str) -> Path:
    return BASELINE_LIBRARY if role == "main" else CURRENT_LIBRARY


diagnosis.t28.binary_for = binary_for
diagnosis.t28.library_for = library_for
original_materialize = diagnosis.materialize
original_run_once = diagnosis.run_once


def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
    case_dir = original_materialize(destination, role, sizes)
    if role == "main" and BASELINE_SUPPORTS_LIFECYCLE_OFF:
        attributes = case_dir / "network_attribute.txt"
        text = attributes.read_text(encoding="utf-8")
        if "UB_LIFECYCLE_TRACE_LEVEL" in text:
            raise RuntimeError("baseline input unexpectedly defines lifecycle output")
        attributes.write_text(
            text + 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n', encoding="utf-8"
        )
    if role == "current" and not CURRENT_SUPPORTS_LIFECYCLE_OFF:
        attributes = case_dir / "network_attribute.txt"
        text = attributes.read_text(encoding="utf-8")
        attributes.write_text(
            text.replace('global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n', ""),
            encoding="utf-8",
        )
    return case_dir


diagnosis.materialize = materialize


def run_once(role: str, destination: Path, sizes: tuple[int, int]) -> dict:
    previous = os.environ.get("DYLD_LIBRARY_PATH")
    os.environ["DYLD_LIBRARY_PATH"] = str(library_for(role).parent)
    try:
        return original_run_once(role, destination, sizes)
    finally:
        if previous is None:
            os.environ.pop("DYLD_LIBRARY_PATH", None)
        else:
            os.environ["DYLD_LIBRARY_PATH"] = previous


diagnosis.run_once = run_once


def main() -> int:
    global BASELINE_BINARY, BASELINE_LIBRARY, CURRENT_BINARY, CURRENT_LIBRARY
    global BASELINE_SUPPORTS_LIFECYCLE_OFF, CURRENT_SUPPORTS_LIFECYCLE_OFF
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    parser.add_argument("--baseline-revision", required=True)
    parser.add_argument("--baseline-binary", required=True, type=Path)
    parser.add_argument("--baseline-library", required=True, type=Path)
    parser.add_argument("--current-revision", required=True)
    parser.add_argument("--current-binary", required=True, type=Path)
    parser.add_argument("--current-library", required=True, type=Path)
    parser.add_argument("--baseline-supports-lifecycle-off", action="store_true")
    parser.add_argument("--current-supports-lifecycle-off", action="store_true")
    args = parser.parse_args()

    BASELINE_BINARY = args.baseline_binary
    BASELINE_LIBRARY = args.baseline_library
    CURRENT_BINARY = args.current_binary
    CURRENT_LIBRARY = args.current_library
    BASELINE_SUPPORTS_LIFECYCLE_OFF = args.baseline_supports_lifecycle_off
    CURRENT_SUPPORTS_LIFECYCLE_OFF = args.current_supports_lifecycle_off
    diagnosis.t28.ROLES["main"] = {
        "source_root": REPO_ROOT,
        "revision": args.baseline_revision,
    }
    diagnosis.t28.ROLES["current"] = {
        "source_root": REPO_ROOT,
        "revision": args.current_revision,
    }
    run_root = HERE / "evidence/t34-main-gap" / args.label
    if run_root.exists():
        raise RuntimeError(f"refusing to overwrite historical evidence: {run_root}")
    run_root.mkdir(parents=True)
    diagnosis.EVIDENCE_ROOT = run_root
    manifest = {
        "schema_version": 1,
        "baseline_revision": args.baseline_revision,
        "current_revision": args.current_revision,
        "baseline_binary_sha256": diagnosis.t28.sha256(BASELINE_BINARY),
        "baseline_library_sha256": diagnosis.t28.sha256(BASELINE_LIBRARY),
        "current_binary_sha256": diagnosis.t28.sha256(CURRENT_BINARY),
        "current_library_sha256": diagnosis.t28.sha256(CURRENT_LIBRARY),
        "sizes": SIZES,
        "orders": diagnosis.ORDERS,
        "execution": "one warm-up per role and five counterbalanced pairs, strictly serial",
        "observability": (
            "packet, port, per-hop, and lifecycle output OFF"
            if BASELINE_SUPPORTS_LIFECYCLE_OFF and CURRENT_SUPPORTS_LIFECYCLE_OFF
            else "packet, port, per-hop output OFF; lifecycle level unavailable"
        ),
    }
    run_root.joinpath("manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    scale = diagnosis.run_scale("16m", SIZES)
    baseline_user = scale["roles"]["main"]["median_user_seconds"]
    summary = {
        "schema_version": 1,
        "baseline": scale["roles"]["main"],
        "current": scale["roles"]["current"],
        "paired_wall_delta_seconds": scale["median_paired_wall_delta_seconds"],
        "paired_wall_delta_percent": scale["median_paired_wall_delta_percent"],
        "paired_user_delta_percent": (
            100.0 * scale["median_paired_user_delta_seconds"] / baseline_user
            if baseline_user
            else 0.0
        ),
        "paired_rss_delta_percent": scale["median_paired_rss_delta_percent"],
        "pairs": scale["pairs"],
        "semantic": scale["semantic"],
        "semantic_exact": scale["semantic_exact"],
    }
    run_root.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
