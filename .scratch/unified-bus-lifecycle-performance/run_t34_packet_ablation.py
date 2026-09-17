#!/usr/bin/env python3
"""Measure a temporary OFF packet-ledger ablation against T34 main/current."""

from __future__ import annotations

import importlib.util
import argparse
import json
import statistics
from pathlib import Path


HERE = Path(__file__).resolve().parent
BASE_SUMMARY = HERE / "evidence/t34-main-gap/runs-v1/summary.json"
SIZES = (16_384_009, 16_384_000)


spec = importlib.util.spec_from_file_location(
    "p01_diagnosis", HERE / "run_p01_detailed_diagnosis.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the p01 diagnosis runner")
diagnosis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnosis)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="packet-ablation-v1")
    parser.add_argument(
        "--description",
        default="OFF bypasses packet registration and packet terminal ledger",
    )
    args = parser.parse_args()
    output_root = HERE / "evidence/t34-main-gap" / args.label
    if output_root.exists():
        raise RuntimeError(f"refusing to overwrite ablation evidence: {output_root}")
    output_root.mkdir(parents=True)
    warmup = diagnosis.run_once("current", output_root / "warmup", SIZES)
    print(f"warmup: {warmup['wall_seconds']:.6f}s", flush=True)
    measured = []
    for index in range(1, 6):
        result = diagnosis.run_once("current", output_root / f"measured-{index}", SIZES)
        measured.append(result)
        print(f"measured-{index}: {result['wall_seconds']:.6f}s", flush=True)

    baseline = json.loads(BASE_SUMMARY.read_text(encoding="utf-8"))
    wall = statistics.median(item["wall_seconds"] for item in measured)
    user = statistics.median(item["user_seconds"] for item in measured)
    rss = statistics.median(item["peak_rss_bytes"] for item in measured)
    main_wall = baseline["main"]["median_wall_seconds"]
    current_wall = baseline["current"]["median_wall_seconds"]
    summary = {
        "schema_version": 1,
        "diagnostic_only": True,
        "ablation": args.description,
        "median_wall_seconds": wall,
        "median_user_seconds": user,
        "median_peak_rss_bytes": rss,
        "wall_delta_vs_main_percent": 100.0 * (wall / main_wall - 1.0),
        "wall_delta_vs_current_percent": 100.0 * (wall / current_wall - 1.0),
        "semantic_hashes": [item["artifact_hashes"] for item in measured],
    }
    output_root.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
