#!/usr/bin/env python3
"""Materialize and serially run the default lifecycle compatibility matrix."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import yaml


PACKAGE = Path(__file__).resolve().parents[1]
REPO = PACKAGE.parents[1]
SOURCE_PACKAGE = REPO / "scratch/20260802-main-next-controlled-comparison"
RESULTS = PACKAGE / "evidence/runs"
PREFLIGHT = PACKAGE / "evidence/preflight.json"
LEDGER = PACKAGE / "run-ledger.json"
BINARY = REPO / "build/scratch/ns3.44-ub-quick-example"
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


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=REPO, text=True).strip()


def load_matrix() -> dict[str, Any]:
    return yaml.safe_load((PACKAGE / "matrix.yaml").read_text(encoding="utf-8"))


def load_manifest() -> dict[str, Any]:
    return yaml.safe_load((PACKAGE / "command-manifest.yaml").read_text(encoding="utf-8"))


def load_source_materializer():
    path = SOURCE_PACKAGE / "scripts/materialize_case.py"
    spec = importlib.util.spec_from_file_location("source_materializer", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load source materializer: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_writer_module():
    scripts = REPO / ".codex/skills/openusim-run-experiment/scripts"
    sys.path.insert(0, str(scripts))
    from openusim_run_experiment import network_attribute_writer

    return network_attribute_writer


def catalog_defaults() -> tuple[dict[str, str], Path]:
    writer = load_writer_module()
    catalog, path = writer.load_or_build_parameter_catalog()
    defaults = {entry["parameter_key"]: str(entry["default_value"]).lower() for entry in catalog["entries"]}
    return defaults, path


def require_runtime_defaults() -> tuple[dict[str, str], Path]:
    defaults, catalog_path = catalog_defaults()
    expected = load_matrix()["shared"]["required_runtime_defaults"]
    mismatches = {
        key: {"expected": str(value).lower(), "actual": defaults.get(key)}
        for key, value in expected.items()
        if defaults.get(key) != str(value).lower()
    }
    if mismatches:
        raise RuntimeError(f"runtime default mismatch: {json.dumps(mismatches, sort_keys=True)}")
    return {key: defaults[key] for key in expected}, catalog_path


def fixture_files(scenario: str) -> tuple[Path, list[Path]]:
    root = SOURCE_PACKAGE / "fixtures" / scenario
    source = root / "legacy"
    manifest = json.loads((root / "fixture-manifest.json").read_text(encoding="utf-8"))
    files: list[Path] = []
    for relative, expected in manifest["files"].items():
        if not relative.startswith("legacy/"):
            continue
        path = root / relative
        if sha256(path) != expected:
            raise RuntimeError(f"fixture hash mismatch: {path}")
        files.append(path)
    required = {"node.csv", "topology.csv", "routing_table.csv", "traffic.csv"}
    present = {path.name for path in files}
    if missing := sorted(required - present):
        raise RuntimeError(f"fixture {scenario} is missing {missing}")
    return source, files


def strip_lifecycle_lines(path: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    filtered = [line for line in lines if not any(key in line for key in LIFECYCLE_KEYS)]
    path.write_text("\n".join(filtered) + "\n", encoding="utf-8")


def hash_case_inputs(case_dir: Path) -> dict[str, str]:
    return {
        path.name: sha256(path)
        for path in sorted(case_dir.iterdir())
        if path.is_file() and path.name != "network_attribute.txt"
    }


def normalized_attributes(path: Path) -> str:
    return "\n".join(
        line
        for line in path.read_text(encoding="utf-8").splitlines()
        if not any(key in line for key in LIFECYCLE_KEYS)
    )


def materialize_row(row: dict[str, Any], overwrite: bool) -> dict[str, Any]:
    run_dir = RESULTS / row["case_id"]
    case_dir = run_dir / "case"
    if run_dir.exists():
        if not overwrite:
            raise RuntimeError(f"refusing to overwrite existing output: {run_dir}")
        shutil.rmtree(run_dir)
    case_dir.mkdir(parents=True)

    _, files = fixture_files(row["scenario"])
    for source in files:
        shutil.copy2(source, case_dir / source.name)

    source_materializer = load_source_materializer()
    overrides = source_materializer.explicit_overrides(
        "next", row["scenario"], "direct-completion-on"
    )
    for key in LIFECYCLE_KEYS:
        overrides.pop(key, None)
    if row["profile"] == "explicit-on":
        overrides.update({key: "true" for key in LIFECYCLE_KEYS})

    writer = load_writer_module()
    rendered = writer.write_network_attributes(
        case_dir,
        explicit_overrides=overrides,
        observability_overrides=source_materializer.observability("normalized-correctness"),
    )
    if row["profile"] == "implicit-default":
        strip_lifecycle_lines(case_dir / "network_attribute.txt")

    defaults, catalog_path = require_runtime_defaults()
    resolved = {
        "case_id": row["case_id"],
        "scenario": row["scenario"],
        "profile": row["profile"],
        "mtp_threads": row["mtp_threads"],
        "source_revision": git("rev-parse", "HEAD"),
        "binary_sha256": sha256(BINARY),
        "fixture_source": str(SOURCE_PACKAGE / "fixtures" / row["scenario"]),
        "parameter_catalog": str(catalog_path),
        "runtime_defaults": defaults,
        "effective_lifecycle_values": {key: "true" for key in LIFECYCLE_KEYS},
        "lifecycle_attributes_explicit": row["profile"] == "explicit-on",
        "network_attribute_writer": rendered,
    }
    (run_dir / "resolved-config.json").write_text(
        json.dumps(resolved, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    hashes = hash_case_inputs(case_dir)
    (run_dir / "input-hashes.json").write_text(
        json.dumps(hashes, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    subprocess.run(
        [
            sys.executable,
            str(REPO / "scratch/ns-3-ub-tools/ub_tools.py"),
            "case",
            "show",
            str(case_dir),
            "--json",
        ],
        cwd=REPO,
        check=True,
        stdout=(run_dir / "case-inspection.json").open("w", encoding="utf-8"),
    )
    return resolved


def verify_pairs() -> None:
    rows = {row["case_id"]: row for row in load_matrix()["rows"]}
    for row in rows.values():
        left = RESULTS / row["case_id"]
        right = RESULTS / row["pair"]
        left_hashes = json.loads((left / "input-hashes.json").read_text(encoding="utf-8"))
        right_hashes = json.loads((right / "input-hashes.json").read_text(encoding="utf-8"))
        if left_hashes != right_hashes:
            raise RuntimeError(f"paired fixture mismatch: {row['case_id']} vs {row['pair']}")
        left_attrs = normalized_attributes(left / "case/network_attribute.txt")
        right_attrs = normalized_attributes(right / "case/network_attribute.txt")
        if left_attrs != right_attrs:
            raise RuntimeError(f"paired attribute mismatch: {row['case_id']} vs {row['pair']}")
        implicit = left if row["profile"] == "implicit-default" else right
        implicit_text = (implicit / "case/network_attribute.txt").read_text(encoding="utf-8")
        if any(key in implicit_text for key in LIFECYCLE_KEYS):
            raise RuntimeError(f"implicit case contains lifecycle override: {implicit}")


def preflight(allow_dirty: bool, persist: bool = True) -> dict[str, Any]:
    dirty = bool(git("status", "--porcelain"))
    if dirty and not allow_dirty:
        raise RuntimeError("execution requires a clean candidate worktree")
    if not BINARY.is_file():
        raise RuntimeError(f"candidate binary is missing: {BINARY}")
    defaults, catalog_path = require_runtime_defaults()
    result = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "source_revision": git("rev-parse", "HEAD"),
        "dirty": dirty,
        "binary": str(BINARY),
        "binary_sha256": sha256(BINARY),
        "catalog": str(catalog_path),
        "runtime_defaults": defaults,
        "matrix_rows": len(load_matrix()["rows"]),
    }
    if persist:
        PREFLIGHT.parent.mkdir(parents=True, exist_ok=True)
        PREFLIGHT.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    return result


def command_for(row: dict[str, Any]) -> list[str]:
    run_dir = RESULTS / row["case_id"]
    case_dir = run_dir / "case"
    return [
        str(BINARY),
        f"--case-path={case_dir}",
        "--rng-run=1",
        f"--mtp-threads={row['mtp_threads']}",
        f"--dependency-visibility-delay={load_matrix()['shared']['dependency_visibility_delay']}",
        "--route-runtime-mode=legacy-csv",
        f"--canonical-output={run_dir / 'canonical'}",
        f"--runtime-storage-snapshot={run_dir / 'runtime-storage-snapshot.csv'}",
    ]


def write_ledger(records: dict[str, Any]) -> None:
    LEDGER.write_text(json.dumps(records, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def canonical_hashes(run_dir: Path) -> dict[str, str]:
    return {
        path.name: sha256(path)
        for path in sorted(run_dir.glob("canonical*"))
        if path.is_file()
    }


def verify_pair_outputs(row: dict[str, Any]) -> dict[str, str] | None:
    pair_dir = RESULTS / row["pair"]
    if not (pair_dir / "record.json").is_file():
        return None
    current = canonical_hashes(RESULTS / row["case_id"])
    paired = canonical_hashes(pair_dir)
    if not current or current != paired:
        raise RuntimeError(f"canonical output mismatch: {row['case_id']} vs {row['pair']}")
    return current


def execute() -> None:
    if not load_manifest()["execution_approved"]:
        raise RuntimeError("command manifest has not been approved for simulation execution")
    if not PREFLIGHT.is_file():
        raise RuntimeError("missing frozen preflight evidence")
    frozen = json.loads(PREFLIGHT.read_text(encoding="utf-8"))
    current = preflight(allow_dirty=False, persist=False)
    if current["source_revision"] != frozen["source_revision"] or current["binary_sha256"] != frozen["binary_sha256"]:
        raise RuntimeError("candidate provenance changed after preflight")
    verify_pairs()
    execution_names = {
        "record.json",
        "stdout.txt",
        "stderr.txt",
        "runtime-storage-snapshot.csv",
    }
    stale = [
        path
        for row in load_matrix()["rows"]
        for path in (RESULTS / row["case_id"]).iterdir()
        if path.name in execution_names or path.name.startswith("canonical")
    ]
    if LEDGER.exists() or stale:
        raise RuntimeError("existing execution evidence requires a new package or explicit reset")
    records: dict[str, Any] = {}
    for row in load_matrix()["rows"]:
        run_dir = RESULTS / row["case_id"]
        command = command_for(row)
        timed_out = False
        returncode = 124
        with (run_dir / "stdout.txt").open("wb") as stdout, (run_dir / "stderr.txt").open("wb") as stderr:
            try:
                completed = subprocess.run(
                    command, cwd=run_dir, stdout=stdout, stderr=stderr, timeout=1800
                )
                returncode = completed.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
        record = {
            "case_id": row["case_id"],
            "command": command,
            "returncode": returncode,
            "status": "success" if returncode == 0 else "failed",
            "timed_out": timed_out,
            "source_revision": current["source_revision"],
            "binary_sha256": current["binary_sha256"],
        }
        (run_dir / "record.json").write_text(
            json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        records[row["case_id"]] = record
        write_ledger(records)
        if returncode != 0:
            raise RuntimeError(f"simulation failed: {row['case_id']}")
        pair_hashes = verify_pair_outputs(row)
        if pair_hashes is not None:
            record["canonical_hashes"] = pair_hashes
            record["pair_status"] = "byte-identical"
            (run_dir / "record.json").write_text(
                json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            records[row["case_id"]] = record
            write_ledger(records)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("preflight", "materialize", "dry-run", "run"))
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    if args.command == "preflight":
        print(json.dumps(preflight(args.allow_dirty), sort_keys=True))
        return 0
    if args.command == "materialize":
        preflight(args.allow_dirty)
        for row in load_matrix()["rows"]:
            materialize_row(row, args.overwrite)
        verify_pairs()
        print(f"materialized {len(load_matrix()['rows'])} rows")
        return 0
    if args.command == "dry-run":
        preflight(args.allow_dirty)
        verify_pairs()
        for row in load_matrix()["rows"]:
            print(" ".join(command_for(row)))
        return 0
    execute()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
