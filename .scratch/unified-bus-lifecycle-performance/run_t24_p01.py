#!/usr/bin/env python3
"""Run one frozen serial p01 no-spray/OFF current/main performance gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
INPUT_SOURCE = (
    REPO_ROOT
    / "scratch/20260716-gitcode-main-lifecycle-differential/cases/p01-lifecycle/case-input"
)
ROLES = {
    "main": {
        "source_root": REPO_ROOT / ".worktrees/t00-reference-main",
        "revision": "ccf41b58b0ba3b9711efea595732a2908e3e07cf",
    },
    "current": {
        "source_root": REPO_ROOT,
        "revision": None,
    },
}
ORDERS = (
    ("main", "current"),
    ("current", "main"),
    ("main", "current"),
    ("current", "main"),
    ("main", "current"),
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


def copy_clean_input(role: str, destination: Path) -> Path:
    case_dir = destination / "case-input"
    case_dir.mkdir(parents=True)
    for source in INPUT_SOURCE.iterdir():
        if source.is_file() and source.name != "input-manifest.json":
            shutil.copy2(source, case_dir / source.name)
    attributes = case_dir / "network_attribute.txt"
    text = attributes.read_text(encoding="utf-8")
    spray = 'default ns3::UbLdstApi::UsePacketSpray "true"'
    if text.count(spray) != 1:
        raise RuntimeError("p01 input no longer has one enabled LDST packet-spray declaration")
    text = text.replace(spray, 'default ns3::UbLdstApi::UsePacketSpray "false"')
    if role == "current":
        if "UB_LIFECYCLE_TRACE_LEVEL" in text:
            raise RuntimeError("p01 input unexpectedly defines lifecycle level")
        text += 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n'
    attributes.write_text(text, encoding="utf-8")
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


def artifact_hashes(case_dir: Path) -> dict[str, str]:
    hashes = {}
    for parent_name in ("output", "runlog"):
        parent = case_dir / parent_name
        if not parent.exists():
            continue
        for path in sorted(parent.iterdir()):
            if path.is_file() and not path.name.startswith("LifecycleTrace"):
                hashes[str(path.relative_to(case_dir))] = sha256(path)
    return hashes


def run_repetition(role: str, destination: Path) -> dict:
    destination.mkdir(parents=True)
    case_dir = copy_clean_input(role, destination)
    timing_path = destination / "time-l.txt"
    command = [
        "/usr/bin/time",
        "-l",
        "-o",
        str(timing_path),
        str(binary_for(role)),
        f"--case-path={case_dir}",
        "--rng-run=1",
    ]
    start = time.monotonic()
    started_at = datetime.now(timezone.utc).isoformat()
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
            f"p01 repetition failed: role={role}, rc={process.returncode}, "
            f"timed_out={timed_out}, artifact={destination}"
        )
    result = parse_time_l(timing_path)
    result.update(
        {
            "role": role,
            "wall_seconds": elapsed,
            "started_at": started_at,
            "ended_at": datetime.now(timezone.utc).isoformat(),
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
    )
    return {f"median_{field}": statistics.median(item[field] for item in results) for field in fields}


def tasktrace_multiset_equal(left: Path, right: Path) -> bool:
    return Counter(left.read_text(encoding="utf-8").splitlines()) == Counter(
        right.read_text(encoding="utf-8").splitlines()
    )


def semantic_comparison(main: dict, current: dict, evidence_root: Path) -> dict:
    main_hashes = main["artifact_hashes"]
    current_hashes = current["artifact_hashes"]
    common = sorted(set(main_hashes) & set(current_hashes))
    mismatches = [name for name in common if main_hashes[name] != current_hashes[name]]
    unexplained = []
    for name in mismatches:
        if not name.startswith("runlog/TaskTrace_node_"):
            unexplained.append(name)
            continue
        left = evidence_root / "warmup-main/case-input" / name
        right = evidence_root / "warmup-current/case-input" / name
        if not tasktrace_multiset_equal(left, right):
            unexplained.append(name)
    return {
        "main_only": sorted(set(main_hashes) - set(current_hashes)),
        "current_only": sorted(set(current_hashes) - set(main_hashes)),
        "hash_mismatches": mismatches,
        "unexplained_mismatches": unexplained,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    evidence_root = Path(__file__).resolve().parent / f"evidence/p01-{args.label}"
    if evidence_root.exists():
        raise RuntimeError(f"refusing to overwrite T24 evidence: {evidence_root}")
    evidence_root.mkdir(parents=True)

    manifests = {}
    for role, config in ROLES.items():
        binary = binary_for(role)
        if not binary.is_file():
            raise RuntimeError(f"missing frozen {role} binary: {binary}")
        manifests[role] = {
            "revision": subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=config["source_root"], text=True
            ).strip(),
            "binary": str(binary),
            "binary_sha256": sha256(binary),
            "unified_bus_library_sha256": sha256(
                config["source_root"] / "build/lib/libns3.44-unified-bus.dylib"
            ),
        }
    manifest = {
        "schema_version": 1,
        "label": args.label,
        "roles": manifests,
        "input_source": str(INPUT_SOURCE),
        "input_hashes": {
            path.name: sha256(path) for path in sorted(INPUT_SOURCE.iterdir()) if path.is_file()
        },
        "transform": {
            "both": 'default ns3::UbLdstApi::UsePacketSpray "false"',
            "current_only": 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"',
        },
        "orders": ORDERS,
        "execution": "strictly serial",
    }
    (evidence_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    warmups = {}
    for role in ROLES:
        warmups[role] = run_repetition(role, evidence_root / f"warmup-{role}")
        print(f"warmup {role}: {warmups[role]['wall_seconds']:.6f}s", flush=True)

    results = {role: [] for role in ROLES}
    paired_deltas = []
    for pair_index, order in enumerate(ORDERS, 1):
        pair = {}
        for position, role in enumerate(order, 1):
            result = run_repetition(
                role, evidence_root / f"pair-{pair_index}-{position}-{role}"
            )
            pair[role] = result
            results[role].append(result)
            print(
                f"pair {pair_index} {position} {role}: "
                f"{result['wall_seconds']:.6f}s rss={result['peak_rss_bytes']}",
                flush=True,
            )
        paired_deltas.append(
            100 * (pair["current"]["wall_seconds"] / pair["main"]["wall_seconds"] - 1)
        )

    summaries = {role: median_summary(values) for role, values in results.items()}
    semantic = semantic_comparison(warmups["main"], warmups["current"], evidence_root)
    summary = {
        "schema_version": 1,
        "label": args.label,
        "roles": summaries,
        "paired_wall_deltas_percent": paired_deltas,
        "median_paired_wall_delta_percent": statistics.median(paired_deltas),
        "median_wall_delta_percent": 100
        * (
            summaries["current"]["median_wall_seconds"]
            / summaries["main"]["median_wall_seconds"]
            - 1
        ),
        "median_peak_rss_delta_percent": 100
        * (
            summaries["current"]["median_peak_rss_bytes"]
            / summaries["main"]["median_peak_rss_bytes"]
            - 1
        ),
        "semantic": semantic,
    }
    (evidence_root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if not semantic["unexplained_mismatches"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
