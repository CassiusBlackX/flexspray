#!/usr/bin/env python3
"""Compare formal T33 with a diagnostic-only direct LDST scheduling ablation."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
T34_ROOT = HERE / "evidence/t34-main-gap"
ABLATION_ROOT = T34_ROOT / "ldst-direct-ablation"
FORMAL_LIBRARY = ABLATION_ROOT / "formal-root/build/lib/libns3.44-unified-bus.dylib"
VARIANT_LIBRARY = ABLATION_ROOT / "variant-root/build/lib/libns3.44-unified-bus.dylib"
RUN_ROOT = ABLATION_ROOT / "runs-v1"
SIZES = (16_384_009, 16_384_000)


spec = importlib.util.spec_from_file_location(
    "p01_diagnosis", HERE / "run_p01_detailed_diagnosis.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the p01 diagnosis runner")
diagnosis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnosis)

diagnosis.t28.ROLES["main"] = {
    "source_root": REPO_ROOT,
    "revision": "T33 formal current@51c60c5",
}
diagnosis.t28.ROLES["current"] = {
    "source_root": REPO_ROOT,
    "revision": "diagnostic-only direct LDST scheduling",
}


def binary_for(role: str) -> Path:
    del role
    return REPO_ROOT / "build/scratch/ns3.44-ub-quick-example"


def library_for(role: str) -> Path:
    return FORMAL_LIBRARY if role == "main" else VARIANT_LIBRARY


diagnosis.t28.binary_for = binary_for
diagnosis.t28.library_for = library_for
original_materialize = diagnosis.materialize
original_run_once = diagnosis.run_once


def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
    case_dir = original_materialize(destination, role, sizes)
    if role == "main":
        attributes = case_dir / "network_attribute.txt"
        text = attributes.read_text(encoding="utf-8")
        if "UB_LIFECYCLE_TRACE_LEVEL" in text:
            raise RuntimeError("formal input unexpectedly defines lifecycle output")
        attributes.write_text(
            text + 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n', encoding="utf-8"
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
    global ABLATION_ROOT, FORMAL_LIBRARY, VARIANT_LIBRARY, RUN_ROOT
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="ldst-direct-ablation")
    parser.add_argument(
        "--description",
        default="Schedule LDST callbacks directly without tracked-event leases",
    )
    parser.add_argument("--run-version", default="runs-v1")
    args = parser.parse_args()
    ABLATION_ROOT = T34_ROOT / args.label
    FORMAL_LIBRARY = ABLATION_ROOT / "formal-root/build/lib/libns3.44-unified-bus.dylib"
    VARIANT_LIBRARY = ABLATION_ROOT / "variant-root/build/lib/libns3.44-unified-bus.dylib"
    RUN_ROOT = ABLATION_ROOT / args.run_version
    if RUN_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite LDST ablation evidence: {RUN_ROOT}")
    RUN_ROOT.mkdir(parents=True)
    diagnosis.EVIDENCE_ROOT = RUN_ROOT
    manifest = {
        "schema_version": 1,
        "diagnostic_only": True,
        "formal_library_sha256": diagnosis.t28.sha256(FORMAL_LIBRARY),
        "variant_library_sha256": diagnosis.t28.sha256(VARIANT_LIBRARY),
        "binary_sha256": diagnosis.t28.sha256(binary_for("main")),
        "ablation": args.description,
        "preserved": "same callbacks, schedule timestamp, task state, packet path, queue path",
        "sizes": SIZES,
        "orders": diagnosis.ORDERS,
        "execution": "one warm-up per role and five counterbalanced pairs, strictly serial",
    }
    RUN_ROOT.joinpath("manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    scale = diagnosis.run_scale("16m", SIZES)
    formal_user = scale["roles"]["main"]["median_user_seconds"]
    summary = {
        "schema_version": 1,
        "diagnostic_only": True,
        "formal": scale["roles"]["main"],
        "ablation": scale["roles"]["current"],
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
