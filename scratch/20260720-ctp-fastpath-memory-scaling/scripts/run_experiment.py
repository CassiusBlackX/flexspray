#!/usr/bin/env python3
"""Run the approved three-role CTP fast-path scaling experiment serially."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import re
import signal
import shutil
import statistics
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
REPO = PACKAGE.parents[1]
NEXT_ROOT = Path(
    os.environ.get("T36_NEXT_ROOT", REPO / ".worktrees/t36-next-baseline")
).expanduser()
LAZY_ROOT = REPO / ".worktrees/t36-lazy-baseline"
BASE_SOURCE = (
    REPO
    / "scratch/20260717-next-lifecycle-ctp-rtp-complex-differential"
    / "cases/ctp-win4/lifecycle/case-input"
)
RESULTS = PACKAGE / "results"
INPUTS = PACKAGE / "inputs"
LEDGER = PACKAGE / "run-ledger.md"
EXPECTED_HEADS = {
    "next": "cf8ee683e00e5b6210661013e93cec7217d662e5",
    "lazy": "a95f8bcdbf2eb32e171edde7aae970cfedf85c76",
    "fastpath": "ed298a8c4b585cf6cace4caf60824bd7a0b1ffac",
}
FASTPATH_PRODUCTION_SOURCE_SHA256 = {
    "src/unified-bus/model/protocol/ub-ctp.cc":
        "425542b73718fbb9ebaa96b2ab46b67975c58ce0be2a262e898608d7e743543b",
    "src/unified-bus/model/ub-lifecycle.cc":
        "16f6b7e06339d0b76f5bd23f2ea4123c267daddfa319497e947a19c674d40f28",
    "src/unified-bus/model/ub-lifecycle.h":
        "8f118137c62fa7fd8399b7349f040a619a46deffba0e63a46e3f9a6ec13a44e7",
}
ROLES = {
    "next": {
        "root": NEXT_ROOT,
        "binary": NEXT_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": NEXT_ROOT / "build/lib/libns3.44-unified-bus.dylib",
    },
    "lazy": {
        "root": LAZY_ROOT,
        "binary": LAZY_ROOT / "build/scratch/ns3.44-ub-quick-example",
        "library": LAZY_ROOT / "build/lib/libns3.44-unified-bus.dylib",
    },
    "fastpath": {
        "root": REPO,
        "binary": REPO / "build/scratch/ns3.44-ub-quick-example",
        "library": REPO / "build/lib/libns3.44-unified-bus.dylib",
    },
}

TRACE_KEYS = (
    "UB_TRACE_ENABLE",
    "UB_TASK_TRACE_ENABLE",
    "UB_PACKET_TRACE_ENABLE",
    "UB_PORT_TRACE_ENABLE",
    "UB_QUEUE_TRACE_ENABLE",
    "UB_RECORD_PKT_TRACE",
    "UB_FLOW_CONTROL_TRACE_ENABLE",
    "UB_CONGESTION_CONTROL_TRACE_ENABLE",
    "UB_PARSE_TRACE_ENABLE",
)
INPUT_NAMES = {
    "node.csv",
    "topology.csv",
    "routing_table.csv",
    "transport_channel.csv",
    "traffic.csv",
    "network_attribute.txt",
}
LIFECYCLE_ARTIFACT = re.compile(r"(^|/)LifecycleTrace|(^|/)LifecycleTraceSchema")

THREE_CYCLIC_ORDERS = (
    ("next", "lazy", "fastpath"),
    ("lazy", "fastpath", "next"),
    ("fastpath", "next", "lazy"),
)
ALL_ROLE_ORDERS = (
    ("next", "lazy", "fastpath"),
    ("next", "fastpath", "lazy"),
    ("lazy", "next", "fastpath"),
    ("lazy", "fastpath", "next"),
    ("fastpath", "next", "lazy"),
    ("fastpath", "lazy", "next"),
)
NINE_BALANCED_ORDERS = ALL_ROLE_ORDERS + THREE_CYCLIC_ORDERS

CASE_CONFIG = {
    "scale-512m": {
        "total_payload_bytes": 512 * 1024**2,
        "traffic": "shared-write",
        "bytes_per_operation": 128 * 1024**2,
        "operations": 4,
        "warmups": 1,
        "orders": THREE_CYCLIC_ORDERS,
        "timeout": 180,
    },
    "scale-2g": {
        "total_payload_bytes": 2 * 1024**3,
        "traffic": "shared-write",
        "bytes_per_operation": 512 * 1024**2,
        "operations": 4,
        "warmups": 2,
        "orders": NINE_BALANCED_ORDERS,
        "timeout": 360,
    },
    "scale-8g": {
        "total_payload_bytes": 8 * 1024**3,
        "traffic": "shared-write-16",
        "bytes_per_operation": 512 * 1024**2,
        "operations": 16,
        "warmups": 1,
        "orders": THREE_CYCLIC_ORDERS,
        "timeout": 1200,
    },
    "multi-entity-2g": {
        "total_payload_bytes": 2 * 1024**3,
        "traffic": "multi-entity-write",
        "bytes_per_operation": 128 * 1024**2,
        "operations": 16,
        "warmups": 1,
        "orders": ALL_ROLE_ORDERS,
        "timeout": 480,
    },
    "mixed-bidir-2g": {
        "total_payload_bytes": 2 * 1024**3,
        "traffic": "mixed-bidirectional",
        "bytes_per_operation": 128 * 1024**2,
        "operations": 16,
        "warmups": 1,
        "orders": ALL_ROLE_ORDERS,
        "timeout": 480,
    },
}
CORRECTNESS_CASES = ("multi-entity-2g", "mixed-bidir-2g")
FROZEN_CTP_CASES = ("ctp-win1", "ctp-win4", "ctp-win128")
FROZEN_CTP_ROOT = (
    REPO / "scratch/20260717-next-lifecycle-ctp-rtp-complex-differential/cases"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def git_output(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=root, text=True).strip()


def role_manifest() -> dict:
    manifest = {}
    for role, config in ROLES.items():
        root = config["root"]
        if not root.is_dir():
            raise RuntimeError(f"missing role root: {role}: {root}")
        head = git_output(root, "rev-parse", "HEAD")
        expected_head = EXPECTED_HEADS[role]
        fastpath_commit_is_ancestor = role == "fastpath" and subprocess.run(
            ["git", "merge-base", "--is-ancestor", expected_head, head],
            cwd=root,
            check=False,
        ).returncode == 0
        if head != expected_head and not fastpath_commit_is_ancestor:
            raise RuntimeError(
                f"role {role} moved: expected {expected_head}, found {head}"
            )
        binary = config["binary"]
        library = config["library"]
        if not binary.is_file() or not library.is_file():
            raise RuntimeError(f"missing built role artifacts for {role}")
        entry = {
            "revision": head,
            "source_root": str(root),
            "binary": str(binary),
            "binary_sha256": sha256(binary),
            "unified_bus_library": str(library),
            "unified_bus_library_sha256": sha256(library),
        }
        if role == "fastpath":
            sources = (
                REPO / "src/unified-bus/model/protocol/ub-ctp.cc",
                REPO / "src/unified-bus/model/ub-lifecycle.cc",
                REPO / "src/unified-bus/model/ub-lifecycle.h",
            )
            diff = subprocess.check_output(
                [
                    "git",
                    "diff",
                    "--binary",
                    "a95f8bc",
                    "--",
                    *(str(source.relative_to(REPO)) for source in sources),
                ],
                cwd=REPO,
            )
            if not diff:
                raise RuntimeError("fastpath role has no production diff from a95f8bc")
            source_hashes = {
                str(source.relative_to(REPO)): sha256(source) for source in sources
            }
            if source_hashes != FASTPATH_PRODUCTION_SOURCE_SHA256:
                raise RuntimeError("fastpath production sources moved after T36 implementation")
            entry["production_source_sha256"] = source_hashes
            entry["production_diff_sha256"] = hashlib.sha256(diff).hexdigest()
        manifest[role] = entry
    return manifest


def copy_base(destination: Path) -> None:
    destination.mkdir(parents=True)
    for source in BASE_SOURCE.iterdir():
        if source.is_file() and source.name in INPUT_NAMES:
            shutil.copy2(source, destination / source.name)


def set_global(text: str, key: str, value: str) -> str:
    pattern = re.compile(rf'^global {re.escape(key)} "[^"]*"$', re.MULTILINE)
    replacement = f'global {key} "{value}"'
    if pattern.search(text):
        return pattern.sub(replacement, text)
    return text.rstrip() + "\n" + replacement + "\n"


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


def traffic_rows(kind: str, bytes_per_operation: int) -> list[dict[str, str]]:
    rows = []
    if kind in ("shared-write", "shared-write-16"):
        operation_count = 4 if kind == "shared-write" else 16
        for task_id in range(operation_count):
            rows.append(
                {
                    "taskId": str(task_id),
                    "sourceNode": "0",
                    "destNode": "1",
                    "dataSize(Byte)": str(bytes_per_operation),
                    "opType": "URMA_WRITE",
                    "priority": "7",
                    "delay": "10ns",
                    "phaseId": "0",
                    "dependOnPhases": "",
                    "srcEntityId": "0",
                    "dstEntityId": "0",
                }
            )
        return rows
    if kind == "multi-entity-write":
        for task_id in range(16):
            rows.append(
                {
                    "taskId": str(task_id),
                    "sourceNode": "0",
                    "destNode": "1",
                    "dataSize(Byte)": str(bytes_per_operation),
                    "opType": "URMA_WRITE",
                    "priority": "7",
                    "delay": "10ns",
                    "phaseId": "0",
                    "dependOnPhases": "",
                    "srcEntityId": str(task_id),
                    "dstEntityId": str(task_id),
                }
            )
        return rows
    if kind == "mixed-bidirectional":
        task_id = 0
        for opcode in ("URMA_WRITE", "URMA_READ"):
            for direction in ((0, 1), (1, 0)):
                for entity in range(4):
                    rows.append(
                        {
                            "taskId": str(task_id),
                            "sourceNode": str(direction[0]),
                            "destNode": str(direction[1]),
                            "dataSize(Byte)": str(bytes_per_operation),
                            "opType": opcode,
                            "priority": "7",
                            "delay": "10ns",
                            "phaseId": "0",
                            "dependOnPhases": "",
                            "srcEntityId": str(entity),
                            "dstEntityId": str(entity),
                        }
                    )
                    task_id += 1
        return rows
    raise RuntimeError(f"unknown traffic kind: {kind}")


def write_traffic(path: Path, rows: list[dict[str, str]]) -> None:
    fieldnames = [
        "taskId",
        "sourceNode",
        "destNode",
        "dataSize(Byte)",
        "opType",
        "priority",
        "delay",
        "phaseId",
        "dependOnPhases",
        "srcEntityId",
        "dstEntityId",
    ]
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def input_hashes(root: Path) -> dict[str, str]:
    return {
        path.name: sha256(path)
        for path in sorted(root.iterdir())
        if path.is_file() and path.name in INPUT_NAMES
    }


def materialize(replace: bool) -> None:
    if INPUTS.exists():
        if not replace:
            raise RuntimeError(f"refusing to overwrite materialized inputs: {INPUTS}")
        shutil.rmtree(INPUTS)
    for case, config in CASE_CONFIG.items():
        for mode in ("correctness", "performance"):
            destination = INPUTS / case / mode
            copy_base(destination)
            bytes_per_operation = int(config["bytes_per_operation"])
            if mode == "correctness" and case in CORRECTNESS_CASES:
                bytes_per_operation = 1024**2
            rows = traffic_rows(str(config["traffic"]), bytes_per_operation)
            write_traffic(destination / "traffic.csv", rows)
            attributes = destination / "network_attribute.txt"
            text = attributes.read_text(encoding="utf-8")
            text = re.sub(
                r'^global UB_LIFECYCLE_TRACE_LEVEL "[^"]*"\n?', "", text, flags=re.MULTILINE
            )
            if mode == "performance":
                for key in TRACE_KEYS:
                    text = set_global(text, key, "false")
            else:
                parser = REPO / "scratch/ns-3-ub-tools/trace_analysis/parse_trace.py"
                text = set_global(text, "UB_PYTHON_SCRIPT_PATH", str(parser.resolve()))
            attributes.write_text(text, encoding="utf-8")
            manifest = {
                "schema_version": 1,
                "case": case,
                "mode": mode,
                "operations": len(rows),
                "bytes_per_operation": bytes_per_operation,
                "total_payload_bytes": bytes_per_operation * len(rows),
                "hashes": input_hashes(destination),
            }
            destination.joinpath("input-manifest.json").write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
    RESULTS.mkdir(parents=True, exist_ok=True)
    PACKAGE.joinpath("materialization.json").write_text(
        json.dumps(
            {
                "schema_version": 1,
                "created_at": utc_now(),
                "base_source": str(BASE_SOURCE),
                "base_source_hashes": input_hashes(BASE_SOURCE),
                "cases": sorted(CASE_CONFIG),
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    update_checkpoint("Inputs", "complete; all templates and SHA-256 manifests materialized")


def parse_tasks(case_dir: Path) -> list[dict[str, str]]:
    path = case_dir / "output/task_statistics.csv"
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as stream:
        return list(csv.DictReader(stream))


def artifact_hashes(case_dir: Path) -> dict[str, str]:
    result = {}
    for dirname in ("output", "runlog"):
        root = case_dir / dirname
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(case_dir).as_posix()
            if LIFECYCLE_ARTIFACT.search(relative):
                continue
            result[relative] = sha256(path)
    return result


def run_once(
    case: str,
    mode: str,
    role: str,
    destination: Path,
    source_override: Path | None = None,
    timeout_override: int | None = None,
) -> dict:
    result_path = destination / "result.json"
    if result_path.is_file():
        return json.loads(result_path.read_text(encoding="utf-8"))
    if destination.exists():
        raise RuntimeError(f"incomplete or colliding run directory: {destination}")
    destination.mkdir(parents=True)
    source = source_override if source_override is not None else INPUTS / case / mode
    if not source.is_dir():
        raise RuntimeError(f"missing materialized input: {source}")
    case_dir = destination / "case-input"
    if source_override is None:
        shutil.copytree(source, case_dir)
    else:
        case_dir.mkdir()
        for input_name in INPUT_NAMES:
            source_file = source / input_name
            if source_file.is_file():
                shutil.copy2(source_file, case_dir / input_name)
    role_config = ROLES[role]
    timing = destination / "time-l.txt"
    command = [
        "/usr/bin/time",
        "-l",
        "-o",
        str(timing),
        "/usr/bin/env",
        f"DYLD_LIBRARY_PATH={role_config['library'].parent.resolve()}",
        str(role_config["binary"]),
        f"--case-path={case_dir}",
        "--rng-run=1",
    ]
    started_at = utc_now()
    elapsed = run_process(
        command,
        role_config["root"],
        timeout_override if timeout_override is not None else int(CASE_CONFIG[case]["timeout"]),
        destination,
    )
    result = parse_time_l(timing)
    result.update(
        {
            "schema_version": 1,
            "case": case,
            "mode": mode,
            "role": role,
            "started_at": started_at,
            "ended_at": utc_now(),
            "wall_seconds": elapsed,
            "input_hashes": input_hashes(case_dir),
            "artifact_hashes": artifact_hashes(case_dir),
            "tasks": parse_tasks(case_dir),
        }
    )
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result


def compare_correctness(case: str, results: dict[str, dict]) -> dict:
    control = results["next"]
    comparisons = {}
    for role in ("lazy", "fastpath"):
        item = results[role]
        control_files = control["artifact_hashes"]
        role_files = item["artifact_hashes"]
        common = sorted(set(control_files) & set(role_files))
        comparisons[role] = {
            "byte_identical": control_files == role_files,
            "task_rows_identical": control["tasks"] == item["tasks"],
            "changed_shared_files": [
                path for path in common if control_files[path] != role_files[path]
            ],
            "next_only_files": sorted(set(control_files) - set(role_files)),
            "role_only_files": sorted(set(role_files) - set(control_files)),
        }
    return {
        "case": case,
        "roles": results,
        "comparisons": comparisons,
        "pass": all(
            item["byte_identical"] and item["task_rows_identical"]
            for item in comparisons.values()
        ),
    }


def compare_result_pair(control: dict, treatment: dict) -> dict:
    control_files = control["artifact_hashes"]
    treatment_files = treatment["artifact_hashes"]
    common = sorted(set(control_files) & set(treatment_files))
    return {
        "byte_identical": control_files == treatment_files,
        "task_rows_identical": control["tasks"] == treatment["tasks"],
        "changed_shared_files": [
            path for path in common if control_files[path] != treatment_files[path]
        ],
        "control_only_files": sorted(set(control_files) - set(treatment_files)),
        "treatment_only_files": sorted(set(treatment_files) - set(control_files)),
    }


def run_queue_retirement_ablation(
    diagnostic_id: str = "queue-retirement-disabled",
    case: str = "multi-entity-2g",
) -> None:
    summary_path = RESULTS / "diagnostics" / diagnostic_id / "summary.json"
    if summary_path.is_file():
        print(summary_path.read_text(encoding="utf-8"), end="", flush=True)
        return
    control_path = RESULTS / "correctness" / case / "next" / "result.json"
    if control_path.is_file():
        control = json.loads(control_path.read_text(encoding="utf-8"))
    else:
        control = run_once(case, "correctness", "next", control_path.parent)
    treatment = run_once(
        case,
        "correctness",
        "fastpath",
        RESULTS / "diagnostics" / diagnostic_id / case,
    )
    comparison = compare_result_pair(control, treatment)
    input_identical = control["input_hashes"] == treatment["input_hashes"]
    summary = {
        "schema_version": 1,
        "diagnostic_id": diagnostic_id,
        "case": case,
        "control": control,
        "treatment": treatment,
        "input_hashes_identical": input_identical,
        "comparison": comparison,
        "pass": input_identical
        and comparison["byte_identical"]
        and comparison["task_rows_identical"],
        "source": role_manifest()["fastpath"],
    }
    summary_path.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    status = "matched next exactly" if summary["pass"] else "did not match next"
    update_checkpoint(
        "Queue retirement causality",
        f"complete; stable-queue diagnostic {status}; see {summary_path.relative_to(PACKAGE)}",
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)


def run_frozen_ctp_validation() -> None:
    result_root = RESULTS / "frozen-ctp-compact-history-v1"
    summaries = {}
    for case in FROZEN_CTP_CASES:
        source = FROZEN_CTP_ROOT / case / "lifecycle" / "case-input"
        control = run_once(
            case,
            "correctness",
            "next",
            result_root / case / "next",
            source,
            120,
        )
        treatment = run_once(
            case,
            "correctness",
            "fastpath",
            result_root / case / "fastpath",
            source,
            120,
        )
        comparison = compare_result_pair(control, treatment)
        summary = {
            "schema_version": 1,
            "case": case,
            "input_hashes_identical": control["input_hashes"] == treatment["input_hashes"],
            "comparison": comparison,
            "pass": control["input_hashes"] == treatment["input_hashes"]
            and comparison["byte_identical"]
            and comparison["task_rows_identical"],
        }
        result_root.joinpath(case, "summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        summaries[case] = summary
        print(
            f"frozen {case}: bytes={comparison['byte_identical']} "
            f"tasks={comparison['task_rows_identical']}",
            flush=True,
        )
        if not summary["pass"]:
            raise RuntimeError(f"unexplained frozen CTP difference: {case}")
    result_root.joinpath("summary.json").write_text(
        json.dumps(
            {"schema_version": 1, "roles": role_manifest(), "cases": summaries},
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


def run_correctness() -> None:
    role_manifest_data = role_manifest()
    root = RESULTS / "correctness"
    summaries = {}
    for case in CORRECTNESS_CASES:
        case_results = {}
        for role in ROLES:
            item = run_once(case, "correctness", role, root / case / role)
            case_results[role] = item
            print(
                f"correctness {case}/{role}: {item['wall_seconds']:.3f}s, "
                f"rss={item['peak_rss_bytes']}",
                flush=True,
            )
        summary = compare_correctness(case, case_results)
        root.joinpath(case, "summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        summaries[case] = summary
        if not summary["pass"]:
            raise RuntimeError(f"unexplained correctness difference: {case}")
    root.joinpath("summary.json").write_text(
        json.dumps(
            {"schema_version": 1, "roles": role_manifest_data, "cases": summaries},
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    update_checkpoint("Complex correctness", "complete; both three-role canaries byte-identical")


def median_fields(items: list[dict]) -> dict[str, float]:
    return {
        "median_wall_seconds": statistics.median(item["wall_seconds"] for item in items),
        "median_time_wall_seconds": statistics.median(
            item["time_wall_seconds"] for item in items
        ),
        "median_user_seconds": statistics.median(item["user_seconds"] for item in items),
        "median_system_seconds": statistics.median(
            item["system_seconds"] for item in items
        ),
        "median_peak_rss_bytes": statistics.median(
            item["peak_rss_bytes"] for item in items
        ),
    }


def percent(treatment: float, control: float) -> float:
    return 100.0 * (treatment / control - 1.0) if control else 0.0


def block_delta(block: dict[str, dict], treatment: str, control: str) -> dict:
    result = {}
    for field in ("wall_seconds", "user_seconds", "system_seconds", "peak_rss_bytes"):
        treatment_value = block[treatment][field]
        control_value = block[control][field]
        result[f"{field}_delta"] = treatment_value - control_value
        result[f"{field}_delta_percent"] = percent(treatment_value, control_value)
    return result


def summarize_deltas(items: list[dict]) -> dict:
    return {
        key: statistics.median(item[key] for item in items)
        for key in items[0]
    }


def run_performance_case(case: str) -> dict:
    config = CASE_CONFIG[case]
    root = RESULTS / "performance" / case
    warmups = {role: [] for role in ROLES}
    for warmup_index in range(1, int(config["warmups"]) + 1):
        for role in ROLES:
            item = run_once(
                case,
                "performance",
                role,
                root / f"warmup-{warmup_index}-{role}",
            )
            warmups[role].append(item)
            print(
                f"{case} warmup {warmup_index}/{role}: "
                f"wall={item['wall_seconds']:.3f}s user={item['user_seconds']:.3f}s "
                f"rss={item['peak_rss_bytes']}",
                flush=True,
            )

    measured = {role: [] for role in ROLES}
    blocks = []
    comparisons = {
        "lazy_vs_next": [],
        "fastpath_vs_next": [],
        "fastpath_vs_lazy": [],
    }
    for block_index, order in enumerate(config["orders"], 1):
        block = {}
        for position, role in enumerate(order, 1):
            item = run_once(
                case,
                "performance",
                role,
                root / f"block-{block_index}-{position}-{role}",
            )
            block[role] = item
            measured[role].append(item)
            print(
                f"{case} block {block_index}.{position}/{role}: "
                f"wall={item['wall_seconds']:.3f}s user={item['user_seconds']:.3f}s "
                f"rss={item['peak_rss_bytes']}",
                flush=True,
            )
        block_record = {
            "block": block_index,
            "order": list(order),
            "results": block,
        }
        blocks.append(block_record)
        comparisons["lazy_vs_next"].append(block_delta(block, "lazy", "next"))
        comparisons["fastpath_vs_next"].append(block_delta(block, "fastpath", "next"))
        comparisons["fastpath_vs_lazy"].append(block_delta(block, "fastpath", "lazy"))

    summary = {
        "schema_version": 1,
        "case": case,
        "total_payload_bytes": config["total_payload_bytes"],
        "warmups_per_role": config["warmups"],
        "measured_blocks": len(config["orders"]),
        "role_medians": {
            role: median_fields(items) for role, items in measured.items()
        },
        "comparisons": {
            name: {"median": summarize_deltas(items), "blocks": items}
            for name, items in comparisons.items()
        },
        "blocks": blocks,
    }
    root.mkdir(parents=True, exist_ok=True)
    root.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    for role in ROLES:
        update_ledger_row(
            case,
            role,
            "success",
            int(config["warmups"]) + len(config["orders"]),
            f"results/performance/{case}/summary.json",
            "serial warm-up and measured blocks complete",
        )
    return summary


def run_performance(selected: list[str]) -> None:
    roles = role_manifest()
    summaries = {}
    for case in selected:
        summaries[case] = run_performance_case(case)
    root = RESULTS / "performance"
    root.mkdir(parents=True, exist_ok=True)
    root.joinpath("summary.json").write_text(
        json.dumps(
            {"schema_version": 1, "roles": roles, "cases": summaries},
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    if set(selected) == set(CASE_CONFIG):
        update_checkpoint("Performance matrix", "complete; all five blocks executed serially")


def run_stable_queue_performance(
    selected: list[str], pair_count: int, result_id: str = "performance-stable-queue-v1"
) -> None:
    role_manifest_data = role_manifest()
    all_pair_orders = (
        ("next", "fastpath"),
        ("fastpath", "next"),
        ("next", "fastpath"),
        ("fastpath", "next"),
        ("next", "fastpath"),
    )
    if pair_count < 1 or pair_count > len(all_pair_orders):
        raise RuntimeError(f"pair count must be between 1 and {len(all_pair_orders)}")
    pair_orders = all_pair_orders[:pair_count]
    result_root = RESULTS / result_id
    summaries = {}
    for case in selected:
        root = result_root / case
        warmups = {}
        for role in ("next", "fastpath"):
            item = run_once(case, "performance", role, root / f"warmup-{role}")
            warmups[role] = item
            print(
                f"{case} warmup/{role}: wall={item['wall_seconds']:.3f}s "
                f"user={item['user_seconds']:.3f}s rss={item['peak_rss_bytes']}",
                flush=True,
            )

        measured = {"next": [], "fastpath": []}
        blocks = []
        deltas = []
        for block_index, order in enumerate(pair_orders, 1):
            block = {}
            for position, role in enumerate(order, 1):
                item = run_once(
                    case,
                    "performance",
                    role,
                    root / f"block-{block_index}-{position}-{role}",
                )
                block[role] = item
                measured[role].append(item)
                print(
                    f"{case} block {block_index}.{position}/{role}: "
                    f"wall={item['wall_seconds']:.3f}s user={item['user_seconds']:.3f}s "
                    f"rss={item['peak_rss_bytes']}",
                    flush=True,
                )
            delta = block_delta(block, "fastpath", "next")
            deltas.append(delta)
            blocks.append({"block": block_index, "order": list(order), "results": block})

        summary = {
            "schema_version": 1,
            "case": case,
            "total_payload_bytes": CASE_CONFIG[case]["total_payload_bytes"],
            "warmups": warmups,
            "measured_pairs": len(pair_orders),
            "role_medians": {
                role: median_fields(items) for role, items in measured.items()
            },
            "fastpath_vs_next": {
                "median": summarize_deltas(deltas),
                "pairs": deltas,
            },
            "blocks": blocks,
        }
        root.mkdir(parents=True, exist_ok=True)
        root.joinpath("summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        summaries[case] = summary

    aggregate_path = result_root / "summary.json"
    aggregate_cases = {}
    if aggregate_path.is_file():
        aggregate_cases.update(
            json.loads(aggregate_path.read_text(encoding="utf-8")).get("cases", {})
        )
    aggregate_cases.update(summaries)
    aggregate_path.write_text(
        json.dumps(
            {"schema_version": 1, "roles": role_manifest_data, "cases": aggregate_cases},
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


def linear_slope(points: list[tuple[float, float]]) -> float:
    x_mean = statistics.mean(x for x, _ in points)
    y_mean = statistics.mean(y for _, y in points)
    denominator = sum((x - x_mean) ** 2 for x, _ in points)
    if denominator == 0:
        return 0.0
    return sum((x - x_mean) * (y - y_mean) for x, y in points) / denominator


def analyze() -> dict:
    correctness_path = RESULTS / "correctness/summary.json"
    performance_path = RESULTS / "performance/summary.json"
    if not correctness_path.is_file() or not performance_path.is_file():
        raise RuntimeError("correctness and full performance summaries are required")
    correctness = json.loads(correctness_path.read_text(encoding="utf-8"))
    performance = json.loads(performance_path.read_text(encoding="utf-8"))
    cases = performance["cases"]
    missing = sorted(set(CASE_CONFIG) - set(cases))
    if missing:
        raise RuntimeError(f"performance matrix incomplete: {missing}")

    scale_cases = ("scale-512m", "scale-2g", "scale-8g")
    rss_scaling = {}
    for role in ROLES:
        points = [
            (
                float(cases[case]["total_payload_bytes"]),
                float(cases[case]["role_medians"][role]["median_peak_rss_bytes"]),
            )
            for case in scale_cases
        ]
        rss_scaling[role] = {
            "points": [
                {"total_payload_bytes": int(x), "median_peak_rss_bytes": int(y)}
                for x, y in points
            ],
            "slope_rss_bytes_per_payload_byte": linear_slope(points),
        }

    eight = cases["scale-8g"]["role_medians"]
    fastpath_8g_rss = eight["fastpath"]["median_peak_rss_bytes"]
    next_8g_rss = eight["next"]["median_peak_rss_bytes"]
    analysis = {
        "schema_version": 1,
        "created_at": utc_now(),
        "correctness_pass": all(
            item["pass"] for item in correctness["cases"].values()
        ),
        "correctness": correctness["cases"],
        "performance": cases,
        "rss_scaling": rss_scaling,
        "scale_8g": {
            "fastpath_rss_excess_bytes_vs_next": fastpath_8g_rss - next_8g_rss,
            "fastpath_rss_ratio_vs_next": fastpath_8g_rss / next_8g_rss,
        },
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "logical_cpus": os.cpu_count(),
        },
    }
    RESULTS.joinpath("analysis.json").write_text(
        json.dumps(analysis, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    update_checkpoint("Analysis", "complete; three-role correctness, CPU, wall, and RSS summarized")
    return analysis


def update_ledger_row(
    case: str, role: str, status: str, runs: int, evidence: str, observation: str
) -> None:
    lines = LEDGER.read_text(encoding="utf-8").splitlines()
    prefix = f"| `{case}` | {role} |"
    matches = [index for index, line in enumerate(lines) if line.startswith(prefix)]
    if len(matches) != 1:
        raise RuntimeError(f"ledger row not unique: {case}/{role}")
    lines[matches[0]] = (
        f"| `{case}` | {role} | {status} | {runs} | `{evidence}` | {observation} |"
    )
    LEDGER.write_text("\n".join(lines) + "\n", encoding="utf-8")


def update_checkpoint(name: str, value: str) -> None:
    lines = LEDGER.read_text(encoding="utf-8").splitlines()
    prefix = f"- {name}:"
    matches = [index for index, line in enumerate(lines) if line.startswith(prefix)]
    if len(matches) != 1:
        raise RuntimeError(f"checkpoint row not unique: {name}")
    lines[matches[0]] = f"- {name}: {value}."
    LEDGER.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "action",
        choices=(
            "materialize",
            "correctness",
            "queue-retirement-ablation",
            "stable-queue-validation",
            "stable-queue-complex-validation",
            "stable-queue-performance",
            "compact-history-validation",
            "compact-history-frozen-validation",
            "compact-history-performance",
            "performance",
            "analyze",
            "all",
        ),
    )
    parser.add_argument("--replace-inputs", action="store_true")
    parser.add_argument(
        "--cases",
        default=",".join(CASE_CONFIG),
        help="comma-separated performance case IDs",
    )
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--result-id", default="")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    selected = [item.strip() for item in args.cases.split(",") if item.strip()]
    unknown = sorted(set(selected) - set(CASE_CONFIG))
    if unknown:
        raise RuntimeError(f"unknown cases: {unknown}")
    if args.action in ("materialize", "all"):
        materialize(args.replace_inputs)
    if args.action in ("correctness", "all"):
        run_correctness()
    if args.action == "queue-retirement-ablation":
        run_queue_retirement_ablation()
    if args.action == "stable-queue-validation":
        run_queue_retirement_ablation("stable-queue-production-v1")
    if args.action == "stable-queue-complex-validation":
        for case in CORRECTNESS_CASES:
            run_queue_retirement_ablation(f"stable-queue-production-v2-{case}", case)
    if args.action == "compact-history-validation":
        for case in CORRECTNESS_CASES:
            run_queue_retirement_ablation(f"compact-history-production-v1-{case}", case)
    if args.action == "compact-history-frozen-validation":
        run_frozen_ctp_validation()
    if args.action in ("performance", "all"):
        run_performance(selected)
    if args.action == "stable-queue-performance":
        run_stable_queue_performance(selected, args.pairs)
    if args.action == "compact-history-performance":
        run_stable_queue_performance(
            selected,
            args.pairs,
            args.result_id or "performance-compact-history-v1",
        )
    if args.action in ("analyze", "all"):
        analysis = analyze()
        print(json.dumps(analysis, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
