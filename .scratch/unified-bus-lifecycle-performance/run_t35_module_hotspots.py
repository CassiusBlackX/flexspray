#!/usr/bin/env python3
"""Profile lifecycle overhead by real Unified Bus module path.

The runner reuses frozen accepted fixtures, changes only traffic volume and
observability, and keeps all simulator processes strictly serial.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
NEXT_ROOT = Path("/Users/ytxing/workspace/ns-3-ub-routing-modes")
EVIDENCE_ROOT = HERE / "evidence/t35-module-hotspots-v3"
BYTES_PER_TASK_OVERRIDES: dict[str, int] = {}
RUN_TIMEOUT_SECONDS = 120
ROLES = {
    "next": {
        "revision": "cf8ee683e00e5b6210661013e93cec7217d662e5",
        "source_root": NEXT_ROOT,
        "binary": NEXT_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library_dir": NEXT_ROOT / "build/lib",
    },
    "current": {
        "revision": "51c60c5e10ed3d5fd0c69771fae1755e717ddf72",
        "source_root": REPO_ROOT,
        "binary": REPO_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library_dir": REPO_ROOT / "build/lib",
    },
}


@dataclass(frozen=True)
class CaseSpec:
    source: Path
    bytes_per_task: int
    description: str
    routing_hash_compat: bool = False


CASES = {
    "rtp-normal": CaseSpec(
        REPO_ROOT
        / "scratch/20260716-gitcode-main-lifecycle-differential/cases/p02-lifecycle/case-input",
        32 * 1024 * 1024,
        "32-host eight-flow RTP Clos workload without loss",
        routing_hash_compat=True,
    ),
    "ctp-normal": CaseSpec(
        REPO_ROOT
        / "scratch/20260717-next-lifecycle-ctp-rtp-complex-differential/cases/ctp-win4/lifecycle/case-input",
        128 * 1024 * 1024,
        "four same-Jetty CTP WRITEs with inflight window four",
    ),
    "rtp-gbn-rto": CaseSpec(
        REPO_ROOT
        / "scratch/20260717-next-lifecycle-ctp-rtp-complex-differential/cases/rtp-gbn-double-drop/lifecycle/case-input",
        512 * 1024 * 1024,
        "RTP GBN workload with deterministic PSN 1 and 3 drops",
    ),
    "dcqcn": CaseSpec(
        REPO_ROOT
        / "scratch/20260716-gitcode-main-lifecycle-differential/cases/m15-lifecycle/case-input",
        256 * 1024 * 1024,
        "two converging RTP flows with PFC_DYNAMIC and DCQCN",
    ),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def set_global(text: str, key: str, value: str, *, add: bool = True) -> str:
    pattern = re.compile(rf'^global {re.escape(key)} "[^"]*"$', re.MULTILINE)
    replacement = f'global {key} "{value}"'
    if pattern.search(text):
        return pattern.sub(replacement, text)
    if not add:
        return text
    return text.rstrip() + "\n" + replacement + "\n"


def materialize(case_name: str, role: str, destination: Path) -> Path:
    spec = CASES[case_name]
    case_dir = destination / "case-input"
    if case_dir.exists():
        raise RuntimeError(f"refusing to overwrite run directory: {destination}")
    case_dir.mkdir(parents=True)
    for source in spec.source.iterdir():
        if source.is_file() and source.name != "input-manifest.json":
            shutil.copy2(source, case_dir / source.name)

    attributes = case_dir / "network_attribute.txt"
    text = attributes.read_text(encoding="utf-8")
    if spec.routing_hash_compat:
        old = 'default ns3::UbRoutingProcess::RoutingAlgorithm "ADAPTIVE"'
        if text.count(old) != 1:
            raise RuntimeError(f"{case_name} routing compatibility seam changed")
        text = text.replace(
            old, 'default ns3::UbRoutingProcess::RoutingAlgorithm "HASH"'
        )
    for key in (
        "UB_TRACE_ENABLE",
        "UB_TASK_TRACE_ENABLE",
        "UB_PACKET_TRACE_ENABLE",
        "UB_PORT_TRACE_ENABLE",
        "UB_RECORD_PKT_TRACE",
        "UB_QUEUE_TRACE_ENABLE",
        "UB_FLOW_CONTROL_TRACE_ENABLE",
        "UB_CONGESTION_CONTROL_TRACE_ENABLE",
        "UB_PARSE_TRACE_ENABLE",
    ):
        text = set_global(text, key, "false")
    if role == "current":
        text = set_global(text, "UB_LIFECYCLE_TRACE_LEVEL", "OFF")
    attributes.write_text(text, encoding="utf-8")

    traffic = case_dir / "traffic.csv"
    with traffic.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    if not rows or "dataSize(Byte)" not in rows[0]:
        raise RuntimeError(f"{case_name} has no scalable traffic rows")
    fieldnames = list(rows[0])
    bytes_per_task = BYTES_PER_TASK_OVERRIDES.get(case_name, spec.bytes_per_task)
    for row in rows:
        row["dataSize(Byte)"] = str(bytes_per_task)
    with traffic.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return case_dir


def parse_time_l(path: Path) -> dict[str, int | float]:
    text = path.read_text(encoding="utf-8")
    cpu = re.search(r"([0-9.]+) real\s+([0-9.]+) user\s+([0-9.]+) sys", text)
    rss = re.search(r"^\s*(\d+)\s+maximum resident set size$", text, re.MULTILINE)
    if cpu is None or rss is None:
        raise RuntimeError(f"cannot parse timing output: {path}")
    return {
        "time_wall_seconds": float(cpu.group(1)),
        "user_seconds": float(cpu.group(2)),
        "system_seconds": float(cpu.group(3)),
        "peak_rss_bytes": int(rss.group(1)),
    }


def run_process(command: list[str], cwd: Path, timeout: int, destination: Path) -> float:
    started = time.monotonic()
    process = subprocess.Popen(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
    elapsed = time.monotonic() - started
    destination.joinpath("stdout.txt").write_bytes(stdout)
    destination.joinpath("stderr.txt").write_bytes(stderr)
    destination.joinpath("exit-code.txt").write_text(
        f"{process.returncode}\n", encoding="ascii"
    )
    destination.joinpath("command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8"
    )
    if timed_out or process.returncode != 0:
        raise RuntimeError(
            f"run failed: rc={process.returncode}, timeout={timed_out}, "
            f"evidence={destination}"
        )
    return elapsed


def run_timed(case_name: str, role: str, destination: Path) -> dict:
    destination.mkdir(parents=True)
    case_dir = materialize(case_name, role, destination)
    timing = destination / "time-l.txt"
    config = ROLES[role]
    command = [
        "/usr/bin/time",
        "-l",
        "-o",
        str(timing),
        "/usr/bin/env",
        f"DYLD_LIBRARY_PATH={config['library_dir'].resolve()}",
        str(config["binary"]),
        f"--case-path={case_dir}",
        "--rng-run=1",
    ]
    started_at = datetime.now(timezone.utc).isoformat()
    elapsed = run_process(command, config["source_root"], RUN_TIMEOUT_SECONDS, destination)
    result = parse_time_l(timing)
    result.update(
        {
            "case": case_name,
            "role": role,
            "wall_seconds": elapsed,
            "started_at": started_at,
            "ended_at": datetime.now(timezone.utc).isoformat(),
        }
    )
    destination.joinpath("result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result


def run_timing(case_name: str, rounds: int) -> dict:
    root = EVIDENCE_ROOT / "timing" / case_name
    for role in ("next", "current"):
        item = run_timed(case_name, role, root / f"warmup-{role}")
        print(f"{case_name} warmup {role}: {item['wall_seconds']:.3f}s", flush=True)

    orders = [("next", "current"), ("current", "next")]
    results = {role: [] for role in ROLES}
    pairs = []
    for index in range(1, rounds + 1):
        pair = {}
        for position, role in enumerate(orders[(index - 1) % len(orders)], 1):
            item = run_timed(case_name, role, root / f"pair-{index}-{position}-{role}")
            pair[role] = item
            results[role].append(item)
            print(
                f"{case_name} pair {index}.{position} {role}: "
                f"{item['wall_seconds']:.3f}s",
                flush=True,
            )
        pairs.append(
            {
                "pair": index,
                "wall_delta_seconds": pair["current"]["wall_seconds"]
                - pair["next"]["wall_seconds"],
                "wall_delta_percent": 100
                * (pair["current"]["wall_seconds"] / pair["next"]["wall_seconds"] - 1),
                "user_delta_seconds": pair["current"]["user_seconds"]
                - pair["next"]["user_seconds"],
            }
        )
    summary = {
        "case": case_name,
        "roles": {
            role: {
                "median_wall_seconds": statistics.median(i["wall_seconds"] for i in items),
                "median_user_seconds": statistics.median(i["user_seconds"] for i in items),
                "median_peak_rss_bytes": statistics.median(i["peak_rss_bytes"] for i in items),
            }
            for role, items in results.items()
        },
        "median_paired_wall_delta_seconds": statistics.median(
            item["wall_delta_seconds"] for item in pairs
        ),
        "median_paired_wall_delta_percent": statistics.median(
            item["wall_delta_percent"] for item in pairs
        ),
        "median_paired_user_delta_seconds": statistics.median(
            item["user_delta_seconds"] for item in pairs
        ),
        "pairs": pairs,
    }
    root.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return summary


def run_profile(case_name: str, role: str) -> Path:
    destination = EVIDENCE_ROOT / "profiles" / case_name / role
    destination.mkdir(parents=True)
    case_dir = materialize(case_name, role, destination)
    config = ROLES[role]
    trace = destination / f"{role}.trace"
    command = [
        "xcrun",
        "xctrace",
        "record",
        "--quiet",
        "--no-prompt",
        "--template",
        "Time Profiler",
        "--output",
        str(trace),
        "--env",
        f"DYLD_LIBRARY_PATH={config['library_dir'].resolve()}",
        "--launch",
        "--",
        str(config["binary"]),
        f"--case-path={case_dir}",
        "--rng-run=1",
    ]
    run_process(command, config["source_root"], RUN_TIMEOUT_SECONDS, destination)
    toc = destination / f"{role}-toc.xml"
    xml = destination / f"{role}.xml"
    subprocess.run(
        ["xcrun", "xctrace", "export", "--input", str(trace), "--toc", "--output", str(toc)],
        check=True,
        cwd=config["source_root"],
    )
    subprocess.run(
        [
            "xcrun",
            "xctrace",
            "export",
            "--input",
            str(trace),
            "--xpath",
            '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]',
            "--output",
            str(xml),
        ],
        check=True,
        cwd=config["source_root"],
    )
    print(f"{case_name} profile {role}: {xml}", flush=True)
    return xml


def validate_environment() -> dict:
    roles = {}
    for role, config in ROLES.items():
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=config["source_root"], text=True
        ).strip()
        if revision != config["revision"]:
            raise RuntimeError(f"{role} revision changed: {revision}")
        for path in (config["binary"], config["library_dir"] / "libns3.44-unified-bus.dylib"):
            if not path.is_file():
                raise RuntimeError(f"missing frozen artifact: {path}")
        roles[role] = {
            "revision": revision,
            "binary": str(config["binary"]),
            "binary_sha256": sha256(config["binary"]),
            "library_dir": str(config["library_dir"]),
            "unified_bus_library_sha256": sha256(
                config["library_dir"] / "libns3.44-unified-bus.dylib"
            ),
        }
    return roles


def main() -> int:
    global EVIDENCE_ROOT
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", choices=("timing", "profile", "all"), default="all")
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--evidence-root", type=Path, default=EVIDENCE_ROOT)
    parser.add_argument("--bytes-per-task", type=int)
    args = parser.parse_args()
    selected = [item.strip() for item in args.cases.split(",") if item.strip()]
    unknown = sorted(set(selected) - set(CASES))
    if unknown:
        raise RuntimeError(f"unknown cases: {unknown}")
    if args.rounds < 1:
        raise RuntimeError("rounds must be positive")
    if args.bytes_per_task is not None:
        if args.bytes_per_task < 1:
            raise RuntimeError("bytes per task must be positive")
        if len(selected) != 1:
            raise RuntimeError("bytes-per-task requires exactly one selected case")
        BYTES_PER_TASK_OVERRIDES[selected[0]] = args.bytes_per_task
    EVIDENCE_ROOT = args.evidence_root.resolve()

    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    manifest_path = EVIDENCE_ROOT / "manifest.json"
    if not manifest_path.exists():
        manifest = {
            "schema_version": 1,
            "roles": validate_environment(),
            "cases": {
                name: {
                    "source": str(spec.source),
                    "bytes_per_task": BYTES_PER_TASK_OVERRIDES.get(
                        name, spec.bytes_per_task
                    ),
                    "description": spec.description,
                }
                for name, spec in CASES.items()
            },
            "observability": "all legacy trace output and lifecycle output OFF",
            "execution": "strictly serial",
        }
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    else:
        validate_environment()

    result = {}
    if args.phase in ("timing", "all"):
        for case_name in selected:
            result[case_name] = run_timing(case_name, args.rounds)
        EVIDENCE_ROOT.joinpath("timing-summary.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    if args.phase in ("profile", "all"):
        inputs = []
        for case_name in selected:
            for role in ("next", "current"):
                inputs.append(run_profile(case_name, role))
        output = EVIDENCE_ROOT / "profile-summary.json"
        subprocess.run(
            [
                "python3.12",
                str(HERE / "analyze_time_profile.py"),
                *map(str, inputs),
                "--output",
                str(output),
            ],
            check=True,
            cwd=REPO_ROOT,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
