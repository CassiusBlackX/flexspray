#!/usr/bin/env python3
"""Materialize, run, and analyze the frozen next/lifecycle differential matrix."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shutil
import subprocess
import time
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
REPO = PACKAGE.parents[1]
ROOTS = {
    "next": Path("/Users/ytxing/workspace/ns-3-ub-routing-modes"),
    "lifecycle": REPO,
}
HEADS = {
    "next": "cf8ee683e00e5b6210661013e93cec7217d662e5",
    "lifecycle": "824b87a0503133a6ca4f43f8bd8ce9bbe2631341",
}
CASES = (
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
LIFECYCLE_ONLY = re.compile(r"(^|/)LifecycleTrace|(^|/)LifecycleTraceSchema")


def replace_line(path: Path, needle: str, replacement: str) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    matches = [i for i, line in enumerate(lines) if needle in line]
    if len(matches) != 1:
        raise RuntimeError(f"expected one {needle!r} in {path}, found {len(matches)}")
    lines[matches[0]] = replacement
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def copy_inputs(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for path in source.iterdir():
        if path.is_file() and path.name in INPUT_NAMES:
            shutil.copy2(path, destination / path.name)


def write_spec(case: str, description: str) -> None:
    path = PACKAGE / "cases" / case / "experiment-spec.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"# Experiment Spec: {case}\n\n"
        "## Planning Mode\n\n- `experiment-group`\n\n"
        "## Goal\n\n"
        f"- {description}\n\n"
        "## Fixed Controls\n\n"
        "- Paired case-input bytes, local-single release runtime, `rng-run=1`, routing, flow control, and observability.\n\n"
        "## Prediction\n\n"
        "- `next` and lifecycle preserve task outcome and protocol-observable behavior; any byte difference requires exact evidence.\n\n"
        "## Artifact Contract\n\n"
        "- Inputs: CSV files and full `network_attribute.txt`; outputs: stdout/stderr, output/, runlog/, pair summary.\n",
        encoding="utf-8",
    )


def manifest(case_dir: Path) -> dict[str, str]:
    result = {}
    for path in sorted(case_dir.iterdir()):
        if path.is_file() and path.name in INPUT_NAMES:
            result[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def materialize(replace: bool) -> None:
    descriptions = {}
    ctp_source = REPO / "scratch/rtp-ctp-macro-uneven-moderate-4m-ctp-win128"
    for window in (1, 4, 128):
        case = f"ctp-win{window}"
        descriptions[case] = (
            f"Compare four concurrent same-Entity/VL CTP WRITEs with Jetty segment window {window}."
        )
        for role in ROOTS:
            destination = PACKAGE / "cases" / case / role / "case-input"
            if destination.exists():
                if not replace:
                    raise RuntimeError(f"refusing to overwrite {destination}")
                shutil.rmtree(destination)
            copy_inputs(ctp_source, destination)
            replace_line(
                destination / "network_attribute.txt",
                "ns3::UbJetty::UbJettyInflightMax",
                f'default ns3::UbJetty::UbJettyInflightMax "{window}"',
            )
            (destination / "traffic.csv").write_text(
                "taskId,sourceNode,destNode,dataSize(Byte),opType,priority,delay,phaseId,dependOnPhases,srcEntityId,dstEntityId\n"
                + "".join(
                    f"{task},0,1,1048576,URMA_WRITE,7,10ns,0,,0,0\n" for task in range(4)
                ),
                encoding="utf-8",
            )

    shared_source = REPO / "tests/unified_bus_lifecycle/cases/medium-shared-tp-concurrency"
    case = "rtp-shared-win1"
    descriptions[case] = "Compare shared-TP ACK wake and fairness with a one-packet transport window."
    for role in ROOTS:
        destination = PACKAGE / "cases" / case / role / "case-input"
        if destination.exists():
            if not replace:
                raise RuntimeError(f"refusing to overwrite {destination}")
            shutil.rmtree(destination)
        copy_inputs(shared_source, destination)
        replace_line(
            destination / "network_attribute.txt",
            "ns3::UbTransportChannel::DefaultMaxInflightPacketSize",
            'default ns3::UbTransportChannel::DefaultMaxInflightPacketSize "1"',
        )

    gbn_source = REPO / "tests/unified_bus_lifecycle/cases/medium-multi-tp-retransmission"
    case = "rtp-gbn-double-drop"
    descriptions[case] = "Compare GBN recovery after deterministic one-time drops of PSN 1 and PSN 3."
    for role in ROOTS:
        destination = PACKAGE / "cases" / case / role / "case-input"
        if destination.exists():
            if not replace:
                raise RuntimeError(f"refusing to overwrite {destination}")
            shutil.rmtree(destination)
        copy_inputs(gbn_source, destination)
        (destination / "retrans_fault.csv").write_text(
            "ruleId,enabled,taskId,nodeId,portId,direction,packetType,psn,lastPacket,dropCount,delayNs,delayCount,comment\n"
            "gbn_psn1,true,0,2,1,forward,DATA,1,any,1,0,0,drop PSN 1 once\n"
            "gbn_psn3,true,0,2,1,forward,DATA,3,any,1,0,0,drop PSN 3 once\n",
            encoding="utf-8",
        )

    tpg_source = REPO / "scratch/rtp-ctp-micro-rtp-tpg"
    case = "rtp-tpg-gbn"
    descriptions[case] = "Compare TPG member pinning when task 0 retransmits while task 1 is runnable."
    for role in ROOTS:
        destination = PACKAGE / "cases" / case / role / "case-input"
        if destination.exists():
            if not replace:
                raise RuntimeError(f"refusing to overwrite {destination}")
            shutil.rmtree(destination)
        copy_inputs(tpg_source, destination)
        replace_line(
            destination / "network_attribute.txt",
            "ns3::UbTransportChannel::EnableRetrans",
            'default ns3::UbTransportChannel::EnableRetrans "true"',
        )
        replace_line(
            destination / "network_attribute.txt",
            "ns3::UbTransportChannel::DefaultMaxInflightPacketSize",
            'default ns3::UbTransportChannel::DefaultMaxInflightPacketSize "4"',
        )
        replace_line(
            destination / "network_attribute.txt",
            "ns3::UbFault::UbFaultUsePacketSpray",
            'default ns3::UbFault::UbFaultUsePacketSpray "false"',
        )
        replace_line(
            destination / "network_attribute.txt",
            "global UB_FAULT_ENABLE",
            'global UB_FAULT_ENABLE "true"',
        )
        (destination / "fault.csv").write_text(
            "taskId,faultType,dropRate(%),delay(ns),lowerDataRate(node port datrate(Mbps)),shutdownDropPackets(start end),OccasionalDropRate(%)\n",
            encoding="utf-8",
        )
        (destination / "retrans_fault.csv").write_text(
            "ruleId,enabled,taskId,nodeId,portId,direction,packetType,psn,lastPacket,dropCount,delayNs,delayCount,comment\n"
            "tpg_member0_psn1,true,0,2,1,forward,DATA,1,any,1,0,0,drop task 0 PSN 1 on member 0 once\n",
            encoding="utf-8",
        )
        (destination / "traffic.csv").write_text(
            "taskId,sourceNode,destNode,dataSize(Byte),opType,priority,delay,phaseId,dependOnPhases\n"
            "0,0,1,1048576,URMA_WRITE,7,10ns,0,\n"
            "1,0,1,1048576,URMA_WRITE,7,10ns,0,\n",
            encoding="utf-8",
        )

    for case, description in descriptions.items():
        write_spec(case, description)
        manifests = {}
        for role in ROOTS:
            case_dir = PACKAGE / "cases" / case / role / "case-input"
            manifests[role] = manifest(case_dir)
            (case_dir / "input-manifest.json").write_text(
                json.dumps(manifests[role], indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
        if manifests["next"] != manifests["lifecycle"]:
            raise RuntimeError(f"paired inputs differ for {case}")
    print(f"materialized {len(CASES)} paired cases")


def verify_root(role: str) -> None:
    root = ROOTS[role]
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    if head != HEADS[role]:
        raise RuntimeError(f"{role} HEAD is {head}, expected {HEADS[role]}")
    binary = root / "build/scratch/ns3.44-ub-quick-example"
    if not binary.is_file():
        raise RuntimeError(f"missing frozen binary: {binary}")


def update_ledger(case: str, role: str, status: str, rc: int, elapsed: float, artifacts: int, observation: str) -> None:
    path = PACKAGE / "run-ledger.md"
    lines = path.read_text(encoding="utf-8").splitlines()
    prefix = f"| `{case}` | {role} |"
    indexes = [i for i, line in enumerate(lines) if line.startswith(prefix)]
    if len(indexes) != 1:
        raise RuntimeError(f"ledger row not unique: {case}/{role}")
    lines[indexes[0]] = (
        f"| `{case}` | {role} | {status} | {rc} | {elapsed:.3f}s | "
        f"{artifacts} files | {observation} |"
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run(replace: bool) -> int:
    for role in ROOTS:
        verify_root(role)
    failures = 0
    for case in CASES:
        for role, root in ROOTS.items():
            run_root = PACKAGE / "cases" / case / role
            case_dir = run_root / "case-input"
            for generated in (case_dir / "output", case_dir / "runlog"):
                if generated.exists():
                    if not replace:
                        raise RuntimeError(f"refusing to overwrite {generated}")
                    shutil.rmtree(generated)
            argument = f"scratch/ub-quick-example --case-path={case_dir} --rng-run=1"
            command = ["python3.12", "./ns3", "run", "--no-build", argument]
            started = time.monotonic()
            completed = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            elapsed = time.monotonic() - started
            (run_root / "stdout.txt").write_bytes(completed.stdout)
            (run_root / "stderr.txt").write_bytes(completed.stderr)
            (run_root / "exit-code.txt").write_text(f"{completed.returncode}\n", encoding="ascii")
            (run_root / "run-command.txt").write_text(" ".join(command) + "\n", encoding="utf-8")
            (run_root / "timing.json").write_text(
                json.dumps({"elapsed_seconds": elapsed}, indent=2) + "\n", encoding="utf-8"
            )
            artifacts = sum(
                1
                for directory in (case_dir / "output", case_dir / "runlog")
                if directory.exists()
                for path in directory.rglob("*")
                if path.is_file()
            )
            status = "success" if completed.returncode == 0 else "failed"
            update_ledger(case, role, status, completed.returncode, elapsed, artifacts, "analysis pending")
            print(f"{case}/{role}: rc={completed.returncode}, elapsed={elapsed:.3f}s, artifacts={artifacts}", flush=True)
            failures += completed.returncode != 0
    return 1 if failures else 0


def shared_files(case_dir: Path) -> dict[str, str]:
    result = {}
    for subdir in ("output", "runlog"):
        root = case_dir / subdir
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            relative = path.relative_to(case_dir).as_posix()
            if LIFECYCLE_ONLY.search(relative):
                continue
            result[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def read_tasks(case_dir: Path) -> list[dict[str, str]]:
    path = case_dir / "output/task_statistics.csv"
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def analyze() -> int:
    summaries = {}
    failures = 0
    comparison_root = PACKAGE / "comparisons"
    comparison_root.mkdir(exist_ok=True)
    for case in CASES:
        roots = {
            role: PACKAGE / "cases" / case / role / "case-input" for role in ROOTS
        }
        files = {role: shared_files(path) for role, path in roots.items()}
        common = sorted(set(files["next"]) & set(files["lifecycle"]))
        changed = [path for path in common if files["next"][path] != files["lifecycle"][path]]
        next_only = sorted(set(files["next"]) - set(files["lifecycle"]))
        lifecycle_only = sorted(set(files["lifecycle"]) - set(files["next"]))
        tasks = {role: read_tasks(path) for role, path in roots.items()}
        task_ids = {
            role: [row.get("taskId") for row in rows] for role, rows in tasks.items()
        }
        status = "byte-identical" if not changed and not next_only and not lifecycle_only else "different"
        if task_ids["next"] != task_ids["lifecycle"]:
            status = "regression"
        summary = {
            "case": case,
            "status": status,
            "common_file_count": len(common),
            "changed_shared_files": changed,
            "next_only_shared_files": next_only,
            "lifecycle_only_shared_files": lifecycle_only,
            "task_ids": task_ids,
            "tasks": tasks,
        }
        summaries[case] = summary
        path = comparison_root / f"{case}.json"
        path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"{case}: {status}, changed={len(changed)}, common={len(common)}")
        failures += status == "regression"

    ctp_effect = {}
    for role in ROOTS:
        values = {}
        for window in (1, 4, 128):
            rows = summaries[f"ctp-win{window}"]["tasks"][role]
            values[str(window)] = [row.get("taskCompletesTime(us)") for row in rows]
        ctp_effect[role] = values
    result = {"cases": summaries, "ctp_window_effect": ctp_effect}
    (PACKAGE / "analysis.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("materialize", "run", "analyze"))
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()
    if args.action == "materialize":
        materialize(args.replace)
        return 0
    if args.action == "run":
        return run(args.replace)
    return analyze()


if __name__ == "__main__":
    raise SystemExit(main())
