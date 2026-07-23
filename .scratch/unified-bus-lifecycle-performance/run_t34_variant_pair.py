#!/usr/bin/env python3
"""Counterbalance pinned next and the formal T33 current build."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
BASELINE_ROOT = Path("/Users/ytxing/workspace/ns-3-ub-routing-modes")
CURRENT_ROOT = REPO_ROOT
RUN_ROOT = HERE / "evidence/t34-main-gap/next-vs-current-v2"
CURRENT_LIBRARY: Path | None = None
CURRENT_BINARY: Path | None = None
OMIT_CURRENT_LIFECYCLE_LEVEL = False
SIZES = (16_384_009, 16_384_000)


spec = importlib.util.spec_from_file_location(
    "p01_diagnosis", HERE / "run_p01_detailed_diagnosis.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the p01 diagnosis runner")
diagnosis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnosis)

diagnosis.t28.ROLES["main"] = {
    "source_root": BASELINE_ROOT,
    "revision": "next@cf8ee683",
}
diagnosis.t28.ROLES["current"] = {
    "source_root": CURRENT_ROOT,
    "revision": "T33 formal current@51c60c5",
}
original_materialize = diagnosis.materialize
original_run_once = diagnosis.run_once
original_library_for = diagnosis.t28.library_for
original_binary_for = diagnosis.t28.binary_for


def library_for(role: str) -> Path:
    if role == "current" and CURRENT_LIBRARY is not None:
        return CURRENT_LIBRARY
    return original_library_for(role)


diagnosis.t28.library_for = library_for


def binary_for(role: str) -> Path:
    if role == "current" and CURRENT_BINARY is not None:
        return CURRENT_BINARY
    return original_binary_for(role)


diagnosis.t28.binary_for = binary_for


def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
    case_dir = original_materialize(destination, role, sizes)
    attributes = case_dir / "network_attribute.txt"
    text = attributes.read_text(encoding="utf-8")
    if role == "main":
        return case_dir
    if OMIT_CURRENT_LIFECYCLE_LEVEL:
        attributes.write_text(
            text.replace('global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n', ""),
            encoding="utf-8",
        )
        return case_dir
    if "UB_LIFECYCLE_TRACE_LEVEL" in text:
        if 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"' not in text:
            raise RuntimeError("variant input defines a non-OFF lifecycle level")
    else:
        attributes.write_text(
            text + 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n', encoding="utf-8"
        )
    return case_dir


diagnosis.materialize = materialize


def run_once(role: str, destination: Path, sizes: tuple[int, int]) -> dict:
    previous = os.environ.get("DYLD_LIBRARY_PATH")
    os.environ["DYLD_LIBRARY_PATH"] = str(diagnosis.t28.library_for(role).parent)
    try:
        return original_run_once(role, destination, sizes)
    finally:
        if previous is None:
            os.environ.pop("DYLD_LIBRARY_PATH", None)
        else:
            os.environ["DYLD_LIBRARY_PATH"] = previous


diagnosis.run_once = run_once


def main() -> int:
    global CURRENT_BINARY, CURRENT_LIBRARY, OMIT_CURRENT_LIFECYCLE_LEVEL, RUN_ROOT
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="next-vs-current-v2")
    parser.add_argument("--current-library", type=Path)
    parser.add_argument("--current-binary", type=Path)
    parser.add_argument("--current-revision", default="T33 formal current@51c60c5")
    parser.add_argument("--omit-current-lifecycle-level", action="store_true")
    args = parser.parse_args()
    RUN_ROOT = HERE / "evidence/t34-main-gap" / args.label
    CURRENT_LIBRARY = args.current_library
    CURRENT_BINARY = args.current_binary
    OMIT_CURRENT_LIFECYCLE_LEVEL = args.omit_current_lifecycle_level
    diagnosis.t28.ROLES["current"]["revision"] = args.current_revision
    if RUN_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite paired evidence: {RUN_ROOT}")
    RUN_ROOT.mkdir(parents=True)
    diagnosis.EVIDENCE_ROOT = RUN_ROOT
    RUN_ROOT.joinpath("manifest.json").write_text(
        json.dumps(
            {
                "schema_version": 1,
                "baseline_revision": diagnosis.t28.ROLES["main"]["revision"],
                "current_revision": diagnosis.t28.ROLES["current"]["revision"],
                "baseline_source_root": str(BASELINE_ROOT),
                "current_source_root": str(CURRENT_ROOT),
                "sizes": SIZES,
                "execution": "one warm-up per role and five counterbalanced pairs, strictly serial",
                "observability": "packet, port, per-hop, and lifecycle output OFF",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    scale = diagnosis.run_scale("16m", SIZES)
    formal_user = scale["roles"]["main"]["median_user_seconds"]
    summary = {
        "schema_version": 1,
        "baseline_library_sha256": diagnosis.t28.sha256(diagnosis.t28.library_for("main")),
        "current_library_sha256": diagnosis.t28.sha256(
            diagnosis.t28.library_for("current")
        ),
        "baseline": scale["roles"]["main"],
        "current": scale["roles"]["current"],
        "paired_wall_delta_seconds": scale["median_paired_wall_delta_seconds"],
        "paired_wall_delta_percent": scale["median_paired_wall_delta_percent"],
        "paired_user_delta_percent": (
            100.0 * scale["median_paired_user_delta_seconds"] / formal_user
            if formal_user
            else 0.0
        ),
        "paired_rss_delta_percent": scale["median_paired_rss_delta_percent"],
        "pairs": scale["pairs"],
        "semantic": scale["semantic"],
        "semantic_exact": scale["semantic_exact"],
    }
    RUN_ROOT.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
