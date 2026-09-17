#!/usr/bin/env python3
"""Fail-closed verification and prediction-vs-actual analysis for T55."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import math
import re
import subprocess
import sys
from pathlib import Path

from run_t55 import (
    BUILD_ARTIFACTS,
    COMMON_ARTIFACTS,
    CONTROL_REVISION,
    INPUT_HASHES,
    PACKAGE_ROOT,
    RSS_LIMIT_BYTES,
    RUNTIME,
    TREATMENT_REVISION,
    WALL_LIMIT_SECONDS,
    render_run_command,
    source_tree_hash,
)


RESULTS = PACKAGE_ROOT / "results"
SNAPSHOT_HEADER = ["schema_version", "category", "metric", "resource_kind", "value"]
EXPECTED_SNAPSHOT = {
    "canonical_connections": 32,
    "resident_tp_endpoints": 64,
    "live_tp_backing_objects": 0,
    "dormant_tp_records": 64,
    "tp_backing_rehydrations": 64,
    "peak_live_tp_backing_objects": 64,
    "traffic_stored_tasks": 64,
    "traffic_completed_tasks": 64,
    "traffic_dependency_references": 0,
}
PREVIOUS_DERIVED_HASHES = {
    "comparison-summary.json": "b2257d18e2526be622396aa9f26cd93fb43cec2bb42ce041d6859469daab423a",
    "analysis.md": "59425ef00175cc744ab573e4a5a1cc4b131ea8870861c8ae7047b0f4c5bab954",
}
PREVIOUS_RAW_AGGREGATE_SHA256 = "e2c4b75ba3754174d8719b3d7fd8cd61340e0a78b0a35ac69de23ef2bbfb798f"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_artifact_hashes(root: Path, expected: dict[str, str]) -> None:
    for name, expected_hash in expected.items():
        artifact = root / name
        require(artifact.is_file() and sha256(artifact) == expected_hash,
                f"artifact hash drift: {name}")


def verify_case_inputs(case_dir: Path, *, require_frozen_hashes: bool = True) -> dict[str, str]:
    require(case_dir.is_dir(), f"case input directory missing: {case_dir}")
    names = sorted(path.name for path in case_dir.iterdir() if path.is_file())
    require(names == sorted(INPUT_HASHES), "case input file set drift")
    hashes = {name: sha256(case_dir / name) for name in names}
    if require_frozen_hashes:
        require(hashes == INPUT_HASHES, "case input hash drift")

    with (case_dir / "traffic.csv").open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
    expected_header = [
        "taskId", "sourceNodeId", "destNodeId", "dataSize(Byte)", "opType", "priority",
        "delay", "phaseId", "dependOnPhases",
    ]
    require(reader.fieldnames == expected_header, "traffic header drift")
    require(len(rows) == 64, f"traffic task count is {len(rows)}, expected 64")
    require([int(row["taskId"]) for row in rows] == list(range(64)), "traffic task IDs drift")
    phase_zero = rows[:32]
    phase_one = rows[32:]
    require(all(row["phaseId"] == "0" and row["dependOnPhases"] == ""
                for row in phase_zero), "phase 0 dependency shape drift")
    require(all(row["phaseId"] == "1" and row["dependOnPhases"] == "0"
                for row in phase_one), "phase 1 dependency shape drift")
    identity_fields = (
        "sourceNodeId", "destNodeId", "dataSize(Byte)", "opType", "priority",
    )
    for first, second in zip(phase_zero, phase_one, strict=True):
        require(all(first[field] == second[field] for field in identity_fields),
                f"phase pair identity mismatch: tasks {first['taskId']}/{second['taskId']}")
    return hashes


def parse_time_l(path: Path) -> tuple[float, int]:
    text = path.read_text(encoding="utf-8", errors="replace")
    real = re.search(r"([0-9.]+)\s+real", text)
    peak = re.search(r"(?m)^\s*(\d+)\s+maximum resident set size\s*$", text)
    require(real is not None and peak is not None, f"unparseable time -l: {path}")
    return float(real.group(1)), int(peak.group(1))


def parse_canonical(path: Path) -> dict[str, object]:
    raw = path.read_bytes()
    require(raw.endswith(b"\n"), "canonical output lacks terminal newline")
    lines = raw.decode("utf-8").splitlines()
    require(len(lines) == 128, f"canonical event count is {len(lines)}, expected 128")
    require(lines == sorted(lines), "canonical output is not normalized")
    events: dict[int, set[str]] = {}
    for line in lines:
        fields = line.split(",")
        require(len(fields) == 5, "canonical row width mismatch")
        require(fields[0].isdigit() and fields[2].isdigit() and fields[3].isdigit()
                and fields[4].isdigit(), "canonical numeric field mismatch")
        task_id = int(fields[2])
        require(0 <= task_id < 64, "canonical task id out of range")
        events.setdefault(task_id, set()).add(fields[1])
    require(set(events) == set(range(64)), "canonical task set mismatch")
    require(all(value == {"START", "COMPLETE_VISIBLE"} for value in events.values()),
            "canonical event types mismatch")
    return {"sha256": hashlib.sha256(raw).hexdigest(), "event_count": len(lines)}


def verify_phase_ordering(canonical_path: Path, case_dir: Path) -> dict[str, int]:
    with (case_dir / "traffic.csv").open(newline="", encoding="utf-8") as stream:
        traffic = list(csv.DictReader(stream))
    phases = {int(row["taskId"]): int(row["phaseId"]) for row in traffic}
    require(len(phases) == 64, "phase ordering task map mismatch")
    event_times: dict[tuple[int, str], int] = {}
    for line in canonical_path.read_text(encoding="utf-8").splitlines():
        fields = line.split(",")
        require(len(fields) == 5 and fields[0].isdigit() and fields[2].isdigit(),
                "phase ordering canonical row mismatch")
        event_times[(int(fields[2]), fields[1])] = int(fields[0])
    phase_zero_complete = [
        event_times[(task_id, "COMPLETE_VISIBLE")]
        for task_id, phase_id in phases.items() if phase_id == 0
    ]
    phase_one_start = [
        event_times[(task_id, "START")]
        for task_id, phase_id in phases.items() if phase_id == 1
    ]
    require(len(phase_zero_complete) == 32 and len(phase_one_start) == 32,
            "phase ordering task count mismatch")
    latest_phase_zero_complete = max(phase_zero_complete)
    earliest_phase_one_start = min(phase_one_start)
    require(earliest_phase_one_start > latest_phase_zero_complete,
            "phase ordering violated: phase 1 must start after all phase 0 completion is visible")
    return {
        "latest_phase_zero_complete_time_steps": latest_phase_zero_complete,
        "earliest_phase_one_start_time_steps": earliest_phase_one_start,
    }


def parse_snapshot(path: Path) -> dict[str, int]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.reader(stream))
    require(rows and rows[0] == SNAPSHOT_HEADER, "snapshot header mismatch")
    require(len(rows) > 1 and all(len(row) == 5 for row in rows[1:]),
            "snapshot row width mismatch")
    records = [dict(zip(SNAPSHOT_HEADER, row)) for row in rows[1:]]
    require(all(row["schema_version"] == "1" for row in records),
            "snapshot schema version mismatch")
    identities = [(row["category"], row["metric"], row["resource_kind"]) for row in records]
    require(len(identities) == len(set(identities)), "duplicate snapshot metric identity")
    require(all(re.fullmatch(r"\d+", row["value"]) is not None for row in records),
            "snapshot contains non-numeric value")
    metrics = {
        row["metric"]: int(row["value"])
        for row in records if row["category"] == "runtime"
    }
    traffic = {
        row["metric"]: int(row["value"])
        for row in records if row["category"] == "traffic"
    }
    required = {
        "canonical_connections", "resident_tp_endpoints", "live_tp_backing_objects",
        "dormant_tp_records", "peak_live_tp_backing_objects", "tp_backing_rehydrations",
    }
    require(required <= metrics.keys(), "required runtime snapshot metric missing")
    require({"stored_tasks", "completed_tasks", "dependency_references"} <= traffic.keys(),
            "required traffic snapshot metric missing")
    snapshot = {**metrics, **{f"traffic_{key}": value for key, value in traffic.items()}}
    require(all(snapshot[key] == value for key, value in EXPECTED_SNAPSHOT.items()),
            "snapshot exact witness mismatch")
    return snapshot


def expected_command(run_dir: Path, binary: str, run_id: str) -> list[str]:
    return render_run_command(run_id, run_dir, Path(binary))


def verify_provenance() -> tuple[dict[str, object], dict[str, object]]:
    path = RESULTS / "build-provenance.json"
    require(path.is_file(), "build provenance missing")
    data = json.loads(path.read_text(encoding="utf-8"))
    require(data.get("schema_version") == 1, "build provenance schema mismatch")
    live: dict[str, object] = {}
    for variant, revision in (("before", CONTROL_REVISION), ("after", TREATMENT_REVISION)):
        entry = data[variant]
        require(entry["source_revision"] == revision, f"{variant} revision drift")
        require(entry["build_shape"] == {
            "build_type": "release", "generator": "Ninja", "mtp": True, "mpi": False,
        }, f"{variant} build shape mismatch")
        worktree = Path(entry["worktree"])
        live_entry: dict[str, object] = {"worktree_available": worktree.is_dir(), "artifacts": {}}
        if worktree.is_dir():
            head = subprocess.run(
                ["git", "-C", str(worktree), "rev-parse", "HEAD"], check=True,
                capture_output=True, text=True,
            ).stdout.strip()
            live_tree_hash, sources = source_tree_hash(worktree)
            live_entry.update({
                "head": head,
                "head_matches": head == revision,
                "source_tree_sha256": live_tree_hash,
                "source_tree_matches": live_tree_hash == entry["source_tree_sha256"],
                "source_file_count": len(sources),
                "source_file_count_matches": len(sources) == entry["source_file_count"],
            })
        for name, relative in BUILD_ARTIFACTS.items():
            artifact = entry["artifacts"][name]
            artifact_path = Path(artifact["path"])
            require(artifact_path == worktree / relative, f"{variant} artifact path drift: {name}")
            require(re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]) is not None,
                    f"{variant} artifact hash schema mismatch: {name}")
            require(isinstance(artifact["mtime_ns"], int) and artifact["mtime_ns"] > 0,
                    f"{variant} artifact timestamp schema mismatch: {name}")
            observed = {"available": artifact_path.is_file()}
            if artifact_path.is_file():
                observed.update({
                    "sha256": sha256(artifact_path),
                    "sha256_matches": sha256(artifact_path) == artifact["sha256"],
                    "mtime_ns": artifact_path.stat().st_mtime_ns,
                    "mtime_matches": artifact_path.stat().st_mtime_ns == artifact["mtime_ns"],
                })
            live_entry["artifacts"][name] = observed  # type: ignore[index]
        live[variant] = live_entry
    return data, live


def verify_run(run_id: str, provenance: dict[str, object]) -> dict[str, object]:
    run_dir = RESULTS / run_id
    require(run_dir.is_dir(), f"run directory missing: {run_id}")
    files = {path.name for path in run_dir.iterdir() if path.is_file()}
    require(COMMON_ARTIFACTS <= files, f"artifact set incomplete: {run_id}")
    rank_files = sorted(run_dir.glob("canonical.rank*.txt"))
    require(rank_files, f"canonical rank artifact missing: {run_id}")
    manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
    require(manifest["schema_version"] == 1 and manifest["run_id"] == run_id,
            f"manifest identity mismatch: {run_id}")
    require(manifest["source_revision"] == provenance["source_revision"]
            and manifest["source_tree_sha256"] == provenance["source_tree_sha256"]
            and manifest["tracked_diff_sha256"] == provenance["tracked_diff_sha256"],
            f"source provenance mismatch: {run_id}")
    require(manifest["build_artifacts"] == provenance["artifacts"],
            f"build provenance mismatch: {run_id}")
    case_hashes = verify_case_inputs(run_dir / "case")
    require(manifest["input_sha256"] == case_hashes == INPUT_HASHES,
            f"input manifest drift: {run_id}")
    exit_text = (run_dir / "exit-code.txt").read_text(encoding="ascii")
    require(re.fullmatch(r"-?\d+\n?", exit_text) is not None, "exit-code schema mismatch")
    require(int(exit_text) == manifest["return_code"] == 0, f"run failed: {run_id}")
    require(manifest["stop_reason"] == "completed" and manifest["termination"] == "not_requested"
            and manifest["wait_completed"] is True and manifest["failure"] is None,
            f"run termination mismatch: {run_id}")
    require(manifest["rss_limit_bytes"] == RSS_LIMIT_BYTES
            and manifest["wall_limit_seconds"] == WALL_LIMIT_SECONDS
            and manifest["runtime"] == RUNTIME, f"guard/runtime drift: {run_id}")
    expected = expected_command(run_dir, provenance["artifacts"]["binary"]["path"], run_id)
    command_artifact = json.loads((run_dir / "command.json").read_text(encoding="utf-8"))
    require(command_artifact == manifest["command"] == expected, f"command drift: {run_id}")
    try:
        verify_artifact_hashes(run_dir, manifest["artifact_sha256"])
    except RuntimeError as error:
        raise RuntimeError(f"{run_id}: {error}") from error

    with (run_dir / "rss-samples.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.reader(stream))
    require(rows and rows[0] == ["elapsed_seconds", "process_group_rss_bytes", "process_count"]
            and len(rows) > 1, f"RSS schema mismatch: {run_id}")
    require(all(len(row) == 3 for row in rows[1:]), f"RSS row width mismatch: {run_id}")
    elapsed = [float(row[0]) for row in rows[1:]]
    rss = [int(row[1]) for row in rows[1:]]
    counts = [int(row[2]) for row in rows[1:]]
    require(all(math.isfinite(value) for value in elapsed) and elapsed == sorted(elapsed),
            f"RSS time order mismatch: {run_id}")
    require(all(value >= 0 for value in rss + counts), f"negative RSS data: {run_id}")
    require(max(rss) == manifest["sampled_peak_process_group_rss_bytes"],
            f"RSS peak mismatch: {run_id}")
    require(max(rss) <= RSS_LIMIT_BYTES and manifest["elapsed_seconds"] <= WALL_LIMIT_SECONDS + 10,
            f"guard bound violated: {run_id}")
    wall, peak = parse_time_l(run_dir / "time-l.txt")
    require(wall == manifest["time_l_wall_seconds"] and peak == manifest["time_l_peak_rss_bytes"],
            f"time -l manifest drift: {run_id}")
    canonical = parse_canonical(run_dir / "canonical-output.csv")
    require(canonical["sha256"] == manifest["canonical_sha256"],
            f"canonical manifest drift: {run_id}")
    phase_ordering = verify_phase_ordering(run_dir / "canonical-output.csv", run_dir / "case")
    output = (run_dir / "stdout.txt").read_text(encoding="utf-8")
    errors = output + (run_dir / "stderr.txt").read_text(encoding="utf-8")
    require(output.count("[run] Simulation finished.") == 1
            and output.count("[summary] Program finished.") == 1,
            f"terminal output mismatch: {run_id}")
    require("[traffic] Scheduled local tasks: 64" in output, f"task count output mismatch: {run_id}")
    require(not re.search(r"\[(?:ERROR|FATAL)\]|NS_FATAL|Assertion", errors),
            f"fatal output present: {run_id}")
    return {
        "wall_seconds": wall, "peak_rss_bytes": peak,
        "sampled_peak_process_group_rss_bytes": max(rss), **canonical, **phase_ordering,
    }


def classify_treatment(run_ok: bool, canonical_equal: bool, snapshot_ok: bool,
                       snapshot: dict[str, int] | None) -> str:
    if not run_ok:
        return "inconclusive"
    if not canonical_equal:
        return "mismatched"
    if not snapshot_ok or snapshot is None:
        return "partially_matched"
    if any(snapshot.get(key) != value for key, value in EXPECTED_SNAPSHOT.items()):
        return "mismatched"
    return "matched"


def aggregate_raw_evidence_hash() -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in RESULTS.rglob("*") if item.is_file()):
        relative = path.relative_to(RESULTS).as_posix()
        digest.update(relative.encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def replace_text(path: Path, value: str) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    require(not temporary.exists(), f"stale derived temporary: {temporary}")
    temporary.write_text(value, encoding="utf-8")
    temporary.replace(path)


def write_outputs(summary: dict[str, object], *, regenerate: bool) -> None:
    summary_path = PACKAGE_ROOT / "comparison-summary.json"
    analysis_path = PACKAGE_ROOT / "analysis.md"
    provenance_path = PACKAGE_ROOT / "derived-regeneration-provenance.json"
    existing = {path.name: sha256(path) for path in (summary_path, analysis_path) if path.is_file()}
    prior_regeneration: dict[str, object] | None = None
    original_derived_hashes = PREVIOUS_DERIVED_HASHES
    if existing:
        require(regenerate, "analysis outputs already exist; use --regenerate-derived")
        if provenance_path.is_file():
            prior_regeneration = json.loads(provenance_path.read_text(encoding="utf-8"))
            require(existing == prior_regeneration["generated_derived_sha256"],
                    "existing derived artifact hash drift")
            original_derived_hashes = prior_regeneration["previous_derived_sha256"]  # type: ignore[assignment]
        else:
            require(existing == PREVIOUS_DERIVED_HASHES, "existing derived artifact hash drift")
    else:
        require(not regenerate, "cannot regenerate missing derived artifacts")

    summary_text = json.dumps(summary, indent=2, sort_keys=True) + "\n"
    rows = summary["rows"]
    treatment = rows["t55-after"]
    snapshot = treatment.get("snapshot") or {}
    report = f"""# T55 MTP16 Owner Cleanup Analysis

