#!/usr/bin/env python3
"""Run the T23 p02 performance gate with frozen binaries and serial ordering."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_ROOT = Path(__file__).resolve().parent / "evidence/p02-performance-v5"
INPUT_SOURCE = (
    REPO_ROOT
    / "scratch/20260716-gitcode-main-lifecycle-differential/cases/p02-lifecycle/case-input"
)
ROLES = {
    "main": {
        "source_root": REPO_ROOT / ".worktrees/t00-reference-main",
        "revision": "ccf41b58b0ba3b9711efea595732a2908e3e07cf",
    },
    "current": {
        "source_root": REPO_ROOT,
        "revision": "17cf695785a98ac5697d957a639c7ec71ef7efb3",
    },
    "next": {
        "source_root": Path("/Users/ytxing/workspace/ns-3-ub-routing-modes"),
        "revision": "cf8ee683e00e5b6210661013e93cec7217d662e5",
    },
}
DEFAULT_ORDERS = (
    ("main", "current", "next"),
    ("current", "next", "main"),
    ("next", "main", "current"),
    ("main", "next", "current"),
    ("next", "current", "main"),
)
SUMMARY_ORDERS = (
    ("off", "summary"),
    ("summary", "off"),
    ("off", "summary"),
    ("summary", "off"),
    ("off", "summary"),
)
RUN_TIMEOUT_SECONDS = 60


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def binary_for(role: str) -> Path:
    return ROLES[role]["source_root"] / "build/scratch/ns3.44-ub-quick-example"


def copy_clean_input(destination: Path, lifecycle_level: str | None = None) -> Path:
    case_dir = destination / "case-input"
    case_dir.mkdir(parents=True)
    for source in INPUT_SOURCE.iterdir():
        if source.is_file() and source.name != "input-manifest.json":
            shutil.copy2(source, case_dir / source.name)
    attributes = case_dir / "network_attribute.txt"
    text = attributes.read_text(encoding="utf-8")
    old_routing = 'default ns3::UbRoutingProcess::RoutingAlgorithm "ADAPTIVE"'
    if text.count(old_routing) != 1:
        raise RuntimeError("p02 input no longer has exactly one ADAPTIVE routing declaration")
    attributes.write_text(
        text.replace(old_routing, 'default ns3::UbRoutingProcess::RoutingAlgorithm "HASH"'),
        encoding="utf-8",
    )
    if lifecycle_level is not None:
        with attributes.open("a", encoding="utf-8") as stream:
            stream.write(f'global UB_LIFECYCLE_TRACE_LEVEL "{lifecycle_level}"\n')
    return case_dir


def parse_time_l(path: Path) -> dict[str, int | float]:
    text = path.read_text(encoding="utf-8")
    cpu = re.search(r"([0-9.]+) real\s+([0-9.]+) user\s+([0-9.]+) sys", text)
    if cpu is None:
        raise RuntimeError(f"missing real/user/sys timing in {path}")

    def integer_field(label: str) -> int:
        match = re.search(rf"^\s*(\d+)\s+{re.escape(label)}$", text, re.MULTILINE)
        if match is None:
            raise RuntimeError(f"missing {label} in {path}")
        return int(match.group(1))

    return {
        "time_l_wall_seconds": float(cpu.group(1)),
        "user_seconds": float(cpu.group(2)),
        "system_seconds": float(cpu.group(3)),
        "peak_rss_bytes": integer_field("maximum resident set size"),
        "page_faults": integer_field("page faults"),
        "block_input_operations": integer_field("block input operations"),
        "block_output_operations": integer_field("block output operations"),
    }


def lifecycle_volume(case_dir: Path) -> tuple[int, int]:
    paths = sorted((case_dir / "runlog").glob("LifecycleTrace*"))
    byte_count = sum(path.stat().st_size for path in paths)
    record_count = 0
    for path in paths:
        if not path.name.startswith("LifecycleTrace_node_"):
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            if line and line[0].isdigit():
                record_count += 1
    return record_count, byte_count


def artifact_hashes(case_dir: Path) -> dict[str, str]:
    hashes = {}
    for parent in (case_dir / "output", case_dir / "runlog"):
        if not parent.exists():
            continue
        for path in sorted(parent.iterdir()):
            if path.is_file():
                hashes[str(path.relative_to(case_dir))] = sha256(path)
    return hashes


def run_repetition(
    role: str,
    destination: Path,
    lifecycle_level: str | None = None,
) -> dict:
    destination.mkdir(parents=True)
    case_dir = copy_clean_input(destination, lifecycle_level)
    timing_path = destination / "time-l.txt"
    binary = binary_for(role)
    command = [
        "/usr/bin/time",
        "-l",
        "-o",
        str(timing_path),
        str(binary),
        f"--case-path={case_dir}",
        "--rng-run=1",
    ]
    started_at = datetime.now(timezone.utc).isoformat()
    start = time.monotonic()
    process = subprocess.Popen(
        command,
        cwd=ROLES[role]["source_root"],
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
    elapsed = time.monotonic() - start
    (destination / "stdout.txt").write_bytes(stdout)
    (destination / "stderr.txt").write_bytes(stderr)
    (destination / "exit-code.txt").write_text(f"{process.returncode}\n", encoding="ascii")
    (destination / "command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8"
    )
    if timed_out or process.returncode != 0:
        raise RuntimeError(
            f"p02 repetition failed: role={role}, rc={process.returncode}, "
            f"timed_out={timed_out}, artifact={destination}"
        )
    result = parse_time_l(timing_path)
    result["wall_seconds"] = elapsed
    records, lifecycle_bytes = lifecycle_volume(case_dir)
    result.update(
        {
            "role": role,
            "lifecycle_level": lifecycle_level or "default",
            "started_at": started_at,
            "ended_at": datetime.now(timezone.utc).isoformat(),
            "lifecycle_record_count": records,
            "lifecycle_bytes": lifecycle_bytes,
            "artifact_hashes": artifact_hashes(case_dir),
        }
    )
    (destination / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result


def median_summary(results: list[dict]) -> dict:
    fields = (
        "wall_seconds",
        "time_l_wall_seconds",
        "user_seconds",
        "system_seconds",
        "peak_rss_bytes",
        "page_faults",
        "block_input_operations",
        "block_output_operations",
        "lifecycle_record_count",
        "lifecycle_bytes",
    )
    return {f"median_{field}": statistics.median(item[field] for item in results) for field in fields}


def delta_percent(treatment: float, control: float) -> float:
    return 100 * (treatment / control - 1)


def main() -> int:
    if EVIDENCE_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite T23 evidence: {EVIDENCE_ROOT}")
    EVIDENCE_ROOT.mkdir(parents=True)

    manifests = {}
    for role, config in ROLES.items():
        binary = binary_for(role)
        if not binary.is_file():
            raise RuntimeError(f"missing frozen {role} binary: {binary}")
        actual_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=config["source_root"], text=True
        ).strip()
        manifests[role] = {
            "expected_revision": config["revision"],
            "actual_revision": actual_revision,
            "binary": str(binary),
            "binary_sha256": sha256(binary),
            "unified_bus_library_sha256": sha256(
                config["source_root"] / "build/lib/libns3.44-unified-bus.dylib"
            ),
        }
    input_hashes = {
        path.name: sha256(path) for path in sorted(INPUT_SOURCE.iterdir()) if path.is_file()
    }
    manifest = {
        "schema_version": 1,
        "roles": manifests,
        "input_source": str(INPUT_SOURCE),
        "input_hashes": input_hashes,
        "compatibility_override": {
            "old_line": 'default ns3::UbRoutingProcess::RoutingAlgorithm "ADAPTIVE"',
            "new_line": 'default ns3::UbRoutingProcess::RoutingAlgorithm "HASH"',
            "reason": "Keep the active UbApp and transport-channel paths per-flow while giving all three revisions one valid routing policy for the resource-scale performance case.",
        },
        "default_orders": DEFAULT_ORDERS,
        "summary_orders": SUMMARY_ORDERS,
        "execution": "strictly serial",
    }
    (EVIDENCE_ROOT / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    default_results = {role: [] for role in ROLES}
    for role in ROLES:
        result = run_repetition(role, EVIDENCE_ROOT / f"default/warmup-{role}")
        print(f"default warmup {role}: {result['wall_seconds']:.3f}s", flush=True)
    default_pair_deltas = []
    for index, order in enumerate(DEFAULT_ORDERS, 1):
        pair = {}
        for position, role in enumerate(order, 1):
            result = run_repetition(
                role,
                EVIDENCE_ROOT / f"default/pair-{index}-{position}-{role}",
            )
            default_results[role].append(result)
            pair[role] = result
            print(
                f"default pair {index}.{position} {role}: {result['wall_seconds']:.3f}s, "
                f"rss={result['peak_rss_bytes']}",
                flush=True,
            )
        default_pair_deltas.append(
            {
                "pair": index,
                "current_vs_main_wall_percent": delta_percent(
                    pair["current"]["wall_seconds"], pair["main"]["wall_seconds"]
                ),
                "current_vs_next_wall_percent": delta_percent(
                    pair["current"]["wall_seconds"], pair["next"]["wall_seconds"]
                ),
            }
        )

    summary_results = {"off": [], "summary": []}
    for mode in summary_results:
        level = None if mode == "off" else "SUMMARY"
        result = run_repetition(
            "current", EVIDENCE_ROOT / f"summary/warmup-{mode}", level
        )
        print(f"summary warmup {mode}: {result['wall_seconds']:.3f}s", flush=True)
    summary_pair_deltas = []
    for index, order in enumerate(SUMMARY_ORDERS, 1):
        pair = {}
        for position, mode in enumerate(order, 1):
            level = None if mode == "off" else "SUMMARY"
            result = run_repetition(
                "current",
                EVIDENCE_ROOT / f"summary/pair-{index}-{position}-{mode}",
                level,
            )
            summary_results[mode].append(result)
            pair[mode] = result
            print(
                f"summary pair {index}.{position} {mode}: {result['wall_seconds']:.3f}s, "
                f"rss={result['peak_rss_bytes']}",
                flush=True,
            )
        summary_pair_deltas.append(
            delta_percent(pair["summary"]["wall_seconds"], pair["off"]["wall_seconds"])
        )

    default_medians = {role: median_summary(results) for role, results in default_results.items()}
    summary_medians = {mode: median_summary(results) for mode, results in summary_results.items()}
    result = {
        "schema_version": 1,
        "default": {
            "medians": default_medians,
            "paired_deltas": default_pair_deltas,
            "median_current_vs_main_wall_percent": statistics.median(
                item["current_vs_main_wall_percent"] for item in default_pair_deltas
            ),
            "median_current_vs_next_wall_percent": statistics.median(
                item["current_vs_next_wall_percent"] for item in default_pair_deltas
            ),
            "current_vs_main_median_wall_percent": delta_percent(
                default_medians["current"]["median_wall_seconds"],
                default_medians["main"]["median_wall_seconds"],
            ),
            "current_vs_next_median_wall_percent": delta_percent(
                default_medians["current"]["median_wall_seconds"],
                default_medians["next"]["median_wall_seconds"],
            ),
        },
        "summary": {
            "medians": summary_medians,
            "paired_wall_deltas_percent": summary_pair_deltas,
            "median_paired_wall_delta_percent": statistics.median(summary_pair_deltas),
            "summary_vs_off_median_wall_percent": delta_percent(
                summary_medians["summary"]["median_wall_seconds"],
                summary_medians["off"]["median_wall_seconds"],
            ),
        },
    }
    (EVIDENCE_ROOT / "summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
