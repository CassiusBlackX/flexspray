#!/usr/bin/env python3
"""Measure p01 lifecycle overhead across minimal-observability workload scales."""

from __future__ import annotations

import csv
import importlib.util
import json
import os
import shutil
import signal
import statistics
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
SOURCE = (
    REPO_ROOT
    / "scratch/20260716-gitcode-main-lifecycle-differential/cases/p01-lifecycle/case-input"
)
EVIDENCE_ROOT = HERE / "evidence/p01-detailed-diagnosis-v1"
SCALES = {
    "1m": (1_024_009, 1_024_000),
    "4m": (4_096_009, 4_096_000),
    "16m": (16_384_009, 16_384_000),
}
ORDERS = (
    ("main", "current"),
    ("current", "main"),
    ("main", "current"),
    ("current", "main"),
    ("main", "current"),
)
RUN_TIMEOUT_SECONDS = 120


spec = importlib.util.spec_from_file_location("t28_runner", HERE / "run_t28_acceptance.py")
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the T28 acceptance runner")
t28 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t28)


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError(f"expected one input line {old!r}, found {text.count(old)}")
    return text.replace(old, new)


def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
    if destination.exists():
        raise RuntimeError(f"refusing to overwrite diagnostic run: {destination}")
    case_dir = destination / "case-input"
    case_dir.mkdir(parents=True)
    for source in SOURCE.iterdir():
        if source.is_file() and source.name in t28.INPUT_NAMES:
            shutil.copy2(source, case_dir / source.name)

    attributes = case_dir / "network_attribute.txt"
    text = attributes.read_text(encoding="utf-8")
    replacements = (
        (
            'default ns3::UbLdstApi::UsePacketSpray "true"',
            'default ns3::UbLdstApi::UsePacketSpray "false"',
        ),
        ('global UB_PACKET_TRACE_ENABLE "true"', 'global UB_PACKET_TRACE_ENABLE "false"'),
        ('global UB_PORT_TRACE_ENABLE "true"', 'global UB_PORT_TRACE_ENABLE "false"'),
        ('global UB_RECORD_PKT_TRACE "true"', 'global UB_RECORD_PKT_TRACE "false"'),
    )
    for old, new in replacements:
        text = replace_once(text, old, new)
    if role == "current":
        if "UB_LIFECYCLE_TRACE_LEVEL" in text:
            raise RuntimeError("p01 source unexpectedly defines lifecycle output")
        text += 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n'
    attributes.write_text(text, encoding="utf-8")

    traffic = case_dir / "traffic.csv"
    with traffic.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
        fieldnames = list(rows[0]) if rows else []
    if len(rows) != 2 or "dataSize(Byte)" not in fieldnames:
        raise RuntimeError("p01 traffic shape changed")
    for row, size in zip(rows, sizes, strict=True):
        row["dataSize(Byte)"] = str(size)
    with traffic.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return case_dir


def generated_hashes(case_dir: Path) -> dict[str, str]:
    result = {}
    for parent_name in ("output", "runlog"):
        parent = case_dir / parent_name
        if not parent.exists():
            continue
        for path in sorted(parent.rglob("*")):
            if path.is_file():
                result[path.relative_to(case_dir).as_posix()] = t28.sha256(path)
    return result


def trace_volume(case_dir: Path) -> tuple[int, int]:
    runlog = case_dir / "runlog"
    paths = sorted(path for path in runlog.rglob("*") if path.is_file()) if runlog.exists() else []
    lines = 0
    for path in paths:
        with path.open(encoding="utf-8", errors="replace") as stream:
            lines += sum(1 for line in stream if line.strip())
    return lines, sum(path.stat().st_size for path in paths)


