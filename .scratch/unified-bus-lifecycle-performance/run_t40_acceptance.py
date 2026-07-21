#!/usr/bin/env python3
"""Run the final frozen next/current complex lifecycle differential serially."""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
T28_RUNNER = HERE / "run_t28_acceptance.py"
EVIDENCE_ROOT = HERE / "evidence/t40-final-v1"
COMPLEX_PACKAGE = REPO_ROOT / "scratch/20260717-next-lifecycle-ctp-rtp-complex-differential"
CASES = (
    "ctp-win1",
    "ctp-win4",
    "ctp-win128",
    "rtp-shared-win1",
    "rtp-gbn-double-drop",
    "rtp-tpg-gbn",
)
ROLES = {
    "current": {
        "source_root": REPO_ROOT,
        "revision": "61abfafe1c69a985a14d42c5bbb616227f5bbf52",
    },
    "next": {
        "source_root": Path("/Users/ytxing/workspace/ns-3-ub-routing-modes"),
        "revision": "cf8ee683e00e5b6210661013e93cec7217d662e5",
    },
}


def load_t28_runner():
    spec = importlib.util.spec_from_file_location("t28_acceptance", T28_RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load T28 runner: {T28_RUNNER}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ROLES = ROLES
    module.EVIDENCE_ROOT = EVIDENCE_ROOT
    return module


T28 = load_t28_runner()


def git_head(root: Path) -> str:
    return subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip()


def unified_bus_source_tree(root: Path, revision: str) -> str:
    return subprocess.check_output(
        ["git", "rev-parse", f"{revision}:src/unified-bus"], cwd=root, text=True
    ).strip()


def source_input(case: str) -> Path:
    return COMPLEX_PACKAGE / "cases" / case / "lifecycle/case-input"


def shared_hashes(case_dir: Path) -> dict[str, str]:
    return T28.artifact_hashes(case_dir, shared_only=True)


def freeze() -> None:
    if EVIDENCE_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite T40 evidence: {EVIDENCE_ROOT}")
    EVIDENCE_ROOT.mkdir(parents=True)
    role_manifest = {}
    for role, config in ROLES.items():
        root = config["source_root"]
        revision = git_head(root)
        if revision != config["revision"]:
            raise RuntimeError(
                f"{role} revision changed: expected {config['revision']}, got {revision}"
            )
        binary = T28.binary_for(role)
        library = T28.library_for(role)
        if not binary.is_file() or not library.is_file():
            raise RuntimeError(f"missing frozen binary or library for {role}")
        role_manifest[role] = {
            "revision": revision,
            "source_root": str(root),
            "binary": str(binary),
            "binary_sha256": T28.sha256(binary),
            "unified_bus_library": str(library),
            "unified_bus_library_sha256": T28.sha256(library),
            "cmake": T28.cmake_facts(root),
        }
    case_manifest = {
        case: T28.input_hashes(source_input(case)) for case in CASES
    }
    accepted_comparisons = {
        case: T28.sha256(COMPLEX_PACKAGE / "comparisons" / f"{case}.json")
        for case in CASES
    }
    manifest = {
        "schema_version": 1,
        "roles": role_manifest,
        "cases": case_manifest,
        "accepted_comparison_sha256": accepted_comparisons,
        "run_policy": "strictly-serial-local-single-rng-run-1",
    }
    (EVIDENCE_ROOT / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def verify_frozen() -> dict:
    path = EVIDENCE_ROOT / "manifest.json"
    if not path.is_file():
        raise RuntimeError("run the T40 freeze phase first")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    for role, expected in manifest["roles"].items():
        root = ROLES[role]["source_root"]
        current_revision = git_head(root)
        if (
            current_revision != expected["revision"]
            and unified_bus_source_tree(root, current_revision)
            != unified_bus_source_tree(root, expected["revision"])
        ):
            raise RuntimeError(f"{role} Unified Bus source tree moved after freeze")
        if T28.sha256(T28.binary_for(role)) != expected["binary_sha256"]:
            raise RuntimeError(f"{role} binary changed after freeze")
        if T28.sha256(T28.library_for(role)) != expected["unified_bus_library_sha256"]:
            raise RuntimeError(f"{role} Unified Bus library changed after freeze")
    for case, expected in manifest["cases"].items():
        if T28.input_hashes(source_input(case)) != expected:
            raise RuntimeError(f"frozen input changed for {case}")
        comparison = COMPLEX_PACKAGE / "comparisons" / f"{case}.json"
        if T28.sha256(comparison) != manifest["accepted_comparison_sha256"][case]:
            raise RuntimeError(f"accepted comparison changed for {case}")
    return manifest


def shared_replay_exact(new_case: Path, archived_case: Path) -> bool:
    return shared_hashes(new_case) == shared_hashes(archived_case)


def run_complex() -> None:
    verify_frozen()
    root = EVIDENCE_ROOT / "complex"
    if root.exists():
        raise RuntimeError(f"refusing to overwrite T40 complex evidence: {root}")
    root.mkdir()
    for index, case in enumerate(CASES):
        order = ("next", "current") if index % 2 == 0 else ("current", "next")
        for role in order:
            destination = root / case / role
            result = T28.run_repetition(role, destination, source_input(case), case)
            print(
                f"{case}/{role}: {result['wall_seconds']:.6f}s, "
                f"tasks={result['task_output_count']}",
                flush=True,
            )
    analyze_complex()


def analyze_complex() -> None:
    verify_frozen()
    root = EVIDENCE_ROOT / "complex"
    if not root.is_dir():
        raise RuntimeError("run the T40 complex phase first")
    cases = {}
    for case in CASES:
        results = {
            role: json.loads(
                (root / case / role / "result.json").read_text(encoding="utf-8")
            )
            for role in ("next", "current")
        }
        destinations = {
            role: root / case / role / "case-input" for role in ("next", "current")
        }
        shared = T28.compare_artifacts(
            results["next"],
            results["current"],
            destinations["next"],
            destinations["current"],
        )
        tasks = {
            role: T28.read_tasks(destinations[role]) for role in ("next", "current")
        }
        task_ids = {
            role: [row.get("taskId") for row in rows] for role, rows in tasks.items()
        }
        archived = {
            role: COMPLEX_PACKAGE / "cases" / case / (
                "next/case-input" if role == "next" else "lifecycle/case-input"
            )
            for role in ("next", "current")
        }
        replay = {
            role: shared_replay_exact(destinations[role], archived[role])
            for role in ("next", "current")
        }
        no_unclassified = (
            not shared["left_only"]
            and not shared["right_only"]
            and not shared["unexplained_differences"]
        )
        if case.startswith("ctp-"):
            accepted = (
                no_unclassified
                and not shared["changed"]
                and tasks["next"] == tasks["current"]
            )
            classification = "byte-identical" if accepted else "regression"
        else:
            accepted = all(replay.values()) and task_ids["next"] == task_ids["current"]
            classification = "archived-controlled-difference" if accepted else "regression"
        cases[case] = {
            "accepted": accepted,
            "classification": classification,
            "shared": shared,
            "archived_shared_replay_exact": replay,
            "tasks": tasks,
            "task_ids": task_ids,
            "current_lifecycle": {
                key: results["current"][key]
                for key in (
                    "lifecycle_record_count",
                    "lifecycle_unique_resource_generations",
                    "lifecycle_bytes",
                )
            },
        }
    ctp_window_effect = {
        role: {
            str(window): [
                row.get("taskCompletesTime(us)")
                for row in cases[f"ctp-win{window}"]["tasks"][role]
            ]
            for window in (1, 4, 128)
        }
        for role in ("next", "current")
    }
    ctp_matched = ctp_window_effect["next"] == ctp_window_effect["current"]
    ctp_non_degenerate = len(
        {tuple(values) for values in ctp_window_effect["current"].values()}
    ) == 3
    verify_frozen()
    summary = {
        "schema_version": 1,
        "status": "passed"
        if ctp_matched and ctp_non_degenerate and all(item["accepted"] for item in cases.values())
        else "failed",
        "cases": cases,
        "ctp_window_effect": ctp_window_effect,
        "ctp_matched": ctp_matched,
        "ctp_non_degenerate": ctp_non_degenerate,
        "automatic_tp_cleanup": "excluded-not-implemented",
    }
    (EVIDENCE_ROOT / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {
                "status": summary["status"],
                "cases": {
                    case: item["classification"] for case, item in cases.items()
                },
            },
            indent=2,
            sort_keys=True,
        )
    )
    if summary["status"] != "passed":
        raise RuntimeError("T40 complex acceptance failed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("phase", choices=("freeze", "complex", "analyze"))
    args = parser.parse_args()
    if args.phase == "freeze":
        freeze()
    elif args.phase == "complex":
        run_complex()
    else:
        analyze_complex()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
