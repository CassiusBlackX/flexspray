#!/usr/bin/env python3
"""Automated checker for the `2nodes_reliable-rerouting` case.

The checker derives its expectations from the case inputs instead of hard-coding one run:

* `link_fault.csv` gives the absolute failure time and the failed node/port;
* `network_attribute.txt` gives the detection delay and LastAck receive deadline;
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
    "source_node", "source_port", "destination_node", "destination_port",
    "receive_next", "copy", "owner", "processing_packets",
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
        rows = []
        for raw in reader:
            if not raw:
                continue
            if len(raw) != len(header):
                raise CheckFailure(f"truncated or extra columns in {path}")
            rows.append(dict(zip(header, raw)))
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
        elif line.startswith("default ns3::UbLink::LastAckTimeout"):
            last_ack_ps = _time_to_ps(fields[-1].strip('"'))
    if detection_ps is None or last_ack_ps is None:
        raise CheckFailure("network_attribute.txt must set FailureDetectionDelay and LastAckTimeout")
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


def validate_handshakes(case_dir, evidence, detection_time, deadline):
    """Bind copy identities to physical directed hops and actual serialization events."""
    import re
    from fractions import Fraction

    links = {}
    with (case_dir / "topology.csv").open(newline="") as handle:
        for row in csv.DictReader(handle):
            a = (int(row["nodeId1"]), int(row["portId1"]))
            b = (int(row["nodeId2"]), int(row["portId2"]))
            match = re.fullmatch(r"([0-9.]+)([KMG]?)bps", row["bandwidth"])
            if not match:
                raise CheckFailure("unsupported topology bandwidth")
            rate = Fraction(match[1]) * {"": 1, "K": 10**3, "M": 10**6, "G": 10**9}[match[2]]
            delay = _time_to_ps(row["delay"])
            links[a] = (b, rate, delay)
            links[b] = (a, rate, delay)
    fault = _read_link_fault(case_dir)
    first = (fault.node_id, fault.port_id)
    if first not in links:
        raise CheckFailure("failure endpoint missing from topology")
    peer = links[first][0]
    endpoints = {first: peer, peer: first}
    actual = {(int(r["node_id"]), int(r["port_id"])) for r in evidence
              if r["event"] == "state:DETECTION_PENDING"}
    if actual != set(endpoints):
        raise CheckFailure("failure evidence does not cover exactly both physical endpoints")

    def time(row):
        return int(row["time_ps"])

    def identity(row):
        return (row["source_node"], row["source_port"], row["copy"])

    legal_identities = {(str(node), str(port), str(copy))
                        for node, port in endpoints for copy in range(1, 4)}
    for row in evidence:
        if row["event"].startswith("lastack_") and identity(row) not in legal_identities:
            raise CheckFailure("unknown LastAck source/copy identity")
    for source, destination in endpoints.items():
        frozen_boundary = None
        for copy in range(1, 4):
            key = (str(source[0]), str(source[1]), str(copy))
            rows = [r for r in evidence if identity(r) == key]
            submit = [r for r in rows if r["event"] == "lastack_submit"]
            starts = [r for r in rows if r["event"] == "lastack_tx_start"]
            finishes = [r for r in rows if r["event"] == "lastack_tx_complete"]
            if len(submit) != 1 or not starts:
                raise CheckFailure(f"{key}: missing real source submission/transmission")
            if time(submit[0]) < detection_time:
                raise CheckFailure(f"{key}: premature LastAck")
            boundary = int(submit[0]["receive_next"])
            if not 0 <= boundary <= 65535:
                raise CheckFailure(f"{key}: out-of-range receive boundary")
            if frozen_boundary is not None and boundary != frozen_boundary:
                raise CheckFailure(f"{key}: three copies disagree on frozen boundary")
            frozen_boundary = boundary
            if any(row["receive_next"] != str(boundary) for row in rows):
                raise CheckFailure(f"{key}: receive boundary changed in transit")
            source_finishes = [r for r in finishes if r["node_id"] == key[0]]
            if len(source_finishes) != 1 or time(source_finishes[0]) > detection_time + 500_000_000_000:
                raise CheckFailure(f"{key}: missing or late source serialization completion")
            visited = set()
            next_node = source[0]
            ready = time(submit[0])
            reached = False
            for start in sorted(starts, key=time):
                node, port = int(start["node_id"]), int(start["port_id"])
                if node != next_node or node in visited or time(start) < ready:
                    raise CheckFailure(f"{key}: impossible or looping directed path")
                visited.add(node)
                if (node, port) in endpoints or (node, port) not in links:
                    raise CheckFailure(f"{key}: failed or nonexistent physical output")
                remote, rate, delay = links[node, port]
                finish = [r for r in finishes if r["node_id"] == str(node) and r["port_id"] == str(port)]
                size = 16 if max(source[0], destination[0]) <= 4095 else 18
                serialization = int(Fraction(size * 8 * 10**12, 1) / rate)
                if len(finish) != 1 or time(finish[0]) < time(start) + serialization:
                    raise CheckFailure(f"{key}: invalid serialization evidence")
                receives = [r for r in rows if r["event"] == "lastack_hop_receive"
                            and r["node_id"] == str(remote[0]) and r["port_id"] == str(remote[1])]
                if not receives:
                    break  # Loss is allowed if another copy completes the handshake.
                if len(receives) != 1 or time(receives[0]) < time(finish[0]) + delay:
                    raise CheckFailure(f"{key}: receipt is not causally after physical delivery")
                ready = time(receives[0])
                next_node = remote[0]
                reached = next_node == destination[0]
            decisions = [r for r in rows if r["event"] in ("lastack_valid", "lastack_duplicate")]
            for decision in decisions:
                if not reached or time(decision) < ready or time(decision) > deadline:
                    raise CheckFailure(f"{key}: accepted message lacks a timely physical path")
                if (int(decision["node_id"]), int(decision["port_id"])) != destination:
                    raise CheckFailure(f"{key}: delivered to wrong failure owner")
                if (int(decision["destination_node"]), int(decision["destination_port"])) != destination:
                    raise CheckFailure(f"{key}: wrong wire destination")
            for row in rows:
                if (row["destination_node"], row["destination_port"]) != tuple(map(str, destination)):
                    raise CheckFailure(f"{key}: inconsistent message destination")


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
    receive_deadline = detection_time + last_ack_ps
    validate_handshakes(case_dir, evidence, detection_time, receive_deadline)

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
        retained = int(wait[0]["retry_packets"])
        if retained <= 0:
            raise CheckFailure(f"node {node_id}: no unreceived recovery work at detection")

        valid = rows_for("lastack_valid", node_id, failed_output)
        if len(valid) != 1:
            raise CheckFailure(f"node {node_id}: expected exactly one valid LastAck")
        last_ack_time = int(valid[0]["time_ps"])
        recovering = rows_for("state:RECOVERING", node_id, failed_output)
        if len(recovering) != 1 or int(recovering[0]["time_ps"]) != last_ack_time:
            raise CheckFailure(f"node {node_id}: recovery must start at actual LastAck receipt")
        for row in rows_for("attempt_start", node_id, failed_output):
            if int(row["time_ps"]) > detection_time or evidence.index(row) > evidence.index(wait[0]):
                raise CheckFailure(f"node {node_id}: send started after output detection")

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
        if len(drain) != 1:
            raise CheckFailure(f"node {node_id}: multiple local drain claims")
        last_drain = drain[0]
        if int(last_drain["time_ps"]) < max(int(row["time_ps"]) for row in transfers):
            raise CheckFailure(f"node {node_id}: drain precedes final ownership transfer")
        if any(int(last_drain[field]) != 0 for field in ("retry_packets", "pending_packets", "processing_packets")):
            raise CheckFailure(
                f"node {node_id}: claimed drain with outstanding recovery work "
                f"(retry={last_drain['retry_packets']}, pending={last_drain['pending_packets']}, processing={last_drain['processing_packets']})"
            )

    if not fault_time_seen:
        raise CheckFailure("no endpoint reported the scheduled failure")
    if len(transfers_per_direction) < 2:
        raise CheckFailure("recovery must be exercised in both link directions")

    # Duplicate recovery of one local attempt or backlog entry is a failure.
    seen: set[tuple[str, str, str]] = set()
    all_transfers = [row for row in evidence if row["event"] == "transfer"]
    for row in all_transfers:
        if row["owner"] not in ("retry", "switch"):
            raise CheckFailure("transfer missing recovery owner")
        key = (row["node_id"], row["owner"], row["attempt_id"], row["old_output"])
        if key in seen:
            raise CheckFailure(f"duplicate recovery transfer for local identity {key}")
        seen.add(key)


    print(
        "PASS: failure at {fail} ps, detection at {det} ps, real LastAck verified, "
        "{transfers} transfers over {dirs} directions, {tasks} tasks complete".format(
            fail=schedule.fail_time_ps,
            det=detection_time,
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

        def remove_actual_sends(target):
            path = target / "output/reliable_rerouting.csv"
            path.write_text("\n".join(line for line in path.read_text().splitlines()
                                       if ",lastack_tx_start," not in line) + "\n")

        def fake_path(target):
            path = target / "output/reliable_rerouting.csv"
            rows = list(csv.DictReader(path.open()))
            for row in rows:
                if row["event"] == "lastack_hop_receive":
                    row["port_id"] = "65535"
                    break
            with path.open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=EXPECTED_EVIDENCE_HEADER)
                writer.writeheader()
                writer.writerows(rows)

        def corrupt_valid(target, field, value):
            path = target / "output/reliable_rerouting.csv"
            with path.open() as handle:
                rows = list(csv.DictReader(handle))
            for row in rows:
                if row["event"] == "lastack_valid":
                    row[field] = value
                    break
            with path.open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=EXPECTED_EVIDENCE_HEADER)
                writer.writeheader()
                writer.writerows(rows)

        def unfinished_processing(target):
            path = target / "output/reliable_rerouting.csv"
            with path.open() as handle:
                rows = list(csv.DictReader(handle))
            for row in rows:
                if row["event"] == "state:DRAINED":
                    row["processing_packets"] = "1"
                    break
            with path.open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=EXPECTED_EVIDENCE_HEADER)
                writer.writeheader()
                writer.writerows(rows)

        expect_rejection("drain-with-processing", unfinished_processing)
        expect_rejection("unknown-copy", lambda target: corrupt_valid(target, "copy", "0"))
        expect_rejection("unknown-source-port", lambda target: corrupt_valid(target, "source_port", "65535"))
        expect_rejection("changed-receive-boundary", lambda target: corrupt_valid(target, "receive_next", "65535"))
        expect_rejection("missing-actual-lastack", remove_actual_sends)
        expect_rejection("fake-physical-path", fake_path)
        expect_rejection("missing-evidence", drop_evidence)
        expect_rejection("truncated-evidence", truncate_evidence)
        expect_rejection("missing-task", drop_task)
        expect_rejection("wrong-detection-time", shift_detection)
        expect_rejection("zero-recovery-in-one-direction", zero_direction)
        expect_rejection("duplicate-transfer", duplicate_transfer)
        expect_rejection("drain-with-outstanding-work", drain_with_work)
    print("PASS: checker self-test exercised the positive run and 13 rejection paths")


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
