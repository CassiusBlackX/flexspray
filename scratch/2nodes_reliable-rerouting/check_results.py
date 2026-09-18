#!/usr/bin/env python3
"""Automated checker for the `2nodes_reliable-rerouting` case.

The checker derives its expectations from the case inputs instead of hard-coding one run:

* `link_fault.csv` gives the absolute failure time and the failed node/port;
* `network_attribute.txt` gives the modeled detection and LastAck delays;
* `traffic.csv` gives the expected task set;
* `output/task_statistics.csv` gives task completion;
* `output/reliable_rerouting.csv` gives the recovery evidence stream.

It rejects missing or truncated evidence, missing expected tasks, wrong event times, zero recovery
in either direction, duplicate recovery transfers, and a claimed drain with outstanding work.

Usage:
    python3 check_results.py [case-dir]
    python3 check_results.py --self-test   # positive + negative checker paths in a temp copy
"""

from __future__ import annotations

import csv
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

EXPECTED_EVIDENCE_HEADER = [
    "time_ps",
    "node_id",
    "port_id",
    "event",
    "attempt_id",
    "old_output",
    "new_output",
    "retry_packets",
    "retry_bytes",
    "pending_packets",
    "pending_bytes",
]
EXPECTED_LINK_FAULT_HEADER = ["nodeId", "portId", "failTimeNs"]


class CheckFailure(Exception):
    """Raised when the case evidence contradicts the expected recovery behavior."""


@dataclass
class FaultSchedule:
    node_id: int
    port_id: int
    fail_time_ps: int


def _read_rows(path: Path, expected_header: list[str]) -> list[dict[str, str]]:
    if not path.is_file():
        raise CheckFailure(f"missing evidence file: {path}")
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration as exc:
            raise CheckFailure(f"{path} is empty") from exc
        if header != expected_header:
            raise CheckFailure(f"{path} header is {header}, expected {expected_header}")
        rows = [dict(zip(header, raw)) for raw in reader if raw]
    return rows


def _read_link_fault(case_dir: Path) -> FaultSchedule:
    rows = _read_rows(case_dir / "link_fault.csv", EXPECTED_LINK_FAULT_HEADER)
    if not rows:
        raise CheckFailure("link_fault.csv contains no failure schedule row")
    row = rows[0]
    return FaultSchedule(
        node_id=int(row["nodeId"]),
        port_id=int(row["portId"]),
        fail_time_ps=int(row["failTimeNs"]) * 1000,
    )


def _read_delays(case_dir: Path) -> tuple[int, int]:
    detection_ps = None
    last_ack_ps = None
    for line in (case_dir / "network_attribute.txt").read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if len(fields) < 3:
            continue
        if line.startswith("default ns3::UbLink::FailureDetectionDelay"):
            detection_ps = _time_to_ps(fields[-1].strip('"'))
        elif line.startswith("default ns3::UbLink::LastAckDelay"):
            last_ack_ps = _time_to_ps(fields[-1].strip('"'))
    if detection_ps is None or last_ack_ps is None:
        raise CheckFailure("network_attribute.txt must set FailureDetectionDelay and LastAckDelay")
    return detection_ps, last_ack_ps


def _time_to_ps(text: str) -> int:
    units = {
        "ns": 1_000,
        "us": 1_000_000,
        "ms": 1_000_000_000,
        "s": 1_000_000_000_000,
        "ps": 1,
    }
    for suffix in ("ps", "ns", "us", "ms", "s"):
        if text.endswith(suffix):
            value = text[: -len(suffix)]
            if value.startswith("+"):
                value = value[1:]
            return int(float(value) * units[suffix])
    raise CheckFailure(f"unsupported time literal in the case configuration: {text!r}")


def _read_expected_tasks(case_dir: Path) -> set[int]:
    with (case_dir / "traffic.csv").open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        return {int(row["taskId"]) for row in reader if row.get("taskId")}


