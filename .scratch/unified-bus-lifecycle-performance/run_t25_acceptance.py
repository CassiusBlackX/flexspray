#!/usr/bin/env python3
"""Run serial T25 SUMMARY-byte and p02 OFF pre/post acceptance."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_ROOT = Path(__file__).resolve().parent / "evidence/t25-acceptance"
PRE_ROOT = Path(__file__).resolve().parent / "evidence/t25-pre-layout/bin"
SUMMARY_INPUT = Path(__file__).resolve().parent / "evidence/summary-byte/post-t23-final"
P02_INPUT = (
    REPO_ROOT
    / "scratch/20260716-gitcode-main-lifecycle-differential/cases/p02-lifecycle/case-input"
)
ROLES = {
    "pre": {
        "binary": PRE_ROOT / "ub-quick-example",
        "library": PRE_ROOT / "libns3.44-unified-bus.dylib",
    },
    "post": {
        "binary": REPO_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": REPO_ROOT / "build/lib/libns3.44-unified-bus.dylib",
    },
}
ORDERS = (
    ("pre", "post"),
    ("post", "pre"),
    ("pre", "post"),
    ("post", "pre"),
    ("pre", "post"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_input(source: Path, destination: Path, p02: bool) -> Path:
    case_dir = destination / "case-input"
    case_dir.mkdir(parents=True)
    for item in source.iterdir():
        if item.is_file() and item.name != "input-manifest.json":
            shutil.copy2(item, case_dir / item.name)
    attributes = case_dir / "network_attribute.txt"
    text = attributes.read_text(encoding="utf-8")
    if p02:
        old_routing = 'default ns3::UbRoutingProcess::RoutingAlgorithm "ADAPTIVE"'
        if text.count(old_routing) != 1:
            raise RuntimeError("p02 input must contain one ADAPTIVE routing declaration")
        text = text.replace(
            old_routing,
            'default ns3::UbRoutingProcess::RoutingAlgorithm "HASH"',
        )
        text += 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n'
    attributes.write_text(text, encoding="utf-8")
    return case_dir


def parse_time(path: Path) -> dict[str, int | float]:
    text = path.read_text(encoding="utf-8")
    cpu = re.search(r"([0-9.]+) real\s+([0-9.]+) user\s+([0-9.]+) sys", text)
    if cpu is None:
        raise RuntimeError(f"missing timing in {path}")

    def integer(label: str) -> int:
        match = re.search(rf"^\s*(\d+)\s+{re.escape(label)}$", text, re.MULTILINE)
        if match is None:
            raise RuntimeError(f"missing {label} in {path}")
        return int(match.group(1))

    return {
        "time_l_wall_seconds": float(cpu.group(1)),
        "user_seconds": float(cpu.group(2)),
        "system_seconds": float(cpu.group(3)),
        "peak_rss_bytes": integer("maximum resident set size"),
        "page_faults": integer("page faults"),
        "block_input_operations": integer("block input operations"),
        "block_output_operations": integer("block output operations"),
    }


def artifact_hashes(case_dir: Path) -> dict[str, str]:
    result = {}
    for name in ("output", "runlog"):
        root = case_dir / name
        if not root.exists():
            continue
        for path in sorted(root.iterdir()):
            if path.is_file():
                result[str(path.relative_to(case_dir))] = sha256(path)
    return result


def run(role: str, destination: Path, source: Path, p02: bool) -> dict:
    destination.mkdir(parents=True)
    case_dir = copy_input(source, destination, p02)
    timing = destination / "time-l.txt"
    command = [
        "/usr/bin/time",
        "-l",
        "-o",
        str(timing),
        str(ROLES[role]["binary"]),
        f"--case-path={case_dir}",
        "--rng-run=1",
    ]
    env = os.environ.copy()
    if role == "pre":
        env["DYLD_LIBRARY_PATH"] = f"{PRE_ROOT}:{REPO_ROOT / 'build/lib'}"
    started = time.monotonic()
    completed = subprocess.run(
        command,
        cwd=REPO_ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=60,
        check=False,
    )
    elapsed = time.monotonic() - started
    (destination / "stdout.txt").write_bytes(completed.stdout)
    (destination / "stderr.txt").write_bytes(completed.stderr)
    (destination / "command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8"
    )
    if completed.returncode != 0:
        raise RuntimeError(f"{role} run failed with {completed.returncode}: {destination}")
    result = parse_time(timing)
    result.update(
        {
            "role": role,
            "wall_seconds": elapsed,
            "artifact_hashes": artifact_hashes(case_dir),
        }
    )
    (destination / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result


def delta(treatment: float, control: float) -> float:
    return 100 * (treatment / control - 1)


def main() -> int:
    if EVIDENCE_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite {EVIDENCE_ROOT}")
    EVIDENCE_ROOT.mkdir(parents=True)
    manifest = {
        "schema_version": 1,
        "execution": "strictly serial",
        "orders": ORDERS,
        "roles": {
            role: {
                "binary_sha256": sha256(config["binary"]),
                "unified_bus_library_sha256": sha256(config["library"]),
            }
            for role, config in ROLES.items()
        },
    }
    (EVIDENCE_ROOT / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    summary = {
        role: run(role, EVIDENCE_ROOT / f"summary-{role}", SUMMARY_INPUT, False)
        for role in ("pre", "post")
    }
    summary_equal = summary["pre"]["artifact_hashes"] == summary["post"]["artifact_hashes"]
    if not summary_equal:
        raise RuntimeError("pre/post explicit-SUMMARY artifact trees differ")

    for role in ("pre", "post"):
        run(role, EVIDENCE_ROOT / f"p02/warmup-{role}", P02_INPUT, True)
    measured = {"pre": [], "post": []}
    paired = []
    for index, order in enumerate(ORDERS, 1):
        pair = {}
        for position, role in enumerate(order, 1):
            result = run(
                role,
                EVIDENCE_ROOT / f"p02/pair-{index}-{position}-{role}",
                P02_INPUT,
                True,
            )
            measured[role].append(result)
            pair[role] = result
            print(
                f"pair {index}.{position} {role}: {result['wall_seconds']:.6f}s "
                f"rss={result['peak_rss_bytes']}",
                flush=True,
            )
        paired.append(delta(pair["post"]["wall_seconds"], pair["pre"]["wall_seconds"]))

    def med(role: str, field: str) -> float:
        return statistics.median(item[field] for item in measured[role])

    result = {
        "schema_version": 1,
        "summary_artifacts_byte_identical": summary_equal,
        "paired_wall_deltas_percent": paired,
        "median_paired_wall_delta_percent": statistics.median(paired),
        "roles": {
            role: {
                "median_wall_seconds": med(role, "wall_seconds"),
                "median_user_seconds": med(role, "user_seconds"),
                "median_system_seconds": med(role, "system_seconds"),
                "median_peak_rss_bytes": med(role, "peak_rss_bytes"),
                "median_page_faults": med(role, "page_faults"),
                "median_block_input_operations": med(role, "block_input_operations"),
                "median_block_output_operations": med(role, "block_output_operations"),
            }
            for role in ("pre", "post")
        },
    }
    result["median_rss_delta_percent"] = delta(
        result["roles"]["post"]["median_peak_rss_bytes"],
        result["roles"]["pre"]["median_peak_rss_bytes"],
    )
    (EVIDENCE_ROOT / "summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
