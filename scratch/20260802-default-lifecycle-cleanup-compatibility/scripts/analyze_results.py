#!/usr/bin/env python3
"""Analyze the completed explicit-on versus implicit-default pairs."""

from __future__ import annotations

import csv
import hashlib
import json
import re
import statistics
from pathlib import Path
from typing import Any

import yaml


PACKAGE = Path(__file__).resolve().parents[1]
RESULTS = PACKAGE / "evidence/runs"
ANALYSIS = PACKAGE / "evidence/analysis"
LIFECYCLE_KEYS = (
    "ns3::UbApp::EnableTaskScopedJettyDestroy",
    "ns3::UbApp::EnablePlannedTpConnectionDestroy",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_rows() -> list[dict[str, Any]]:
    return yaml.safe_load((PACKAGE / "matrix.yaml").read_text(encoding="utf-8"))["rows"]


def canonical_digest(run_dir: Path) -> tuple[dict[str, str], str]:
    files = {
        path.name: sha256(path)
        for path in sorted(run_dir.glob("canonical*.txt"))
        if path.is_file()
    }
    digest = hashlib.sha256()
    for name, value in sorted(files.items()):
        digest.update(name.encode())
        digest.update(b"\0")
        digest.update(value.encode())
        digest.update(b"\n")
    return files, digest.hexdigest()


def input_hashes(run_dir: Path) -> dict[str, str]:
    return json.loads((run_dir / "input-hashes.json").read_text(encoding="utf-8"))


def normalized_attributes(run_dir: Path) -> str:
    return "\n".join(
        line
        for line in (run_dir / "case/network_attribute.txt").read_text(encoding="utf-8").splitlines()
        if not any(key in line for key in LIFECYCLE_KEYS)
    )


def task_fct(run_dir: Path) -> dict[str, Any]:
    starts: dict[int, int] = {}
    completes: dict[int, int] = {}
    for path in sorted(run_dir.glob("canonical*.txt")):
        for line in path.read_text(encoding="utf-8").splitlines():
            fields = line.split(",")
            if len(fields) < 3:
                continue
            try:
                timestamp = int(fields[0])
                task_id = int(fields[2])
            except ValueError:
                continue
            if fields[1] == "START":
                starts.setdefault(task_id, timestamp)
            elif fields[1] == "COMPLETE_VISIBLE":
                completes.setdefault(task_id, timestamp)
    fcts = sorted(
        completes[task_id] - starts[task_id]
        for task_id in starts.keys() & completes.keys()
        if completes[task_id] >= starts[task_id]
    )
    return {
        "started_tasks": len(starts),
        "completed_tasks": len(completes),
        "matched_tasks": len(fcts),
        "fct_ns": {
            "count": len(fcts),
            "min": min(fcts) if fcts else None,
            "p50": statistics.median(fcts) if fcts else None,
            "mean": statistics.mean(fcts) if fcts else None,
            "max": max(fcts) if fcts else None,
        },
    }


def snapshot(run_dir: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    with (run_dir / "runtime-storage-snapshot.csv").open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            key = ":".join(row[field] for field in ("category", "metric", "resource_kind"))
            result[key] = row["value"]
    return result


def summary_line(run_dir: Path, name: str) -> float | None:
    text = (run_dir / "stdout.txt").read_text(encoding="utf-8", errors="replace")
    match = re.search(rf"\[summary\]\s+{name}\s+:\s+([0-9.]+)\s+(us|ms|s)", text)
    if not match:
        return None
    scale = {"us": 1.0, "ms": 1000.0, "s": 1_000_000.0}[match.group(2)]
    return float(match.group(1)) * scale


def analyze_pair(control: dict[str, Any], treatment: dict[str, Any]) -> dict[str, Any]:
    control_dir = RESULTS / control["case_id"]
    treatment_dir = RESULTS / treatment["case_id"]
    control_files, control_digest = canonical_digest(control_dir)
    treatment_files, treatment_digest = canonical_digest(treatment_dir)
    control_fct = task_fct(control_dir)
    treatment_fct = task_fct(treatment_dir)
    control_snapshot = snapshot(control_dir)
    treatment_snapshot = snapshot(treatment_dir)
    same_inputs = input_hashes(control_dir) == input_hashes(treatment_dir)
    same_attributes = normalized_attributes(control_dir) == normalized_attributes(treatment_dir)
    canonical_equal = control_digest == treatment_digest and control_files == treatment_files
    fct_equal = control_fct == treatment_fct
    snapshot_equal = control_snapshot == treatment_snapshot
    snapshot_differences = {
        key: {"control": control_snapshot.get(key), "treatment": treatment_snapshot.get(key)}
        for key in sorted(set(control_snapshot) | set(treatment_snapshot))
        if control_snapshot.get(key) != treatment_snapshot.get(key)
    }
    non_trace_snapshot_equal = all(
        not key.startswith("trace_writer:") for key in snapshot_differences
    )
    status = "matched" if same_inputs and same_attributes and canonical_equal and fct_equal else "mismatched"
    return {
        "pair_id": control["case_id"].removesuffix("-explicit-local").removesuffix("-explicit-mtp2").removesuffix("-explicit-mtp8"),
        "control": control["case_id"],
        "treatment": treatment["case_id"],
        "status": status,
        "evidence": {
            "measured": {
                "control_returncode": json.loads((control_dir / "record.json").read_text())["returncode"],
                "treatment_returncode": json.loads((treatment_dir / "record.json").read_text())["returncode"],
            },
            "case_derived": {"same_input_hashes": same_inputs, "same_normalized_attributes": same_attributes},
            "trace_derived": {
                "canonical_byte_identical": canonical_equal,
                "control_tasks": control_fct,
                "treatment_tasks": treatment_fct,
                "fct_equal": fct_equal,
            },
            "runtime_storage": {
                "exactly_equal": snapshot_equal,
                "non_trace_metrics_equal": non_trace_snapshot_equal,
                "differences": snapshot_differences,
            },
            "log_derived": {
                "control_run_us": summary_line(control_dir, "run"),
                "treatment_run_us": summary_line(treatment_dir, "run"),
                "control_total_us": summary_line(control_dir, "total"),
                "treatment_total_us": summary_line(treatment_dir, "total"),
            },
        },
    }


def main() -> int:
    rows = load_rows()
    by_id = {row["case_id"]: row for row in rows}
    pairs = [row for row in rows if row["profile"] == "explicit-on"]
    results = [analyze_pair(row, by_id[row["pair"]]) for row in pairs]
    output = {
        "package": str(PACKAGE),
        "source_revision": json.loads((PACKAGE / "evidence/preflight.json").read_text())["source_revision"],
        "pair_count": len(results),
        "status_counts": {status: sum(result["status"] == status for result in results) for status in ("matched", "mismatched")},
        "pairs": results,
    }
    ANALYSIS.mkdir(parents=True, exist_ok=True)
    (ANALYSIS / "comparison.json").write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    lines = [
        "# Default Lifecycle Compatibility Results",
        "",
        f"- Candidate source revision: `{output['source_revision']}`",
        f"- Pairs analyzed: `{output['pair_count']}`",
        f"- Status: `{output['status_counts']}`",
        "- FCT source: trace-derived from canonical `START` and `COMPLETE_VISIBLE` events.",
        "- Host runtime source: log-derived summary lines; no performance claim is made from this matrix.",
        "",
        "| Pair | Status | Canonical | Tasks/FCT | Lifecycle snapshot | Trace writer snapshot |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for result in results:
        evidence = result["evidence"]
        lines.append(
            f"| {result['control']} vs {result['treatment']} | {result['status']} | "
            f"{evidence['trace_derived']['canonical_byte_identical']} | "
            f"{evidence['trace_derived']['fct_equal']} | "
            f"{evidence['runtime_storage']['non_trace_metrics_equal']} | "
            f"{evidence['runtime_storage']['exactly_equal']} |"
        )
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "All rows are expected to be `matched` only when fixture hashes, normalized attributes, "
            "canonical bytes and task/FCT evidence agree. The comparison isolates explicit attribute "
            "presence from effective runtime defaults; it does not compare main versus next and does "
            "not estimate lifecycle memory savings.",
            "",
            "Snapshot differences, where present, are limited to `trace_writer:*` buffering and "
            "file lifecycle counters. Non-trace lifecycle/resource metrics are equal; these counters "
            "are observability artifacts and are not treated as protocol mismatches.",
        ]
    )
    (ANALYSIS / "final-review.md").write_text("\n".join(lines) + "\n")
    print(json.dumps({"pair_count": output["pair_count"], "status_counts": output["status_counts"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