def run_once(role: str, destination: Path, sizes: tuple[int, int]) -> dict:
    case_dir = materialize(destination, role, sizes)
    timing_path = destination / "time-l.txt"
    command = [
        "/usr/bin/time",
        "-l",
        "-o",
        str(timing_path),
        str(t28.binary_for(role)),
        f"--case-path={case_dir}",
        "--rng-run=1",
    ]
    started_at = datetime.now(timezone.utc).isoformat()
    started = time.monotonic()
    process = subprocess.Popen(
        command,
        cwd=t28.ROLES[role]["source_root"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=RUN_TIMEOUT_SECONDS)
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
            f"diagnostic failed: role={role}, rc={process.returncode}, timed_out={timed_out}"
        )
    result = t28.parse_time_l(timing_path)
    trace_lines, trace_bytes = trace_volume(case_dir)
    result.update(
        {
            "role": role,
            "wall_seconds": wall,
            "started_at": started_at,
            "ended_at": datetime.now(timezone.utc).isoformat(),
            "trace_lines": trace_lines,
            "trace_bytes": trace_bytes,
            "artifact_hashes": generated_hashes(case_dir),
        }
    )
    destination.joinpath("result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result


def median_fields(items: list[dict]) -> dict[str, float]:
    fields = (
        "wall_seconds",
        "time_l_wall_seconds",
        "user_seconds",
        "system_seconds",
        "peak_rss_bytes",
        "page_faults",
        "block_input_operations",
        "block_output_operations",
        "trace_lines",
        "trace_bytes",
    )
    return {f"median_{field}": statistics.median(item[field] for item in items) for field in fields}


def run_scale(label: str, sizes: tuple[int, int]) -> dict:
    root = EVIDENCE_ROOT / label
    warmups = {}
    for role in ("main", "current"):
        warmups[role] = run_once(role, root / f"warmup-{role}", sizes)
        print(f"{label} warmup {role}: {warmups[role]['wall_seconds']:.6f}s", flush=True)

    results = {"main": [], "current": []}
    pairs = []
    semantic = []
    for index, order in enumerate(ORDERS, 1):
        pair = {}
        for position, role in enumerate(order, 1):
            item = run_once(role, root / f"pair-{index}-{position}-{role}", sizes)
            pair[role] = item
            results[role].append(item)
            print(
                f"{label} pair {index}.{position} {role}: "
                f"{item['wall_seconds']:.6f}s rss={item['peak_rss_bytes']}",
                flush=True,
            )
        main_hashes = pair["main"]["artifact_hashes"]
        current_hashes = pair["current"]["artifact_hashes"]
        semantic.append(
            {
                "pair": index,
                "main_only": sorted(set(main_hashes) - set(current_hashes)),
                "current_only": sorted(set(current_hashes) - set(main_hashes)),
                "changed": sorted(
                    name
                    for name in set(main_hashes) & set(current_hashes)
                    if main_hashes[name] != current_hashes[name]
                ),
            }
        )
        pairs.append(
            {
                "pair": index,
                "wall_delta_seconds": pair["current"]["wall_seconds"]
                - pair["main"]["wall_seconds"],
                "wall_delta_percent": t28.delta_percent(
                    pair["current"]["wall_seconds"], pair["main"]["wall_seconds"]
                ),
                "user_delta_seconds": pair["current"]["user_seconds"]
                - pair["main"]["user_seconds"],
                "rss_delta_bytes": pair["current"]["peak_rss_bytes"]
                - pair["main"]["peak_rss_bytes"],
                "rss_delta_percent": t28.delta_percent(
                    pair["current"]["peak_rss_bytes"], pair["main"]["peak_rss_bytes"]
                ),
            }
        )
    return {
        "sizes": sizes,
        "total_data_bytes": sum(sizes),
        "roles": {role: median_fields(items) for role, items in results.items()},
        "pairs": pairs,
        "median_paired_wall_delta_seconds": statistics.median(
            item["wall_delta_seconds"] for item in pairs
        ),
        "median_paired_wall_delta_percent": statistics.median(
            item["wall_delta_percent"] for item in pairs
        ),
        "median_paired_user_delta_seconds": statistics.median(
            item["user_delta_seconds"] for item in pairs
        ),
        "median_paired_rss_delta_bytes": statistics.median(
            item["rss_delta_bytes"] for item in pairs
        ),
        "median_paired_rss_delta_percent": statistics.median(
            item["rss_delta_percent"] for item in pairs
        ),
        "semantic": semantic,
        "semantic_exact": all(
            not item["main_only"] and not item["current_only"] and not item["changed"]
            for item in semantic
        ),
    }


def linear_slope(points: list[tuple[float, float]]) -> float:
    mean_x = statistics.mean(x for x, _ in points)
    mean_y = statistics.mean(y for _, y in points)
    numerator = sum((x - mean_x) * (y - mean_y) for x, y in points)
    denominator = sum((x - mean_x) ** 2 for x, _ in points)
    return numerator / denominator


def main() -> int:
    if EVIDENCE_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite diagnosis evidence: {EVIDENCE_ROOT}")
    EVIDENCE_ROOT.mkdir(parents=True)
    manifest = {
        "schema_version": 1,
        "roles": {
            role: {
                "revision": t28.ROLES[role]["revision"],
                "binary_sha256": t28.sha256(t28.binary_for(role)),
                "unified_bus_library_sha256": t28.sha256(t28.library_for(role)),
            }
            for role in ("main", "current")
        },
        "source": str(SOURCE),
        "source_input_hashes": t28.input_hashes(SOURCE),
        "scales": SCALES,
        "orders": ORDERS,
        "observability": "repo minimal preset: task trace ON; packet, port, per-hop and lifecycle OFF",
        "execution": "one warm-up per role and five counterbalanced pairs, strictly serial",
    }
    EVIDENCE_ROOT.joinpath("manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    scales = {label: run_scale(label, sizes) for label, sizes in SCALES.items()}
    for role in ("main", "current"):
        points = [
            (item["total_data_bytes"] / 1_000_000, item["roles"][role]["median_wall_seconds"])
            for item in scales.values()
        ]
        manifest[f"{role}_wall_slope_seconds_per_mb"] = linear_slope(points)
    delta_points = [
        (item["total_data_bytes"] / 1_000_000, item["median_paired_wall_delta_seconds"])
        for item in scales.values()
    ]
    result = {
        "schema_version": 1,
        "scales": scales,
        "current_minus_main_wall_slope_seconds_per_mb": linear_slope(delta_points),
        "main_wall_slope_seconds_per_mb": manifest["main_wall_slope_seconds_per_mb"],
        "current_wall_slope_seconds_per_mb": manifest["current_wall_slope_seconds_per_mb"],
        "all_semantics_exact": all(item["semantic_exact"] for item in scales.values()),
    }
    EVIDENCE_ROOT.joinpath("summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {
                "scales": {
                    label: {
                        "wall_percent": item["median_paired_wall_delta_percent"],
                        "wall_seconds": item["median_paired_wall_delta_seconds"],
                        "rss_bytes": item["median_paired_rss_delta_bytes"],
                        "semantic_exact": item["semantic_exact"],
                    }
                    for label, item in scales.items()
                },
                "delta_slope_seconds_per_mb": result[
                    "current_minus_main_wall_slope_seconds_per_mb"
                ],
            },
            indent=2,
        ),
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