## Prediction vs Actual

| row | role | classification | wall | peak RSS | canonical |
| --- | --- | --- | ---: | ---: | --- |
| t55-before | control | {rows['t55-before']['classification']} | {rows['t55-before'].get('wall_seconds', 'n/a')} s | {rows['t55-before'].get('peak_rss_bytes', 'n/a')} B | {rows['t55-before'].get('sha256', 'n/a')} |
| t55-after | treatment | {treatment['classification']} | {treatment.get('wall_seconds', 'n/a')} s | {treatment.get('peak_rss_bytes', 'n/a')} B | {treatment.get('sha256', 'n/a')} |

Canonical byte equality: `{summary['canonical_byte_identical']}`.

Dependency phase ordering (both rows): latest phase-0 `COMPLETE_VISIBLE` =
`{rows['t55-before'].get('latest_phase_zero_complete_time_steps', 'n/a')}`, earliest phase-1
`START` = `{rows['t55-before'].get('earliest_phase_one_start_time_steps', 'n/a')}` time steps.

## Treatment Storage Witness

- Resident endpoints: {snapshot.get('resident_tp_endpoints', 'n/a')}
- Final live backing: {snapshot.get('live_tp_backing_objects', 'n/a')}
- Final compact records: {snapshot.get('dormant_tp_records', 'n/a')}
- Peak live backing: {snapshot.get('peak_live_tp_backing_objects', 'n/a')}
- Successful backing restorations: {snapshot.get('tp_backing_rehydrations', 'n/a')}
- Completed/stored traffic tasks: {snapshot.get('traffic_completed_tasks', 'n/a')}/{snapshot.get('traffic_stored_tasks', 'n/a')}
- Remaining dependency references: {snapshot.get('traffic_dependency_references', 'n/a')}