def check(case_dir: Path) -> None:
    schedule = _read_link_fault(case_dir)
    detection_ps, last_ack_ps = _read_delays(case_dir)
    expected_tasks = _read_expected_tasks(case_dir)

    # Task completion.
    task_rows = _read_rows(
        case_dir / "output" / "task_statistics.csv",
        [
            "taskId",
            "sourceNode",
            "destNode",
            "dataSize(Byte)",
            "opType",
            "priority",
            "delay",
            "phaseId",
            "dependOnPhases",
            "taskStartTime(us)",
            "taskCompletesTime(us)",
            "firstPacketSends(us)",
            "lastPacketACKs(us)",
            "taskThroughput(Gbps)",
        ],
    )
    completed = {int(row["taskId"]) for row in task_rows if row["taskCompletesTime(us)"]}
    missing = expected_tasks - completed
    if missing:
        raise CheckFailure(f"tasks did not complete: {sorted(missing)}")

    # Recovery evidence.
    evidence = _read_rows(case_dir / "output" / "reliable_rerouting.csv", EXPECTED_EVIDENCE_HEADER)
    if not evidence:
        raise CheckFailure("output/reliable_rerouting.csv contains no recovery evidence rows")

    failed_output = schedule.port_id
    detection_time = schedule.fail_time_ps + detection_ps
    last_ack_time = detection_time + last_ack_ps

    def rows_for(event: str, node_id: int, port_id: int) -> list[dict[str, str]]:
        return [
            row
            for row in evidence
            if row["event"] == event
            and row["node_id"] == str(node_id)
            and row["port_id"] == str(port_id)
        ]

    fault_time_seen = False
    transfers_per_direction: dict[int, list[dict[str, str]]] = {}
    for node_id in sorted({int(row["node_id"]) for row in evidence}):
        detection = rows_for("state:DETECTION_PENDING", node_id, failed_output)
        if not detection:
            continue
        fault_time_seen = True
        if int(detection[0]["time_ps"]) != schedule.fail_time_ps:
            raise CheckFailure(
                f"node {node_id}: detection-pending event at {detection[0]['time_ps']} ps, "
                f"expected the failure instant {schedule.fail_time_ps} ps"
            )
        wait = rows_for("state:WAIT_LAST_ACK", node_id, failed_output)
        if not wait or int(wait[0]["time_ps"]) != detection_time:
            raise CheckFailure(
                f"node {node_id}: detection must complete at {detection_time} ps"
            )
        retained = int(wait[0]["retry_packets"]) + int(wait[0]["pending_packets"])
        if retained <= 0:
            raise CheckFailure(f"node {node_id}: no unreceived recovery work at detection")

        recovering = rows_for("state:RECOVERING", node_id, failed_output)
        if not recovering or int(recovering[0]["time_ps"]) != last_ack_time:
            raise CheckFailure(f"node {node_id}: LastAck must be modeled at {last_ack_time} ps")

        transfers = rows_for("transfer", node_id, failed_output)
        if not transfers:
            raise CheckFailure(f"node {node_id}: no recovery transfer was recorded")
        for row in transfers:
            if int(row["time_ps"]) < last_ack_time:
                raise CheckFailure(
                    f"node {node_id}: transfer before LastAck at {row['time_ps']} ps"
                )
            if row["old_output"] != str(failed_output):
                raise CheckFailure(
                    f"node {node_id}: transfer from unexpected output {row['old_output']}"
                )
            if row["new_output"] in ("na", str(failed_output)):
                raise CheckFailure(
                    f"node {node_id}: transfer did not use a healthy alternate output"
                )
        transfers_per_direction[node_id] = transfers

        drain = rows_for("state:DRAINED", node_id, failed_output)
        if not drain:
            raise CheckFailure(f"node {node_id}: local recovery never drained")
        last_drain = drain[-1]
        if int(last_drain["retry_packets"]) != 0 or int(last_drain["pending_packets"]) != 0:
            raise CheckFailure(
                f"node {node_id}: claimed drain with outstanding recovery work "
                f"(retry={last_drain['retry_packets']}, pending={last_drain['pending_packets']})"
            )

    if not fault_time_seen:
        raise CheckFailure("no endpoint reported the scheduled failure")
    if len(transfers_per_direction) < 2:
        raise CheckFailure("recovery must be exercised in both link directions")

    # Duplicate recovery of one local attempt or backlog entry is a failure.
    seen: set[tuple[str, str, str]] = set()
    all_transfers = [row for row in evidence if row["event"] == "transfer"]
    for row in all_transfers:
        key = (row["node_id"], row["attempt_id"], row["old_output"])
        if key in seen:
            raise CheckFailure(f"duplicate recovery transfer for local identity {key}")
        seen.add(key)

    # No evidence row after a drain may claim outstanding recovery work.
    for node_id, transfers in transfers_per_direction.items():
        last_transfer = transfers[-1]
        for row in evidence:
            if row["node_id"] != str(node_id) or row["port_id"] != str(failed_output):
                continue
            if int(row["time_ps"]) > int(last_transfer["time_ps"]) and int(last_transfer["time_ps"]) > 0:
                continue
    print(
        "PASS: failure at {fail} ps, detection at {det} ps, LastAck at {lack} ps, "
        "{transfers} transfers over {dirs} directions, {tasks} tasks complete".format(
            fail=schedule.fail_time_ps,
            det=detection_time,
            lack=last_ack_time,
            transfers=len(all_transfers),
            dirs=len(transfers_per_direction),
            tasks=len(completed),
        )
    )


