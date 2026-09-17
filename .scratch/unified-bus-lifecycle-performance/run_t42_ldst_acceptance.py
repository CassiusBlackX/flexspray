#!/usr/bin/env python3
"""Compare next, frozen pre-T42, and production typed LDST scheduling."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import signal
import statistics
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
NEXT_ROOT = Path("/Users/ytxing/workspace/ns-3-ub-routing-modes")
PRE_T42_ROOT = Path("/tmp/ns3-ub-t42-pre")
EVIDENCE_ROOT = HERE / "evidence/t42-ldst-typed-acceptance-v2"
FINAL_BYTE_ROOT = HERE / "evidence/t42-ldst-final-byte-v1"
SIZES = (16_384_009, 16_384_000)
ORDERS = (
    ("next", "pre_t42", "typed"),
    ("typed", "next", "pre_t42"),
    ("pre_t42", "typed", "next"),
    ("next", "typed", "pre_t42"),
    ("pre_t42", "next", "typed"),
    ("typed", "pre_t42", "next"),
)
ROLES = {
    "next": {
        "revision": "next@cf8ee683",
        "source_root": NEXT_ROOT,
        "binary": NEXT_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": NEXT_ROOT / "build/lib/libns3.44-unified-bus.dylib",
    },
    "pre_t42": {
        "revision": "pre-T42@d376fff",
        "source_root": PRE_T42_ROOT,
        "binary": PRE_T42_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": PRE_T42_ROOT / "build/lib/libns3.44-unified-bus.dylib",
    },
    "typed": {
        "revision": "T42 working tree",
        "source_root": REPO_ROOT,
        "binary": REPO_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": REPO_ROOT / "build/lib/libns3.44-unified-bus.dylib",
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


def run_once(role: str, destination: Path, sizes: tuple[int, int]) -> dict:
    case_dir = diagnosis.materialize(destination, role, sizes)
    timing_path = destination / "time-l.txt"
    command = [
        "/usr/bin/time",
        "-l",
        "-o",
        str(timing_path),
        "/usr/bin/env",
        f"DYLD_LIBRARY_PATH={ROLES[role]['library'].parent.resolve()}",
        str(ROLES[role]["binary"]),
        f"--case-path={case_dir}",
        "--rng-run=1",
    ]
    started_at = datetime.now(timezone.utc).isoformat()
    started = time.monotonic()
    process = subprocess.Popen(
        command,
        cwd=ROLES[role]["source_root"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=diagnosis.RUN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
    wall = time.monotonic() - started
    destination.joinpath("stdout.txt").write_bytes(stdout)
    destination.joinpath("stderr.txt").write_bytes(stderr)
    destination.joinpath("command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8"
    )
    destination.joinpath("exit-code.txt").write_text(
        f"{process.returncode}\n", encoding="ascii"
    )
    if timed_out or process.returncode != 0:
        raise RuntimeError(
            f"T42 run failed: role={role}, rc={process.returncode}, timed_out={timed_out}"
        )
    result = diagnosis.t28.parse_time_l(timing_path)
    trace_lines, trace_bytes = diagnosis.trace_volume(case_dir)
    result.update(
        {
            "role": role,
            "wall_seconds": wall,
            "started_at": started_at,
            "ended_at": datetime.now(timezone.utc).isoformat(),
            "trace_lines": trace_lines,
            "trace_bytes": trace_bytes,
            "artifact_hashes": diagnosis.generated_hashes(case_dir),
        }
    )
    destination.joinpath("result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result


def delta_percent(treatment: float, control: float) -> float:
    return 100.0 * (treatment - control) / control if control else 0.0


def block_delta(block: dict, treatment: str, control: str) -> dict:
    treatment_result = block[treatment]
    control_result = block[control]
    return {
        "wall_delta_percent": delta_percent(
            treatment_result["wall_seconds"], control_result["wall_seconds"]
        ),
        "user_delta_percent": delta_percent(
            treatment_result["user_seconds"], control_result["user_seconds"]
        ),
        "rss_delta_percent": delta_percent(
            treatment_result["peak_rss_bytes"], control_result["peak_rss_bytes"]
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--final-byte-check", action="store_true")
    args = parser.parse_args()
    if args.final_byte_check:
        if FINAL_BYTE_ROOT.exists():
            raise RuntimeError(f"refusing to overwrite T42 byte evidence: {FINAL_BYTE_ROOT}")
        FINAL_BYTE_ROOT.mkdir(parents=True)
        results = {
            role: run_once(role, FINAL_BYTE_ROOT / role, SIZES)
            for role in ("next", "typed")
        }
        exact = results["typed"]["artifact_hashes"] == results["next"]["artifact_hashes"]
        summary = {
            "schema_version": 1,
            "roles": {
                role: {
                    "revision": ROLES[role]["revision"],
                    "binary_sha256": diagnosis.t28.sha256(ROLES[role]["binary"]),
                    "unified_bus_library_sha256": diagnosis.t28.sha256(ROLES[role]["library"]),
                    "artifact_hashes": results[role]["artifact_hashes"],
                }
                for role in results
            },
            "semantic_exact": exact,
            "scope": "final binary byte check only; not a performance sample",
        }
        FINAL_BYTE_ROOT.joinpath("summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
        return 0 if exact else 1

    if EVIDENCE_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite T42 evidence: {EVIDENCE_ROOT}")
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

    for role in ROLES:
        warmup = run_once(role, EVIDENCE_ROOT / f"warmup-{role}", SIZES)
        print(f"warmup {role}: {warmup['wall_seconds']:.6f}s", flush=True)

    results = {role: [] for role in ROLES}
    comparisons = {
        "pre_t42_vs_next": [],
        "typed_vs_next": [],
        "typed_vs_pre_t42": [],
    }
    semantic = []
    for index, order in enumerate(ORDERS, 1):
        block = {}
        for position, role in enumerate(order, 1):
            item = run_once(
                role, EVIDENCE_ROOT / f"block-{index}-{position}-{role}", SIZES
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
                "pre_t42_exact": block["pre_t42"]["artifact_hashes"] == next_hashes,
                "typed_exact": block["typed"]["artifact_hashes"] == next_hashes,
            }
        )
        comparisons["pre_t42_vs_next"].append(block_delta(block, "pre_t42", "next"))
        comparisons["typed_vs_next"].append(block_delta(block, "typed", "next"))
        comparisons["typed_vs_pre_t42"].append(block_delta(block, "typed", "pre_t42"))

    summary = {
        "schema_version": 1,
        "roles": {role: diagnosis.median_fields(items) for role, items in results.items()},
        "comparisons": {
            name: {
                "median": {
                    key: statistics.median(item[key] for item in items)
                    for key in items[0]
                },
                "blocks": items,
            }
            for name, items in comparisons.items()
        },
        "semantic": semantic,
        "semantic_exact": all(
            item["pre_t42_exact"] and item["typed_exact"] for item in semantic
        ),
    }
    pre_gap = summary["comparisons"]["pre_t42_vs_next"]["median"]["wall_delta_percent"]
    typed_gap = summary["comparisons"]["typed_vs_next"]["median"]["wall_delta_percent"]
    typed_vs_pre = summary["comparisons"]["typed_vs_pre_t42"]["median"]
    typed_vs_next = summary["comparisons"]["typed_vs_next"]["median"]
    gates = {
        "semantic_exact": summary["semantic_exact"],
        "wall_recovery_at_least_5pp": pre_gap - typed_gap >= 5.0,
        "typed_wall_improves_pre_t42": typed_vs_pre["wall_delta_percent"] < 0.0,
        "typed_user_cpu_improves_pre_t42": typed_vs_pre["user_delta_percent"] < 0.0,
        "typed_wall_within_10pct_of_next": typed_vs_next["wall_delta_percent"] <= 10.0,
        "typed_rss_within_10pct_of_next": typed_vs_next["rss_delta_percent"] <= 10.0,
    }
    summary["acceptance"] = {
        "gates": gates,
        "wall_gap_recovery_percentage_points": pre_gap - typed_gap,
        "pass": all(gates.values()),
    }
    EVIDENCE_ROOT.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0 if summary["acceptance"]["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