## Evidence Boundary

Compact records and restoration counts are simulator-emitted, run-derived aggregate evidence.
Canonical output proves phase 1 starts strictly after every phase-0 completion is visible. The input
proves phase 1 reuses the same endpoint identity keys. The snapshot reports 64 aggregate backing
restorations across 64 resident endpoints; per-identity restoration counts are unavailable. Fine-grained
`COMPLETE_VISIBLE -> cleanup` ordering, wrong-owner rejection, public-LP rejection, and missing-owner
behavior remain focused-test-derived from `unified-bus-lifecycle`,
`unified-bus-parallel-lifecycle`, and `unified-bus-tp-reservation`; they are not promoted to
run-derived evidence.
"""
    if regenerate:
        replace_text(summary_path, summary_text)
        replace_text(analysis_path, report)
    else:
        summary_path.write_text(summary_text, encoding="utf-8")
        analysis_path.write_text(report, encoding="utf-8")
    provenance = {
        "schema_version": 1,
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "operation": "regenerate-derived" if regenerate else "generate-derived",
        "raw_evidence_write_policy": "read_only",
        "previously_recorded_raw_evidence_aggregate_sha256": PREVIOUS_RAW_AGGREGATE_SHA256,
        "observed_raw_evidence_aggregate_sha256": aggregate_raw_evidence_hash(),
        "observed_raw_evidence_aggregate_method": "sha256(sorted relative path + NUL + bytes)",
        "raw_aggregate_comparison": "not comparable because the earlier method was not recorded",
        "previous_derived_sha256": original_derived_hashes,
        "superseded_derived_sha256": existing if prior_regeneration is not None else None,
        "generated_derived_sha256": {
            summary_path.name: sha256(summary_path),
            analysis_path.name: sha256(analysis_path),
        },
        "command": "python3.12 scripts/analyze_t55.py --regenerate-derived",
    }
    replace_text(provenance_path, json.dumps(provenance, indent=2, sort_keys=True) + "\n")


def build_summary() -> dict[str, object]:
    require(RESULTS.is_dir(), "results root missing")
    provenance, live_provenance = verify_provenance()
    rows: dict[str, dict[str, object]] = {}
    control_ok = treatment_ok = False
    try:
        rows["t55-before"] = verify_run("t55-before", provenance["before"])
        rows["t55-before"]["classification"] = "matched"
        control_ok = True
    except (RuntimeError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        rows["t55-before"] = {"classification": "inconclusive", "error": str(error)}
    try:
        rows["t55-after"] = verify_run("t55-after", provenance["after"])
        treatment_ok = True
    except (RuntimeError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        rows["t55-after"] = {"error": str(error)}
    canonical_equal = bool(
        control_ok and treatment_ok and rows["t55-before"]["sha256"] == rows["t55-after"]["sha256"]
    )
    snapshot: dict[str, int] | None = None
    snapshot_ok = False
    if treatment_ok:
        try:
            snapshot = parse_snapshot(RESULTS / "t55-after/runtime-storage-snapshot.csv")
            snapshot_ok = True
            rows["t55-after"]["snapshot"] = snapshot
        except (RuntimeError, OSError, ValueError) as error:
            rows["t55-after"]["snapshot_error"] = str(error)
    rows["t55-after"]["classification"] = classify_treatment(
        treatment_ok, canonical_equal, snapshot_ok, snapshot
    )
    summary = {
        "schema_version": 1,
        "experiment_id": "t55-mtp-owner-cleanup",
        "canonical_byte_identical": canonical_equal,
        "rows": rows,
        "wrong_owner_evidence_source": "test-derived: unified-bus-tp-reservation",
        "completion_cleanup_ordering_evidence_source": (
            "test-derived: unified-bus-lifecycle and unified-bus-parallel-lifecycle"
        ),
        "restoration_evidence_scope": (
            "64 aggregate restorations across 64 resident endpoints; per-identity counts unavailable"
        ),
        "live_provenance_reverification": live_provenance,
        "formal_gate": control_ok and rows["t55-after"]["classification"] == "matched",
    }
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--verify-only", action="store_true")
    mode.add_argument("--regenerate-derived", action="store_true")
    args = parser.parse_args()
    summary = build_summary()
    if args.regenerate_derived:
        write_outputs(summary, regenerate=True)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["formal_gate"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
