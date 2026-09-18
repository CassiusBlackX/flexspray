#!/usr/bin/env python3
"""Strictly verify this fixed CFG7 fixture using current production CSV evidence."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import sys
from collections import Counter
from pathlib import Path

from prepare_run import INPUTS

EXPECTED = {0: (0, 1, "MEM_STORE"), 1: (1, 0, "MEM_LOAD"),
            2: (1, 0, "MEM_STORE"), 3: (0, 1, "MEM_LOAD")}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def csv_rows(path):
    with path.open(newline="") as file:
        return list(csv.DictReader(file))


def finite(value):
    number = float(value)
    require(math.isfinite(number) and number >= 0, f"Invalid timestamp: {value}")
    return number


def verify(case):
    start = json.loads((case / "run-start.json").read_text())
    for name in INPUTS:
        digest = hashlib.sha256((case / name).read_bytes()).hexdigest()
        require(digest == start["inputs_sha256"][name], f"Inputs changed since run: {name}")
    required = [case / name for name in ("runner.log", "runner.exit")]
    required += [case / "output" / "task_statistics.csv", case / "output" / "throughput.csv"]
    for node in (0, 1):
        required += [case / "runlog" / f"{kind}Trace_node_{node}.csv"
                     for kind in ("Task", "Packet")]
    required += [case / "runlog" / f"PortTrace_node_2_port_{port}.csv" for port in (0, 1)]
    for path in required:
        require(path.is_file(), f"Missing evidence: {path}")
        require(path.stat().st_mtime_ns >= start["started_ns"], f"Stale evidence: {path}")
    require((case / "runner.exit").read_text().strip() == "0", "Runner exited unsuccessfully")
    log = (case / "runner.log").read_text()
    for marker in ("[setup] Load network_attribute.txt", "Scheduled local tasks: 4",
                   "[run] Simulation finished.", "Stats: Processed 4 tasks.",
                   "[summary] Program finished."):
        require(marker in log, f"Missing runner evidence: {marker}")
    require(not re.search(r"\b(error|failed|fatal|aborted|SIGSEGV|SIGABRT|dropped)\b", log, re.I),
            "Runner reports failure or packet drop")
    # The existing runner aborts on runtime drops when retransmission is disabled.
    attributes = (case / "network_attribute.txt").read_text()
    for line in ('default ns3::UbTransportChannel::EnableRetrans "false"',
                 'default ns3::UbLdstApi::HeaderConfig "7"',
                 'default ns3::UbLdstEndpoint::HeaderConfig "7"',
                 'default ns3::UbSwitch::FlowControl "CBFC"'):
        require(line in attributes.splitlines(), f"Required fixture setting missing: {line}")

    traffic = csv_rows(case / "traffic.csv")
    require(Counter(int(row["taskId"]) for row in traffic) == Counter(EXPECTED.keys()),
            "Expected exactly tasks 0,1,2,3 in traffic.csv")
    for row in traffic:
        task = int(row["taskId"])
        require((int(row["sourceNode"]), int(row["destNode"]), row["opType"]) == EXPECTED[task]
                and int(row["dataSize(Byte)"]) == 4096, f"Unexpected workload for task {task}")
        ports = (row["srcPortId"], row["dstPortId"])
        require(ports == (("", "") if task < 2 else ("0", "0")),
                f"Unexpected port constraints for task {task}")

    events = {task: {} for task in EXPECTED}
    for path in sorted((case / "runlog").glob("TaskTrace_node_*.csv")):
        node = int(path.stem.rsplit("_", 1)[1])
        for row in csv_rows(path):
            task, event = int(row["task_id"]), row["event"]
            require(task in EXPECTED, f"Unexpected task {task}")
            require(node == EXPECTED[task][0], f"Task {task} observed at wrong source node")
            require(event in ("MEM Task Starts", "MEM Task Completes"),
                    f"Unexpected task event: {event}")
            require(event not in events[task], f"Duplicate {event} for task {task}")
            events[task][event] = finite(row["time_ps"])
    for task, event in events.items():
        require(set(event) == {"MEM Task Starts", "MEM Task Completes"},
                f"Missing start/completion for task {task}")
        require(event["MEM Task Starts"] == 10000 < event["MEM Task Completes"],
                f"Invalid task times: {task}")

    packets = {task: {} for task in EXPECTED}
    for node in (0, 1):
        for row in csv_rows(case / "runlog" / f"PacketTrace_node_{node}.csv"):
            task, event = int(row["task_id"]), row["event"]
            require(task in EXPECTED and node == EXPECTED[task][0], "Unexpected packet task/source")
            require(row["transport"] == "LDST", "Unexpected packet transport")
            require(event in ("First Packet Sends", "Last Packet ACKs")
                    and event not in packets[task], f"Unexpected/duplicate packet event: {task}")
            packets[task][event] = finite(row["time_ps"])

    summaries = csv_rows(case / "output/task_statistics.csv")
    require(Counter(int(row["taskId"]) for row in summaries) == Counter(EXPECTED.keys()),
            "Summary must contain exactly tasks 0,1,2,3")
    for row in summaries:
        task = int(row["taskId"])
        require(set(packets[task]) == {"First Packet Sends", "Last Packet ACKs"},
                f"Missing packet timing: {task}")
        fields = {"taskStartTime(us)": events[task]["MEM Task Starts"],
                  "taskCompletesTime(us)": events[task]["MEM Task Completes"],
                  "firstPacketSends(us)": packets[task]["First Packet Sends"],
                  "lastPacketACKs(us)": packets[task]["Last Packet ACKs"]}
        for field, ps in fields.items():
            require(math.isclose(finite(row[field]), ps / 1e6, rel_tol=0, abs_tol=1e-9),
                    f"Summary disagrees with raw timing: task {task}, {field}")
        require(fields["taskStartTime(us)"] <= fields["firstPacketSends(us)"]
                < fields["lastPacketACKs(us)"] <= fields["taskCompletesTime(us)"],
                f"Packet/task causal order invalid: {task}")

    port_counts = {}
    for port in (0, 1):
        rows = csv_rows(case / "runlog" / f"PortTrace_node_2_port_{port}.csv")
        counts = Counter()
        for row in rows:
            require(int(row["port_id"]) == port and int(row["packet_size_bytes"]) > 0,
                    f"Invalid port row: {port}")
            finite(row["time_ps"])
            require(row["event"] in ("Port Rx", "Port Tx"), "Unexpected port event")
            counts[row["event"]] += 1
        require(counts["Port Rx"] > 0 and counts["Port Tx"] > 0,
                f"Missing middle-switch Rx/Tx on port {port}")
        port_counts[port] = dict(counts)
    return {"status": "PASS", "tasks": sorted(EXPECTED),
            "completion_us": {task: event["MEM Task Completes"] / 1e6
                              for task, event in events.items()}, "switch_ports": port_counts}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-path", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.case_path), indent=2))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