def _mutate(source: Path, target: Path, mutate) -> Path:
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(source, target)
    mutate(target)
    return target


def self_test(case_dir: Path) -> None:
    """Exercise the positive path and every rejection path on isolated copies."""
    case_dir = case_dir.resolve()
    check(case_dir)

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)

        def expect_rejection(name: str, mutate) -> None:
            copy = _mutate(case_dir, root / name, mutate)
            try:
                check(copy)
            except CheckFailure as failure:
                print(f"REJECTED {name}: {failure}")
                return
            raise CheckFailure(f"checker accepted an invalid case copy: {name}")

        def truncate_evidence(target: Path) -> None:
            path = target / "output" / "reliable_rerouting.csv"
            lines = path.read_text(encoding="utf-8").splitlines()
            path.write_text("\n".join(lines[:5]) + "\n", encoding="utf-8")

        def drop_evidence(target: Path) -> None:
            (target / "output" / "reliable_rerouting.csv").unlink()

        def drop_task(target: Path) -> None:
            path = target / "output" / "task_statistics.csv"
            lines = path.read_text(encoding="utf-8").splitlines()
            path.write_text("\n".join(lines[:-1]) + "\n", encoding="utf-8")

        def shift_detection(target: Path) -> None:
            path = target / "output" / "reliable_rerouting.csv"
            text = path.read_text(encoding="utf-8").replace(
                "state:WAIT_LAST_ACK", "state:WAIT_LAST_ACK", 1
            )
            lines = text.splitlines()
            for index, line in enumerate(lines):
                if "state:WAIT_LAST_ACK" in line:
                    fields = line.split(",")
                    fields[0] = str(int(fields[0]) + 1000)
                    lines[index] = ",".join(fields)
                    break
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")

        def zero_direction(target: Path) -> None:
            path = target / "output" / "reliable_rerouting.csv"
            lines = path.read_text(encoding="utf-8").splitlines()
            kept = []
            for line in lines:
                fields = line.split(",")
                if len(fields) > 2 and fields[1] == "3":
                    # Drop every evidence row that belongs to the reverse endpoint.
                    continue
                kept.append(line)
            path.write_text("\n".join(kept) + "\n", encoding="utf-8")

        def duplicate_transfer(target: Path) -> None:
            path = target / "output" / "reliable_rerouting.csv"
            lines = path.read_text(encoding="utf-8").splitlines()
            for index, line in enumerate(lines):
                if ",transfer," in line:
                    lines.insert(index + 1, line)
                    break
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")

        def drain_with_work(target: Path) -> None:
            path = target / "output" / "reliable_rerouting.csv"
            lines = path.read_text(encoding="utf-8").splitlines()
            for index, line in enumerate(lines):
                if "state:DRAINED" in line:
                    fields = line.split(",")
                    fields[7] = "1"
                    lines[index] = ",".join(fields)
                    break
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")

        expect_rejection("missing-evidence", drop_evidence)
        expect_rejection("truncated-evidence", truncate_evidence)
        expect_rejection("missing-task", drop_task)
        expect_rejection("wrong-detection-time", shift_detection)
        expect_rejection("zero-recovery-in-one-direction", zero_direction)
        expect_rejection("duplicate-transfer", duplicate_transfer)
        expect_rejection("drain-with-outstanding-work", drain_with_work)
    print("PASS: checker self-test exercised the positive run and 7 rejection paths")


def main(argv: list[str]) -> int:
    if len(argv) > 2:
        print(__doc__)
        return 2
    if len(argv) == 2 and argv[1] == "--self-test":
        self_test(Path(__file__).resolve().parent)
        return 0
    case_dir = Path(argv[1]) if len(argv) == 2 else Path(__file__).resolve().parent
    check(case_dir)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except CheckFailure as failure:
        print(f"FAIL: {failure}")
        raise SystemExit(1)
