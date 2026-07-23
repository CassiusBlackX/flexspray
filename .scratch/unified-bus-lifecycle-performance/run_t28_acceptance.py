#!/usr/bin/env python3
"""Run the frozen, strictly serial T28 lifecycle acceptance matrix."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
EVIDENCE_ROOT = HERE / "evidence/t28-final-v1"
P01_SOURCE = (
    REPO_ROOT
    / "scratch/20260716-gitcode-main-lifecycle-differential/cases/p01-lifecycle/case-input"
)
P02_SOURCE = (
    REPO_ROOT
    / "scratch/20260716-gitcode-main-lifecycle-differential/cases/p02-lifecycle/case-input"
)
COMPLEX_PACKAGE = REPO_ROOT / "scratch/20260717-next-lifecycle-ctp-rtp-complex-differential"
TRACE_PARSER_ROOT = REPO_ROOT / "scratch/ns-3-ub-tools/trace_analysis"
sys.path.insert(0, str(TRACE_PARSER_ROOT))
from lifecycle_trace import parse_case  # noqa: E402


ROLES = {
    "main": {
        "source_root": REPO_ROOT / ".worktrees/t00-reference-main",
        "revision": "ccf41b58b0ba3b9711efea595732a2908e3e07cf",
    },
    "current": {
        "source_root": REPO_ROOT,
        "revision": "85c61e8bc4a6a47971f1e7f83e3eced72f47f531",
    },
    "next": {
        "source_root": Path("/Users/ytxing/workspace/ns-3-ub-routing-modes"),
        "revision": "cf8ee683e00e5b6210661013e93cec7217d662e5",
    },
}
TRIPLE_ORDERS = (
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
COMPLEX_CASES = (
    "ctp-win1",
    "ctp-win4",
    "ctp-win128",
    "rtp-shared-win1",
    "rtp-gbn-double-drop",
    "rtp-tpg-gbn",
)
INPUT_NAMES = {
    "node.csv",
    "topology.csv",
    "routing_table.csv",
    "transport_channel.csv",
    "traffic.csv",
    "network_attribute.txt",
    "fault.csv",
    "retrans_fault.csv",
}
RUN_TIMEOUT_SECONDS = 120
LIFECYCLE_ARTIFACT = re.compile(r"(^|/)LifecycleTrace|(^|/)lifecycle")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_digest(root: Path, *, inputs_only: bool = False) -> str:
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if not path.is_file() or (inputs_only and path.name not in INPUT_NAMES):
            continue
        relative = path.relative_to(root).as_posix().encode()
        digest.update(relative)
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


def command_output(command: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(command, cwd=cwd, text=True, stderr=subprocess.STDOUT).strip()


def binary_for(role: str) -> Path:
    return ROLES[role]["source_root"] / "build/scratch/ns3.44-ub-quick-example"


def library_for(role: str) -> Path:
    return ROLES[role]["source_root"] / "build/lib/libns3.44-unified-bus.dylib"


def cmake_facts(root: Path) -> dict[str, str]:
    cache = root / "cmake-cache/CMakeCache.txt"
    wanted = {
        "CMAKE_BUILD_TYPE",
        "CMAKE_CXX_COMPILER",
        "NS3_EXAMPLES",
        "NS3_MPI",
        "NS3_TESTS",
    }
    result = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith("//") or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key_type, value = line.split("=", 1)
        key = key_type.split(":", 1)[0]
        if key in wanted:
            result[key] = value
    return result


def input_hashes(root: Path) -> dict[str, str]:
    return {
        path.name: sha256(path)
        for path in sorted(root.iterdir())
        if path.is_file() and path.name in INPUT_NAMES
    }


def freeze() -> None:
    if EVIDENCE_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite T28 evidence: {EVIDENCE_ROOT}")
    EVIDENCE_ROOT.mkdir(parents=True)
    role_manifest = {}
    for role, config in ROLES.items():
        root = config["source_root"]
        actual_revision = command_output(["git", "rev-parse", "HEAD"], root)
        if actual_revision != config["revision"]:
            raise RuntimeError(
                f"{role} revision changed: expected {config['revision']}, got {actual_revision}"
            )
        binary = binary_for(role)
        library = library_for(role)
        if not binary.is_file() or not library.is_file():
            raise RuntimeError(f"missing frozen binary or library for {role}")
        role_manifest[role] = {
            "revision": actual_revision,
            "source_root": str(root),
            "binary": str(binary),
            "binary_sha256": sha256(binary),
            "binary_bytes": binary.stat().st_size,
            "unified_bus_library": str(library),
            "unified_bus_library_sha256": sha256(library),
            "unified_bus_library_bytes": library.stat().st_size,
            "cmake": cmake_facts(root),
            "binary_dependencies": command_output(["otool", "-L", str(binary)]).splitlines(),
        }

    complex_inputs = {}
    archived_outputs = {}
    for case in COMPLEX_CASES:
        role_hashes = {}
        for archived_role in ("next", "lifecycle"):
            case_dir = COMPLEX_PACKAGE / "cases" / case / archived_role / "case-input"
            role_hashes[archived_role] = input_hashes(case_dir)
            archived_outputs[f"{case}/{archived_role}"] = tree_digest(case_dir)
        if role_hashes["next"] != role_hashes["lifecycle"]:
            raise RuntimeError(f"complex paired inputs differ for {case}")
        complex_inputs[case] = role_hashes["next"]

    manifest = {
        "schema_version": 1,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "roles": role_manifest,
        "runtime": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "uname": command_output(["uname", "-a"]),
            "sw_vers": command_output(["sw_vers"]).splitlines(),
            "cxx": command_output(["/usr/bin/c++", "--version"]).splitlines(),
        },
        "inputs": {
            "p01": {"source": str(P01_SOURCE), "hashes": input_hashes(P01_SOURCE)},
            "p02": {"source": str(P02_SOURCE), "hashes": input_hashes(P02_SOURCE)},
            "complex": complex_inputs,
        },
        "archived_complex_tree_digests": archived_outputs,
        "transforms": {
            "p01_all_roles": 'default ns3::UbLdstApi::UsePacketSpray "false"',
            "p01_current": 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"',
            "p02_all_roles": 'default ns3::UbRoutingProcess::RoutingAlgorithm "HASH"',
            "p02_current_modes": ["OFF", "SUMMARY"],
            "complex_current": 'global UB_LIFECYCLE_TRACE_LEVEL "SUMMARY"',
        },
        "commands": {
            "run": "/usr/bin/time -l <frozen-binary> --case-path=<copied-case> --rng-run=1",
            "performance": "python3.12 .scratch/unified-bus-lifecycle-performance/run_t28_acceptance.py performance",
            "complex": "python3.12 .scratch/unified-bus-lifecycle-performance/run_t28_acceptance.py complex",
            "finalize": "python3.12 .scratch/unified-bus-lifecycle-performance/run_t28_acceptance.py finalize",
        },
        "orders": {
            "p01_p02": TRIPLE_ORDERS,
            "p02_summary": SUMMARY_ORDERS,
            "complex": "alternating next/current, strictly serial",
        },
        "automatic_tp_cleanup": "excluded",
    }
    (EVIDENCE_ROOT / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(role_manifest, indent=2, sort_keys=True))


def load_manifest() -> dict:
    path = EVIDENCE_ROOT / "manifest.json"
    if not path.is_file():
        raise RuntimeError("run the T28 freeze phase first")
    return json.loads(path.read_text(encoding="utf-8"))


def verify_frozen() -> dict:
    manifest = load_manifest()
    for role, expected in manifest["roles"].items():
        root = ROLES[role]["source_root"]
        actual_revision = command_output(["git", "rev-parse", "HEAD"], root)
        actual_binary = sha256(binary_for(role))
        actual_library = sha256(library_for(role))
        if actual_revision != expected["revision"]:
            raise RuntimeError(f"{role} revision moved after freeze")
        if actual_binary != expected["binary_sha256"]:
            raise RuntimeError(f"{role} binary changed after freeze")
        if actual_library != expected["unified_bus_library_sha256"]:
            raise RuntimeError(f"{role} Unified Bus library changed after freeze")
    return manifest


def copy_input(source: Path, destination: Path, case_name: str, role: str, mode: str = "off") -> Path:
    if destination.exists():
        raise RuntimeError(f"refusing to overwrite run destination: {destination}")
    case_dir = destination / "case-input"
    case_dir.mkdir(parents=True)
    for path in source.iterdir():
        if path.is_file() and path.name in INPUT_NAMES:
            shutil.copy2(path, case_dir / path.name)

    attributes = case_dir / "network_attribute.txt"
    text = attributes.read_text(encoding="utf-8")
    if case_name == "p01":
        old = 'default ns3::UbLdstApi::UsePacketSpray "true"'
        if text.count(old) != 1:
            raise RuntimeError("p01 input does not contain one enabled packet-spray declaration")
        text = text.replace(old, 'default ns3::UbLdstApi::UsePacketSpray "false"')
        if role == "current":
            text += 'global UB_LIFECYCLE_TRACE_LEVEL "OFF"\n'
    elif case_name == "p02":
        old = 'default ns3::UbRoutingProcess::RoutingAlgorithm "ADAPTIVE"'
        if text.count(old) != 1:
            raise RuntimeError("p02 input does not contain one ADAPTIVE routing declaration")
        text = text.replace(old, 'default ns3::UbRoutingProcess::RoutingAlgorithm "HASH"')
        if role == "current":
            text += f'global UB_LIFECYCLE_TRACE_LEVEL "{mode.upper()}"\n'
    elif case_name in COMPLEX_CASES and role == "current":
        if "UB_LIFECYCLE_TRACE_LEVEL" in text:
            raise RuntimeError(f"complex input unexpectedly defines lifecycle level: {case_name}")
        text += 'global UB_LIFECYCLE_TRACE_LEVEL "SUMMARY"\n'
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


def artifact_hashes(case_dir: Path, *, shared_only: bool = False) -> dict[str, str]:
    result = {}
    for parent_name in ("output", "runlog"):
        parent = case_dir / parent_name
        if not parent.exists():
            continue
        for path in sorted(parent.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(case_dir).as_posix()
            if shared_only and LIFECYCLE_ARTIFACT.search(relative):
                continue
            result[relative] = sha256(path)
    return result


def count_csv_rows(path: Path) -> int:
    if not path.is_file():
        return 0
    with path.open(newline="", encoding="utf-8-sig") as stream:
        return sum(1 for _ in csv.DictReader(stream))


def count_nonempty_lines(paths: list[Path]) -> int:
    count = 0
    for path in paths:
        with path.open(encoding="utf-8", errors="replace") as stream:
            count += sum(1 for line in stream if line.strip())
    return count


def artifact_metrics(case_dir: Path) -> dict[str, int]:
    runlog = case_dir / "runlog"
    shared_traces = [
        path
        for path in sorted(runlog.rglob("*"))
        if path.is_file() and not LIFECYCLE_ARTIFACT.search(path.relative_to(case_dir).as_posix())
    ] if runlog.exists() else []
    task_traces = [path for path in shared_traces if path.name.startswith("TaskTrace")]
    port_traces = [path for path in shared_traces if path.name.startswith("PortTrace")]
    lifecycle_paths = sorted(runlog.glob("LifecycleTrace*")) if runlog.exists() else []
    result = {
        "task_output_count": count_csv_rows(case_dir / "output/task_statistics.csv"),
        "input_transport_resource_count": count_csv_rows(case_dir / "transport_channel.csv"),
        "task_trace_event_count": count_nonempty_lines(task_traces),
        "port_trace_event_count": count_nonempty_lines(port_traces),
        "shared_trace_line_count": count_nonempty_lines(shared_traces),
        "shared_trace_bytes": sum(path.stat().st_size for path in shared_traces),
        "lifecycle_record_count": 0,
        "lifecycle_unique_resource_generations": 0,
        "lifecycle_bytes": sum(path.stat().st_size for path in lifecycle_paths),
    }
    if list(runlog.glob("LifecycleTrace_node_*.tr")):
        records = parse_case(case_dir)
        result["lifecycle_record_count"] = len(records)
        result["lifecycle_unique_resource_generations"] = len(
            {
                (
                    item.values["ownerNode"],
                    item.values["resourceKind"],
                    item.values["localId"],
                    item.values["generation"],
                )
                for item in records
            }
        )
    return result


def run_repetition(
    role: str,
    destination: Path,
    source: Path,
    case_name: str,
    mode: str = "off",
) -> dict:
    case_dir = copy_input(source, destination, case_name, role, mode)
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
            f"run failed: case={case_name}, role={role}, mode={mode}, "
            f"rc={process.returncode}, timed_out={timed_out}, artifact={destination}"
        )
    result = parse_time_l(timing_path)
    result.update(artifact_metrics(case_dir))
    result.update(
        {
            "case": case_name,
            "role": role,
            "mode": mode,
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


def pure_tasktrace_reorder(left: Path, right: Path) -> bool:
    left_lines = left.read_text(encoding="utf-8").splitlines()
    right_lines = right.read_text(encoding="utf-8").splitlines()
    timestamp = re.compile(r"^\[([^]]+)\]")

    def timestamps(lines: list[str]) -> list[str]:
        result = []
        for line in lines:
            match = timestamp.match(line)
            if match is None:
                raise RuntimeError(f"TaskTrace line has no timestamp: {line!r}")
            result.append(match.group(1))
        return result

    return (
        left_lines != right_lines
        and Counter(left_lines) == Counter(right_lines)
        and timestamps(left_lines) == timestamps(right_lines)
    )


def compare_artifacts(
    left: dict,
    right: dict,
    left_case: Path,
    right_case: Path,
    *,
    shared_only: bool = True,
) -> dict:
    left_hashes = artifact_hashes(left_case, shared_only=shared_only)
    right_hashes = artifact_hashes(right_case, shared_only=shared_only)
    common = sorted(set(left_hashes) & set(right_hashes))
    changed = [name for name in common if left_hashes[name] != right_hashes[name]]
    controlled = []
    unexplained = []
    for name in changed:
        if name.startswith("runlog/TaskTrace_node_") and pure_tasktrace_reorder(
            left_case / name, right_case / name
        ):
            controlled.append(
                {
                    "path": name,
                    "cause": "pure same-timestamp TaskTrace full-line reorder",
                    "left_sha256": left_hashes[name],
                    "right_sha256": right_hashes[name],
                }
            )
        else:
            unexplained.append(name)
    return {
        "left_role": left["role"],
        "right_role": right["role"],
        "common_file_count": len(common),
        "left_only": sorted(set(left_hashes) - set(right_hashes)),
        "right_only": sorted(set(right_hashes) - set(left_hashes)),
        "changed": changed,
        "controlled_differences": controlled,
        "unexplained_differences": unexplained,
    }


MEDIAN_FIELDS = (
    "wall_seconds",
    "time_l_wall_seconds",
    "user_seconds",
    "system_seconds",
    "peak_rss_bytes",
    "page_faults",
    "block_input_operations",
    "block_output_operations",
    "task_output_count",
    "input_transport_resource_count",
    "task_trace_event_count",
    "port_trace_event_count",
    "shared_trace_line_count",
    "shared_trace_bytes",
    "lifecycle_record_count",
    "lifecycle_unique_resource_generations",
    "lifecycle_bytes",
)


def medians(results: list[dict]) -> dict[str, float]:
    return {
        f"median_{field}": statistics.median(item[field] for item in results)
        for field in MEDIAN_FIELDS
    }


def delta_percent(treatment: float, control: float) -> float:
    if control == 0:
        return 0.0 if treatment == 0 else float("inf")
    return 100 * (treatment / control - 1)


def performance_case(case_name: str, source: Path, root: Path) -> dict:
    warmups = {}
    for role in ROLES:
        destination = root / f"warmup-{role}"
        warmups[role] = run_repetition(role, destination, source, case_name)
        print(
            f"{case_name} warmup {role}: {warmups[role]['wall_seconds']:.6f}s",
            flush=True,
        )

    results = {role: [] for role in ROLES}
    paired = []
    comparisons = []
    for round_index, order in enumerate(TRIPLE_ORDERS, 1):
        round_results = {}
        destinations = {}
        for position, role in enumerate(order, 1):
            destination = root / f"round-{round_index}-{position}-{role}"
            item = run_repetition(role, destination, source, case_name)
            results[role].append(item)
            round_results[role] = item
            destinations[role] = destination / "case-input"
            print(
                f"{case_name} round {round_index}.{position} {role}: "
                f"{item['wall_seconds']:.6f}s rss={item['peak_rss_bytes']}",
                flush=True,
            )
        paired.append(
            {
                "round": round_index,
                "current_vs_main_wall_percent": delta_percent(
                    round_results["current"]["wall_seconds"],
                    round_results["main"]["wall_seconds"],
                ),
                "current_vs_next_wall_percent": delta_percent(
                    round_results["current"]["wall_seconds"],
                    round_results["next"]["wall_seconds"],
                ),
                "current_vs_main_rss_percent": delta_percent(
                    round_results["current"]["peak_rss_bytes"],
                    round_results["main"]["peak_rss_bytes"],
                ),
                "current_vs_next_rss_percent": delta_percent(
                    round_results["current"]["peak_rss_bytes"],
                    round_results["next"]["peak_rss_bytes"],
                ),
            }
        )
        for control in ("main", "next"):
            comparisons.append(
                {
                    "round": round_index,
                    "control": control,
                    **compare_artifacts(
                        round_results[control],
                        round_results["current"],
                        destinations[control],
                        destinations["current"],
                    ),
                }
            )

    role_medians = {role: medians(items) for role, items in results.items()}
    gates = {}
    for control in ("main", "next"):
        wall = statistics.median(
            item[f"current_vs_{control}_wall_percent"] for item in paired
        )
        rss = statistics.median(
            item[f"current_vs_{control}_rss_percent"] for item in paired
        )
        gates[control] = {
            "median_paired_wall_delta_percent": wall,
            "median_paired_rss_delta_percent": rss,
            "wall_within_10_percent": wall <= 10.0,
            "rss_within_10_percent": rss <= 10.0,
            "passed": wall <= 10.0 and rss <= 10.0,
        }
    semantic_passed = all(
        not item["left_only"]
        and not item["right_only"]
        and not item["unexplained_differences"]
        for item in comparisons
    )
    return {
        "roles": role_medians,
        "paired_rounds": paired,
        "gates": gates,
        "semantic_comparisons": comparisons,
        "semantic_passed": semantic_passed,
        "performance_passed": all(item["passed"] for item in gates.values()),
    }


def summary_mode_case(root: Path) -> dict:
    warmups = {}
    for mode in ("off", "summary"):
        destination = root / f"warmup-{mode}"
        warmups[mode] = run_repetition("current", destination, P02_SOURCE, "p02", mode)
        print(f"p02 {mode} warmup: {warmups[mode]['wall_seconds']:.6f}s", flush=True)

    results = {"off": [], "summary": []}
    paired = []
    comparisons = []
    for round_index, order in enumerate(SUMMARY_ORDERS, 1):
        round_results = {}
        destinations = {}
        for position, mode in enumerate(order, 1):
            destination = root / f"round-{round_index}-{position}-{mode}"
            item = run_repetition("current", destination, P02_SOURCE, "p02", mode)
            results[mode].append(item)
            round_results[mode] = item
            destinations[mode] = destination / "case-input"
            print(
                f"p02 summary round {round_index}.{position} {mode}: "
                f"{item['wall_seconds']:.6f}s rss={item['peak_rss_bytes']}",
                flush=True,
            )
        paired.append(
            {
                "round": round_index,
                "summary_vs_off_wall_percent": delta_percent(
                    round_results["summary"]["wall_seconds"],
                    round_results["off"]["wall_seconds"],
                ),
                "summary_vs_off_rss_percent": delta_percent(
                    round_results["summary"]["peak_rss_bytes"],
                    round_results["off"]["peak_rss_bytes"],
                ),
            }
        )
        comparisons.append(
            {
                "round": round_index,
                **compare_artifacts(
                    round_results["off"],
                    round_results["summary"],
                    destinations["off"],
                    destinations["summary"],
                    shared_only=True,
                ),
            }
        )
    semantic_passed = all(
        not item["left_only"]
        and not item["right_only"]
        and not item["unexplained_differences"]
        for item in comparisons
    )
    return {
        "modes": {mode: medians(items) for mode, items in results.items()},
        "paired_rounds": paired,
        "median_paired_wall_delta_percent": statistics.median(
            item["summary_vs_off_wall_percent"] for item in paired
        ),
        "median_paired_rss_delta_percent": statistics.median(
            item["summary_vs_off_rss_percent"] for item in paired
        ),
        "semantic_comparisons": comparisons,
        "semantic_passed": semantic_passed,
    }


def performance() -> None:
    verify_frozen()
    root = EVIDENCE_ROOT / "performance"
    if root.exists():
        raise RuntimeError(f"refusing to overwrite T28 performance evidence: {root}")
    root.mkdir()
    result = {
        "schema_version": 1,
        "p01": performance_case("p01", P01_SOURCE, root / "p01"),
        "p02": performance_case("p02", P02_SOURCE, root / "p02"),
        "p02_summary": summary_mode_case(root / "p02-summary"),
    }
    result["semantic_passed"] = (
        result["p01"]["semantic_passed"]
        and result["p02"]["semantic_passed"]
        and result["p02_summary"]["semantic_passed"]
    )
    result["performance_passed"] = (
        result["p01"]["performance_passed"] and result["p02"]["performance_passed"]
    )
    result["status"] = (
        "passed"
        if result["semantic_passed"] and result["performance_passed"]
        else "failed"
    )
    (root / "summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps({"status": result["status"], "p01": result["p01"]["gates"], "p02": result["p02"]["gates"]}, indent=2))


def read_tasks(case_dir: Path) -> list[dict[str, str]]:
    path = case_dir / "output/task_statistics.csv"
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as stream:
        return list(csv.DictReader(stream))


def exact_tree_comparison(new_case: Path, archived_case: Path) -> dict:
    new_hashes = artifact_hashes(new_case)
    archived_hashes = artifact_hashes(archived_case)
    common = sorted(set(new_hashes) & set(archived_hashes))
    changed = [name for name in common if new_hashes[name] != archived_hashes[name]]
    return {
        "common_file_count": len(common),
        "new_only": sorted(set(new_hashes) - set(archived_hashes)),
        "archived_only": sorted(set(archived_hashes) - set(new_hashes)),
        "changed": changed,
        "exact": not changed
        and set(new_hashes) == set(archived_hashes),
    }


def run_complex_case(case: str, role: str, destination: Path) -> dict:
    source = COMPLEX_PACKAGE / "cases" / case / "lifecycle/case-input"
    return run_repetition(role, destination, source, case)


def complex_matrix() -> None:
    verify_frozen()
    root = EVIDENCE_ROOT / "complex-v2-explicit-summary"
    if root.exists():
        raise RuntimeError(f"refusing to overwrite T28 complex evidence: {root}")
    root.mkdir()
    cases = {}
    for index, case in enumerate(COMPLEX_CASES):
        order = ("next", "current") if index % 2 == 0 else ("current", "next")
        results = {}
        destinations = {}
        for role in order:
            destination = root / case / role
            result = run_complex_case(case, role, destination)
            results[role] = result
            destinations[role] = destination / "case-input"
            print(
                f"complex {case} {role}: {result['wall_seconds']:.6f}s, "
                f"tasks={result['task_output_count']}",
                flush=True,
            )

        shared = compare_artifacts(
            results["next"],
            results["current"],
            destinations["next"],
            destinations["current"],
        )
        replay = {
            "next": exact_tree_comparison(
                destinations["next"],
                COMPLEX_PACKAGE / "cases" / case / "next/case-input",
            ),
            "current": exact_tree_comparison(
                destinations["current"],
                COMPLEX_PACKAGE / "cases" / case / "lifecycle/case-input",
            ),
        }
        tasks = {role: read_tasks(destinations[role]) for role in ("next", "current")}
        task_ids = {role: [row.get("taskId") for row in rows] for role, rows in tasks.items()}
        replay_exact = replay["next"]["exact"] and replay["current"]["exact"]
        if case.startswith("ctp-"):
            accepted = (
                replay_exact
                and not shared["left_only"]
                and not shared["right_only"]
                and not shared["changed"]
                and tasks["next"] == tasks["current"]
            )
            classification = "byte-identical" if accepted else "regression"
        else:
            accepted = replay_exact and task_ids["next"] == task_ids["current"]
            classification = "approved-existing-difference" if accepted else "regression"
        cases[case] = {
            "classification": classification,
            "accepted": accepted,
            "shared": shared,
            "archived_replay": replay,
            "task_ids": task_ids,
            "tasks": tasks,
            "current_lifecycle": {
                key: results["current"][key]
                for key in (
                    "lifecycle_record_count",
                    "lifecycle_unique_resource_generations",
                    "lifecycle_bytes",
                )
            },
        }

    ctp_window_effect = {}
    for role in ("next", "current"):
        ctp_window_effect[role] = {
            str(window): [
                row.get("taskCompletesTime(us)")
                for row in cases[f"ctp-win{window}"]["tasks"][role]
            ]
            for window in (1, 4, 128)
        }
    ctp_non_degenerate = (
        ctp_window_effect["current"] == ctp_window_effect["next"]
        and len({tuple(values) for values in ctp_window_effect["current"].values()}) == 3
    )
    result = {
        "schema_version": 1,
        "cases": cases,
        "ctp_window_effect": ctp_window_effect,
        "ctp_non_degenerate_and_matched": ctp_non_degenerate,
        "automatic_tp_cleanup": "excluded",
        "status": "passed"
        if ctp_non_degenerate and all(item["accepted"] for item in cases.values())
        else "failed",
    }
    (root / "summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps({"status": result["status"], "ctp_window_effect": ctp_window_effect}, indent=2))


def finalize() -> None:
    manifest = load_manifest()
    performance_path = EVIDENCE_ROOT / "performance/summary.json"
    complex_path = EVIDENCE_ROOT / "complex-v2-explicit-summary/summary.json"
    if not performance_path.is_file() or not complex_path.is_file():
        raise RuntimeError("performance and complex phases must complete before finalize")
    performance_result = json.loads(performance_path.read_text(encoding="utf-8"))
    complex_result = json.loads(complex_path.read_text(encoding="utf-8"))
    compact_manifest = {
        "schema_version": 1,
        "created_at": manifest["created_at"],
        "roles": manifest["roles"],
        "runtime": manifest["runtime"],
        "inputs": manifest["inputs"],
        "archived_complex_tree_digests": manifest["archived_complex_tree_digests"],
        "transforms": manifest["transforms"],
        "commands": manifest["commands"],
        "orders": manifest["orders"],
        "automatic_tp_cleanup": "excluded",
        "raw_evidence_root": str(EVIDENCE_ROOT),
        "raw_summary_sha256": {
            "performance": sha256(performance_path),
            "complex_default_off": sha256(EVIDENCE_ROOT / "complex/summary.json"),
            "complex_explicit_summary": sha256(complex_path),
        },
    }
    def compact_semantics(comparisons: list[dict]) -> dict:
        controlled = {}
        for comparison in comparisons:
            for item in comparison["controlled_differences"]:
                key = (item["path"], item["left_sha256"], item["right_sha256"])
                controlled[key] = item
        return {
            "comparison_count": len(comparisons),
            "controlled_differences": sorted(controlled.values(), key=lambda item: item["path"]),
            "unexplained_differences": sorted(
                {path for item in comparisons for path in item["unexplained_differences"]}
            ),
            "left_only": sorted({path for item in comparisons for path in item["left_only"]}),
            "right_only": sorted({path for item in comparisons for path in item["right_only"]}),
        }

    def compact_performance_case(case: dict) -> dict:
        return {
            "roles": case["roles"],
            "paired_rounds": case["paired_rounds"],
            "gates": case["gates"],
            "semantic": compact_semantics(case["semantic_comparisons"]),
            "semantic_passed": case["semantic_passed"],
            "performance_passed": case["performance_passed"],
        }

    compact_complex_cases = {}
    complex_root = EVIDENCE_ROOT / "complex-v2-explicit-summary"
    exception_causes = {
        "rtp-shared-win1": "T05 same-timestamp TaskTrace order only",
        "rtp-gbn-double-drop": "T06 cumulative-ACK descriptor-prefix retirement",
        "rtp-tpg-gbn": "T14 resolved-generation TPG attribution and pinning",
    }
    for case_name, case in complex_result["cases"].items():
        role_hashes = {
            role: artifact_hashes(complex_root / case_name / role / "case-input", shared_only=True)
            for role in ("next", "current")
        }
        changed_artifacts = [
            {
                "path": path,
                "next_sha256": role_hashes["next"][path],
                "current_sha256": role_hashes["current"][path],
            }
            for path in case["shared"]["changed"]
        ]
        task_observations = {
            role: [
                {
                    key: row.get(key)
                    for key in (
                        "taskId",
                        "firstPacketSends(us)",
                        "lastPacketACKs(us)",
                        "taskCompletesTime(us)",
                        "taskThroughput(Gbps)",
                    )
                }
                for row in rows
            ]
            for role, rows in case["tasks"].items()
        }
        compact_complex_cases[case_name] = {
            "classification": case["classification"],
            "accepted": case["accepted"],
            "approved_cause": exception_causes.get(case_name),
            "changed_artifacts": changed_artifacts,
            "archived_replay_exact": {
                role: case["archived_replay"][role]["exact"] for role in ("next", "current")
            },
            "task_ids": case["task_ids"],
            "task_observations": task_observations,
            "current_lifecycle": case["current_lifecycle"],
        }

    compact_summary = {
        "schema_version": 1,
        "status": "passed"
        if performance_result["status"] == "passed" and complex_result["status"] == "passed"
        else "failed",
        "performance_status": performance_result["status"],
        "semantic_status": "passed"
        if performance_result["semantic_passed"] and complex_result["status"] == "passed"
        else "failed",
        "p01": compact_performance_case(performance_result["p01"]),
        "p02": compact_performance_case(performance_result["p02"]),
        "p02_summary": {
            "modes": performance_result["p02_summary"]["modes"],
            "paired_rounds": performance_result["p02_summary"]["paired_rounds"],
            "median_paired_wall_delta_percent": performance_result["p02_summary"][
                "median_paired_wall_delta_percent"
            ],
            "median_paired_rss_delta_percent": performance_result["p02_summary"][
                "median_paired_rss_delta_percent"
            ],
            "semantic": compact_semantics(
                performance_result["p02_summary"]["semantic_comparisons"]
            ),
            "semantic_passed": performance_result["p02_summary"]["semantic_passed"],
        },
        "complex": {
            "status": complex_result["status"],
            "ctp_window_effect": complex_result["ctp_window_effect"],
            "ctp_non_degenerate_and_matched": complex_result[
                "ctp_non_degenerate_and_matched"
            ],
            "cases": compact_complex_cases,
        },
        "automatic_tp_cleanup": "excluded",
    }
    (HERE / "T28-manifest.json").write_text(
        json.dumps(compact_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (HERE / "T28-summary.json").write_text(
        json.dumps(compact_summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps({"status": compact_summary["status"], "semantic": compact_summary["semantic_status"]}, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("phase", choices=("freeze", "performance", "complex", "finalize"))
    args = parser.parse_args()
    if args.phase == "freeze":
        freeze()
    elif args.phase == "performance":
        performance()
    elif args.phase == "complex":
        complex_matrix()
    else:
        finalize()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
