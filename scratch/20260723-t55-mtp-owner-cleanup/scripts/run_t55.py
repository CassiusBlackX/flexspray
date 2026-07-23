#!/usr/bin/env python3
"""Validate and execute the frozen T55 MTP16 A/B exactly once per row."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
CONTROL_REVISION = "e3682e593c144ab441dd7045aff5f6312efab86d"
TREATMENT_REVISION = "5965931f378430f3d4195ec7fab5487a4e01ce1d"
RUN_ORDER = ("t55-before", "t55-after")
RUNTIME = {
    "mtp_threads": 16,
    "rng_run": 1,
    "dependency_visibility_delay": "10ns",
    "initial_task_start_offset_window": "32ps",
    "timing_offset_seed": 1,
}
RSS_LIMIT_BYTES = 2_147_483_648
WALL_LIMIT_SECONDS = 120
SAMPLE_INTERVAL_SECONDS = 0.25
INPUT_HASHES = json.loads((PACKAGE_ROOT / "input-sha256.json").read_text(encoding="utf-8"))
COMMAND_MANIFEST = json.loads(
    (PACKAGE_ROOT / "command-manifest.yaml").read_text(encoding="utf-8")
)
MATRIX = json.loads((PACKAGE_ROOT / "matrix.yaml").read_text(encoding="utf-8"))
COMMON_ARTIFACTS = {
    "command.json", "manifest.json", "exit-code.txt", "time-l.txt", "rss-samples.csv",
    "stdout.txt", "stderr.txt", "canonical-output.csv",
}
BUILD_ARTIFACTS = {
    "binary": Path("build/scratch/ns3.44-ub-quick-example"),
    "unified_bus_library": Path("build/lib/libns3.44-unified-bus.dylib"),
    "mtp_library": Path("build/lib/libns3.44-mtp.dylib"),
    "cmake_cache": Path("cmake-cache/CMakeCache.txt"),
}


class ExperimentError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ExperimentError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_text_new(path: Path, value: str, encoding: str = "utf-8") -> None:
    require(not path.exists(), f"refusing to overwrite artifact: {path}")
    path.write_text(value, encoding=encoding)


def write_json_new(path: Path, value: object) -> None:
    write_text_new(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def git_output(worktree: Path, *args: str, text: bool = True) -> str | bytes:
    result = subprocess.run(
        ["git", "-C", str(worktree), *args], check=True, capture_output=True, text=text
    )
    return result.stdout.strip() if text else result.stdout


def source_tree_hash(worktree: Path) -> tuple[str, list[str]]:
    result = subprocess.run(
        ["git", "-C", str(worktree), "ls-files", "-z", "src/unified-bus", "src/mtp",
         "scratch/ub-quick-example.cc", "CMakeLists.txt"],
        check=True, capture_output=True,
    )
    listed = [entry.decode("utf-8") for entry in result.stdout.split(b"\0") if entry]
    digest = hashlib.sha256()
    for relative in sorted(listed):
        path = worktree / relative
        require(path.is_file(), f"tracked source is missing: {path}")
        digest.update(relative.encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest(), sorted(listed)


def validate_worktree(worktree: Path, revision: str, allow_tracked_changes: bool) -> dict[str, object]:
    require(worktree.is_dir(), f"worktree does not exist: {worktree}")
    observed_revision = str(git_output(worktree, "rev-parse", "HEAD"))
    require(observed_revision == revision, f"revision mismatch: {worktree}: {observed_revision}")
    status = str(git_output(worktree, "status", "--short", "--untracked-files=no"))
    if not allow_tracked_changes:
        require(not status, f"control has tracked changes: {status}")
    diff_bytes = bytes(git_output(worktree, "diff", "--binary", text=False))
    artifacts: dict[str, dict[str, object]] = {}
    newest_source_ns = 0
    tree_hash, source_files = source_tree_hash(worktree)
    for relative in source_files:
        if "/test/" not in relative and "/doc/" not in relative:
            newest_source_ns = max(newest_source_ns, (worktree / relative).stat().st_mtime_ns)
    for name, relative in BUILD_ARTIFACTS.items():
        path = worktree / relative
        require(path.is_file(), f"missing build artifact: {path}")
        if name == "binary":
            require(os.access(path, os.X_OK), f"binary is not executable: {path}")
        artifacts[name] = {
            "path": str(path), "sha256": sha256(path), "mtime_ns": path.stat().st_mtime_ns,
        }
    cache = (worktree / BUILD_ARTIFACTS["cmake_cache"]).read_text(
        encoding="utf-8", errors="replace"
    )
    require("CMAKE_BUILD_TYPE:STRING=release" in cache, "build is not Release")
    require("CMAKE_GENERATOR:INTERNAL=Ninja" in cache, "build generator is not Ninja")
    require("NS3_MTP:BOOL=ON" in cache, "MTP is disabled")
    require("NS3_MPI:BOOL=OFF" in cache, "MPI is enabled")
    require(int(artifacts["unified_bus_library"]["mtime_ns"]) >= newest_source_ns,
            f"unified-bus library is older than source in {worktree}")
    return {
        "worktree": str(worktree),
        "source_revision": revision,
        "tracked_status": status,
        "tracked_diff_sha256": hashlib.sha256(diff_bytes).hexdigest(),
        "source_tree_sha256": tree_hash,
        "source_file_count": len(source_files),
        "build_shape": {"build_type": "release", "generator": "Ninja", "mtp": True, "mpi": False},
        "artifacts": artifacts,
    }


def command_entry(command_id: str) -> dict[str, object]:
    matches = [entry for entry in COMMAND_MANIFEST["commands"] if entry["id"] == command_id]
    require(len(matches) == 1, f"command manifest entry missing or duplicated: {command_id}")
    return matches[0]


def validate_manifest_contract(
    manifest: dict[str, object], matrix_case_ids: list[str], case_spec_ids: set[str]
) -> None:
    commands = manifest.get("commands")
    require(isinstance(commands, list), "command manifest commands missing")
    allowed_keys = {
        "id", "case_id", "phase", "command", "cwd", "output_dir", "expected_artifacts",
    }
    for entry in commands:
        require(isinstance(entry, dict), "command entry is not an object")
        require(set(entry) == allowed_keys,
                f"unsupported command key or missing field: {entry.get('id')}")
        command = entry["command"]
        require(isinstance(command, list) and command
                and all(isinstance(value, str) for value in command),
                f"structured command missing: {entry.get('id')}")
    run_order = manifest.get("run_order")
    require(run_order == matrix_case_ids, "matrix case IDs differ from run order")
    require(set(matrix_case_ids) == case_spec_ids, "matrix case IDs differ from case specs")
    run_case_ids = [entry["case_id"] for entry in commands if entry["phase"] == "run"]
    require(run_case_ids == matrix_case_ids, "manifest run entries differ from matrix case IDs")


def validate_package() -> None:
    required = [
        "experiment-plan.md", "matrix.yaml", "command-manifest.yaml", "run-ledger.md",
        "input-sha256.json", "cases/t55-before/experiment-spec.md",
        "cases/t55-after/experiment-spec.md",
    ]
    for relative in required:
        require((PACKAGE_ROOT / relative).is_file(), f"missing package artifact: {relative}")
    require(COMMAND_MANIFEST["schema_version"] == 2, "command manifest schema drift")
    require(COMMAND_MANIFEST["control_revision"] == CONTROL_REVISION, "control revision drift")
    require(COMMAND_MANIFEST["treatment_revision"] == TREATMENT_REVISION,
            "treatment revision drift")
    require(COMMAND_MANIFEST["run_order"] == list(RUN_ORDER), "run order drift")
    require(COMMAND_MANIFEST["runtime"] == RUNTIME, "runtime manifest drift")
    require(COMMAND_MANIFEST["guards"] == {
        "rss_limit_bytes": RSS_LIMIT_BYTES,
        "wall_limit_seconds": WALL_LIMIT_SECONDS,
        "sample_interval_seconds": SAMPLE_INTERVAL_SECONDS,
    }, "guard manifest drift")
    require(COMMAND_MANIFEST["execution"] == {
        "serial": True,
        "repetitions": 1,
        "checkpoint_policy": "continue_full_matrix",
        "manual_approval_gate": False,
        "overwrite_policy": "refuse_existing_results_root",
        "failure_behavior": "fail_closed_stop_before_next_run",
    }, "execution manifest drift")
    require(set(COMMAND_MANIFEST["artifact_contract"]) == COMMON_ARTIFACTS,
            "artifact contract drift")
    expected_commands = {
        "generate-before", "generate-after", "build-before", "build-after", "validate",
        "run-before", "run-after", "analyze",
    }
    require({entry["id"] for entry in COMMAND_MANIFEST["commands"]} == expected_commands,
            "command set drift")
    require(MATRIX.get("schema_version") == 1, "matrix schema drift")
    matrix_rows = MATRIX.get("rows")
    require(isinstance(matrix_rows, list), "matrix rows missing")
    matrix_case_ids = [row["case_id"] for row in matrix_rows]
    case_spec_ids = {
        path.parent.name for path in (PACKAGE_ROOT / "cases").glob("*/experiment-spec.md")
    }
    validate_manifest_contract(COMMAND_MANIFEST, matrix_case_ids, case_spec_ids)


def render_run_command(run_id: str, run_dir: Path, binary: Path) -> list[str]:
    entry = command_entry("run-before" if run_id == "t55-before" else "run-after")
    template = entry.get("command")
    require(isinstance(template, list) and all(isinstance(value, str) for value in template),
            f"run command template missing: {run_id}")
    replacements = {"run_dir": str(run_dir), "binary": str(binary)}
    return [value.format(**replacements) for value in template]


def require_same_provenance(observed: dict[str, object], frozen: dict[str, object]) -> None:
    for field in (
        "source_revision", "tracked_status", "tracked_diff_sha256", "source_tree_sha256",
        "source_file_count", "build_shape", "artifacts",
    ):
        require(observed[field] == frozen[field], f"pre-exec provenance drift: {field}")


def validate_inputs() -> None:
    expected_names = sorted(INPUT_HASHES)
    observed: list[dict[str, str]] = []
    for run_id in RUN_ORDER:
        root = PACKAGE_ROOT / "cases" / run_id / "input"
        require(root.is_dir(), f"missing input directory: {root}")
        names = sorted(path.name for path in root.iterdir() if path.is_file())
        require(names == expected_names, f"input file set drift: {run_id}")
        require(not any(path.is_dir() for path in root.iterdir()), f"input has directory: {run_id}")
        hashes = {name: sha256(root / name) for name in names}
        require(hashes == INPUT_HASHES, f"input hash drift: {run_id}")
        require(not (root / "transport_channel.csv").exists(), "on-demand case has transport CSV")
        observed.append(hashes)
    require(observed[0] == observed[1], "control and treatment inputs differ")


def process_group_rss_bytes(pgid: int) -> tuple[int, int]:
    output = subprocess.run(
        ["ps", "-axo", "pid=,pgid=,rss="], check=True, capture_output=True, text=True
    ).stdout
    rss_kib = 0
    count = 0
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3 and int(fields[1]) == pgid:
            count += 1
            rss_kib += int(fields[2])
    return rss_kib * 1024, count


def wait_group_exit(pgid: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process_group_rss_bytes(pgid)[1] == 0:
            return True
        time.sleep(0.05)
    return process_group_rss_bytes(pgid)[1] == 0


def terminate_group(process: subprocess.Popen[bytes]) -> str:
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5.0)
        leader_reaped = True
    except subprocess.TimeoutExpired:
        leader_reaped = False
    if leader_reaped and wait_group_exit(process.pid, 5.0):
        return "sigterm_reaped"
    if process_group_rss_bytes(process.pid)[1] > 0:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    if not leader_reaped:
        process.wait(timeout=5.0)
    require(wait_group_exit(process.pid, 5.0), "process group survived SIGKILL")
    return "sigkill_reaped"


def monitor_process(
    process: object,
    *,
    sample_group=process_group_rss_bytes,
    record_sample,
    terminate=terminate_group,
    clock=time.monotonic,
    sleep=time.sleep,
) -> dict[str, object]:
    """Monitor one new-session child and always reap it after any post-spawn failure."""
    start = clock()
    peak_rss = 0
    stop_reason = "completed"
    termination = "not_requested"
    return_code: int | None = None
    try:
        while True:
            elapsed = clock() - start
            rss, count = sample_group(process.pid)  # type: ignore[attr-defined]
            peak_rss = max(peak_rss, rss)
            record_sample(elapsed, rss, count)
            if rss > RSS_LIMIT_BYTES:
                stop_reason = "rss_limit"
                termination = terminate(process)
                break
            if elapsed > WALL_LIMIT_SECONDS:
                stop_reason = "wall_limit"
                termination = terminate(process)
                break
            if process.poll() is not None and count == 0:  # type: ignore[attr-defined]
                break
            sleep(SAMPLE_INTERVAL_SECONDS)
    except BaseException:
        if process.poll() is None:  # type: ignore[attr-defined]
            termination = terminate(process)
        raise
    finally:
        if stop_reason == "completed" and process.poll() is None:  # type: ignore[attr-defined]
            termination = terminate(process)
        return_code = process.wait(timeout=5.0)  # type: ignore[attr-defined]
    return {
        "elapsed_seconds": clock() - start,
        "return_code": return_code,
        "stop_reason": stop_reason,
        "termination": termination,
        "sampled_peak_process_group_rss_bytes": peak_rss,
    }


def normalize_canonical(run_dir: Path, base: Path) -> str:
    files = sorted(base.parent.glob(base.name + ".rank*.txt"))
    require(files, "canonical rank output is missing")
    lines: list[str] = []
    for path in files:
        lines.extend(line for line in path.read_text(encoding="utf-8").splitlines() if line)
    lines.sort()
    require(len(lines) == 128, f"expected 128 canonical events, found {len(lines)}")
    events: dict[int, set[str]] = {}
    for line in lines:
        fields = line.split(",")
        require(len(fields) == 5 and fields[0].isdigit() and fields[2].isdigit(),
                "invalid canonical row")
        events.setdefault(int(fields[2]), set()).add(fields[1])
    require(set(events) == set(range(64)), "canonical task set mismatch")
    require(all(value == {"START", "COMPLETE_VISIBLE"} for value in events.values()),
            "canonical task event set mismatch")
    output = "\n".join(lines) + "\n"
    write_text_new(run_dir / "canonical-output.csv", output)
    return sha256(run_dir / "canonical-output.csv")


def parse_time_l(path: Path) -> dict[str, float | int | None]:
    text = path.read_text(encoding="utf-8", errors="replace")
    real = re.search(r"([0-9.]+)\s+real", text)
    peak = re.search(r"(?m)^\s*(\d+)\s+maximum resident set size\s*$", text)
    return {
        "time_l_wall_seconds": float(real.group(1)) if real else None,
        "time_l_peak_rss_bytes": int(peak.group(1)) if peak else None,
    }


def update_ledger(run_id: str, manifest: dict[str, object]) -> None:
    path = PACKAGE_ROOT / "run-ledger.md"
    text = path.read_text(encoding="utf-8")
    status = "success" if manifest["failure"] is None else "failed"
    canonical = str(manifest.get("canonical_sha256") or "unavailable")[:12]
    snapshot = "recorded" if run_id == "t55-after" and manifest["failure"] is None else (
        "not supported" if run_id == "t55-before" else "unavailable"
    )
    wall = manifest.get("time_l_wall_seconds") or manifest["elapsed_seconds"]
    peak = manifest.get("time_l_peak_rss_bytes") or manifest["sampled_peak_process_group_rss_bytes"]
    revision = "e3682e5" if run_id == "t55-before" else "5965931+patch"
    role = "control" if run_id == "t55-before" else "treatment"
    row = (f"| {run_id} | {role} | {revision} | {status} | {manifest['return_code']} | "
           f"{float(wall):.3f}s | {peak} | {canonical} | {snapshot} |")
    updated, count = re.subn(rf"(?m)^\| {run_id} \|.*$", row, text)
    require(count == 1, f"ledger row missing: {run_id}")
    package_status = "running" if run_id == "t55-before" and status == "success" else (
        "complete" if run_id == "t55-after" and status == "success" else "stopped"
    )
    updated, count = re.subn(r"(?m)^Status: (?:planned|running)$",
                             f"Status: {package_status}", updated)
    require(count == 1, "ledger status drift")
    temporary = path.with_suffix(".md.tmp")
    require(not temporary.exists(), "stale ledger temporary")
    temporary.write_text(updated, encoding="utf-8")
    temporary.replace(path)


def run_one(run_id: str, worktree: Path, provenance: dict[str, object], expected_hash: str | None) -> str:
    result_dir = PACKAGE_ROOT / "results" / run_id
    require(not result_dir.exists(), f"refusing to overwrite formal evidence: {result_dir}")
    result_dir.mkdir(parents=True)
    case_dir = result_dir / "case"
    shutil.copytree(PACKAGE_ROOT / "cases" / run_id / "input", case_dir)
    binary = Path(str(provenance["artifacts"]["binary"]["path"]))  # type: ignore[index]
    snapshot = result_dir / "runtime-storage-snapshot.csv"
    time_l = result_dir / "time-l.txt"
    canonical_base = result_dir / "canonical"
    command = render_run_command(run_id, result_dir, binary)
    write_json_new(result_dir / "command.json", command)
    started_at = dt.datetime.now(dt.timezone.utc).isoformat()
    stdout_path = result_dir / "stdout.txt"
    stderr_path = result_dir / "stderr.txt"
    with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr, (
        result_dir / "rss-samples.csv"
    ).open("x", newline="", encoding="utf-8") as samples_file:
        samples = csv.writer(samples_file)
        samples.writerow(["elapsed_seconds", "process_group_rss_bytes", "process_count"])
        observed = validate_worktree(
            worktree,
            CONTROL_REVISION if run_id == "t55-before" else TREATMENT_REVISION,
            run_id == "t55-after",
        )
        require_same_provenance(observed, provenance)
        process = subprocess.Popen(
            command, cwd=worktree, stdout=stdout, stderr=stderr, start_new_session=True
        )
        def record_sample(elapsed: float, rss: int, count: int) -> None:
            samples.writerow([f"{elapsed:.6f}", rss, count])
            samples_file.flush()

        outcome = monitor_process(process, record_sample=record_sample)
    elapsed = float(outcome["elapsed_seconds"])
    return_code = int(outcome["return_code"])
    stop_reason = str(outcome["stop_reason"])
    termination = str(outcome["termination"])
    peak_rss = int(outcome["sampled_peak_process_group_rss_bytes"])
    write_text_new(result_dir / "exit-code.txt", f"{return_code}\n", "ascii")
    failure: str | None = None
    canonical_hash: str | None = None
    try:
        require(stop_reason == "completed", f"guard stopped run: {stop_reason}")
        require(return_code == 0, f"run exited with status {return_code}")
        require(time_l.is_file() and time_l.stat().st_size > 0, "time -l output missing")
        canonical_hash = normalize_canonical(result_dir, canonical_base)
        if expected_hash is not None:
            require(canonical_hash == expected_hash,
                    f"canonical mismatch: {canonical_hash} != {expected_hash}")
        if run_id == "t55-after":
            require(snapshot.is_file() and snapshot.stat().st_size > 0, "snapshot missing")
        else:
            require(not snapshot.exists(), "control unexpectedly wrote snapshot")
        output = stdout_path.read_text(encoding="utf-8") + stderr_path.read_text(encoding="utf-8")
        require(not re.search(r"\[(?:ERROR|FATAL)\]|NS_FATAL|Assertion", output),
                "fatal semantic output detected")
    except ExperimentError as error:
        failure = str(error)
        if not (result_dir / "canonical-output.csv").exists():
            write_text_new(result_dir / "canonical-output.csv", "")
    timing = parse_time_l(time_l) if time_l.exists() else {}
    hashable_artifacts = [
        result_dir / "command.json", result_dir / "exit-code.txt", time_l,
        result_dir / "rss-samples.csv", stdout_path, stderr_path,
        result_dir / "canonical-output.csv",
        *sorted(result_dir.glob("canonical.rank*.txt")),
    ]
    if snapshot.exists():
        hashable_artifacts.append(snapshot)
    artifact_sha256 = {
        path.relative_to(result_dir).as_posix(): sha256(path)
        for path in hashable_artifacts if path.is_file()
    }
    manifest = {
        "schema_version": 1, "run_id": run_id,
        "variant": "before" if run_id == "t55-before" else "after",
        "source_revision": provenance["source_revision"],
        "source_tree_sha256": provenance["source_tree_sha256"],
        "tracked_diff_sha256": provenance["tracked_diff_sha256"],
        "build_artifacts": provenance["artifacts"], "input_sha256": INPUT_HASHES,
        "command": command, "runtime": RUNTIME, "started_at": started_at,
        "ended_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "elapsed_seconds": elapsed, "return_code": return_code, "stop_reason": stop_reason,
        "termination": termination, "wait_completed": True, "failure": failure,
        "failure_category": None if failure is None else "formal_run_validation",
        "retryable": False,
        "artifact_inventory": sorted(artifact_sha256),
        "rss_limit_bytes": RSS_LIMIT_BYTES, "wall_limit_seconds": WALL_LIMIT_SECONDS,
        "sample_interval_seconds": SAMPLE_INTERVAL_SECONDS,
        "sampled_peak_process_group_rss_bytes": peak_rss,
        "canonical_sha256": canonical_hash, "expected_canonical_sha256": expected_hash,
        "artifact_sha256": artifact_sha256,
        "platform": platform.platform(), **timing,
    }
    write_json_new(result_dir / "manifest.json", manifest)
    require(COMMON_ARTIFACTS <= {p.name for p in result_dir.iterdir() if p.is_file()},
            f"artifact contract incomplete: {run_id}")
    update_ledger(run_id, manifest)
    if failure is not None:
        raise ExperimentError(f"{run_id}: {failure}")
    return str(canonical_hash)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--before-worktree", required=True, type=Path)
    parser.add_argument("--after-worktree", required=True, type=Path)
    args = parser.parse_args()
    validate_package()
    validate_inputs()
    before = validate_worktree(args.before_worktree.resolve(), CONTROL_REVISION, False)
    after = validate_worktree(args.after_worktree.resolve(), TREATMENT_REVISION, True)
    require(not (PACKAGE_ROOT / "results").exists(), "formal results root already exists")
    if args.validate_only:
        print(json.dumps({"before": before, "after": after}, indent=2, sort_keys=True))
        return 0
    results = PACKAGE_ROOT / "results"
    results.mkdir()
    write_json_new(results / "build-provenance.json",
                   {"schema_version": 1, "before": before, "after": after})
    control_hash = run_one("t55-before", args.before_worktree.resolve(), before, None)
    current_after = validate_worktree(args.after_worktree.resolve(), TREATMENT_REVISION, True)
    require(current_after["source_tree_sha256"] == after["source_tree_sha256"],
            "treatment source changed after control run")
    require(current_after["tracked_diff_sha256"] == after["tracked_diff_sha256"],
            "treatment patch changed after control run")
    run_one("t55-after", args.after_worktree.resolve(), after, control_hash)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ExperimentError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
