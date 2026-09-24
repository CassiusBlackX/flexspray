#!/usr/bin/env python3
"""Validate the fixed ROI experiment's evidence; never implement protocol behavior."""

import argparse
import copy
import csv
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import sys

COLUMNS = "seq,time_ps,node,event,task_id,opcode,address,byte_count,value,packet_bytes".split(",")
PACKAGE = Path(__file__).resolve().parent


def load_case(directory):
    """Keep missing/malformed evidence visible instead of dropping a matrix row."""
    evidence = {"directory": str(directory), "load_errors": [], "hashes": {}}
    for name in ("events.csv", "summary.json", "resolved-parameters.json", "console.log", "run-record.json"):
        try:
            raw = (directory / name).read_bytes()
            evidence["hashes"][name] = hashlib.sha256(raw).hexdigest()
            if name == "events.csv":
                reader = csv.DictReader(raw.decode().splitlines())
                if reader.fieldnames != COLUMNS:
                    raise ValueError("unexpected CSV columns")
                evidence["events"] = list(reader)
            elif name.endswith(".json"):
                evidence[name] = json.loads(raw)
        except (OSError, ValueError, UnicodeError) as exc:
            evidence["load_errors"].append(f"{name}: {exc}")
    return evidence


def validate(evidence, variant):
    errors = list(evidence["load_errors"])
    result = {"case_id": variant, "status": "failed", "valid": False, "errors": errors,
              "directory": evidence["directory"], "artifact_sha256": evidence["hashes"],
              "prediction_status": "inconclusive", "metrics": {}}
    if errors:
        return result
    rows = copy.deepcopy(evidence["events"])
    summary = evidence["summary.json"]
    run = evidence["run-record.json"]
    result["execution_return_code"] = run.get("return_code") if isinstance(run, dict) else None
    result["execution_status"] = "success" if result["execution_return_code"] == 0 else "failed"
    parameters = evidence["resolved-parameters.json"]

    def require(condition, message):
        if not condition:
            errors.append(message)

    if not isinstance(summary, dict) or not isinstance(parameters, dict):
        errors.append("summary/parameters must be JSON objects")
        return result
    for row in rows:
        if set(row) != set(COLUMNS):
            errors.append("malformed CSV row")
            return result
        for field in ("seq", "time_ps", "task_id", "opcode", "byte_count", "value", "packet_bytes"):
            value = row[field]
            if value == "" and field not in ("seq", "time_ps"):
                continue
            if not isinstance(value, str) or re.fullmatch(r"[0-9]+", value) is None:
                errors.append(f"non-integer {field}: {value!r}")
                return result
            if int(value) > 2**64 - 1:
                errors.append(f"out-of-range {field}")
                return result
        row["seq"] = int(row["seq"])
        row["time_ps"] = int(row["time_ps"])
        require(row["node"] in ("A", "B"), "invalid node")
        require(not row["address"] or re.fullmatch(r"0x[0-9a-f]+", row["address"]),
                "invalid address representation")
    require([r["seq"] for r in rows] == list(range(1, len(rows) + 1)),
            "event sequence must be consecutive and unique")
    require(all(a["time_ps"] <= b["time_ps"] for a, b in zip(rows, rows[1:])),
            "event times must be nondecreasing")

    if errors:
        return result

    def select(name, task=None):
        return [r for r in rows if r["event"] == name and
                (task is None or r["task_id"] == str(task))]

    def one(name, task=None):
        found = select(name, task)
        require(len(found) == 1, f"expected one {name} for task {task}, got {len(found)}")
        return found[0] if len(found) == 1 else None

    def before(a, b, message):
        if a is not None and b is not None:
            require(a["seq"] < b["seq"] and a["time_ps"] <= b["time_ps"], message)

    def fields(row, **expected):
        if row is not None:
            for key, value in expected.items():
                require(row[key] == str(value), f"{row['event']} {key}: expected {value}, got {row[key]}")

    tasks = (1, 2) if variant == "control" else (1,)
    final_task = tasks[-1]
    data_opcode = 3 if variant == "control" else 25
    network = {}
    for name in ("wqe_submit", "request_tx", "request_rx", "target_execute",
                 "taack_tx", "taack_rx", "a_complete"):
        require(len(select(name)) == len(tasks), f"wrong total {name} count")
    for task in tasks:
        events = {name: one(name, task) for name in
                  ("wqe_submit", "request_tx", "request_rx", "target_execute",
                   "taack_tx", "taack_rx", "a_complete")}
        address, size, opcode, order = (("0x1000", 64, data_opcode, 1) if task == 1
                                        else ("0x2000", 8, 3, 2))
        for name in ("wqe_submit", "request_tx", "request_rx", "target_execute"):
            fields(events[name], address=address, byte_count=size, opcode=opcode,
                   node="A" if name in ("wqe_submit", "request_tx") else "B")
        for name in ("request_tx", "request_rx"):
            fields(events[name], value=order)
        fields(events["taack_tx"], node="B", opcode=17, byte_count=0)
        fields(events["taack_rx"], node="A", opcode=17, byte_count=0)
        fields(events["a_complete"], node="A")
        if events["wqe_submit"]:
            require(events["wqe_submit"]["time_ps"] == 0, "WQEs must be pre-submitted at time zero")
        chain = list(events.values())
        for a, b in zip(chain, chain[1:]):
            before(a, b, f"task {task} request/response causal order")
        for tx, rx in (("request_tx", "request_rx"), ("taack_tx", "taack_rx")):
            if events[tx] and events[rx]:
                require(events[tx]["packet_bytes"] != "" and
                        int(events[tx]["packet_bytes"]) > 0 and
                        events[tx]["packet_bytes"] == events[rx]["packet_bytes"],
                        "packet size missing or changed between tx/rx")
        network[task] = events

    init = one("counter_init")
    commit = one("data_commit")
    decoded_name = "post_value_decoded" if variant == "control" else "notify_value_decoded"
    decoded = one(decoded_name)
    require(not select("notify_value_decoded" if variant == "control" else "post_value_decoded"),
            "unexpected counter input kind")
    update, ready = one("counter_update"), one("counter_ready")
    cleanup = one("cleanup_complete")
    require(not select("error"), "driver emitted error event")
    fields(init, node="B", address="0x2000", byte_count=8, value=0)
    fields(commit, node="B", task_id=1, opcode=data_opcode, address="0x1000", byte_count=64)
    for row in (decoded, update, ready):
        fields(row, node="B", task_id=final_task, opcode=3 if variant == "control" else 25,
               address="0x2000", byte_count=8, value=64)
    before(init, network[1]["wqe_submit"], "counter initialization must precede submission")
    before(network[1]["target_execute"], commit, "commit must follow target execution")
    before(commit, decoded, "data commit must precede decoded counter update")
    before(network[final_task]["target_execute"], decoded, "decode must follow target execution")
    before(decoded, update, "counter input must be decoded before update")
    before(update, ready, "counter update must precede ready")
    before(ready, network[final_task]["taack_tx"], "ready must precede final TAACK")
    before(network[final_task]["a_complete"], cleanup, "missing normal cleanup after A completion")
    if variant == "control":
        before(network[1]["wqe_submit"], network[2]["wqe_submit"], "wrong WQE submission order")
        before(network[2]["wqe_submit"], network[1]["request_tx"], "post submitted after dispatch")
        before(network[1]["taack_rx"], network[2]["request_tx"], "SO post bypassed data TAACK")
    elif commit and decoded and update and ready:
        require(len({r["time_ps"] for r in (commit, decoded, update, ready)}) == 1,
                "combined commit and counter update must share execution time")

    def time(row):
        return row["time_ps"] if row else None

    submit = time(network[1]["wqe_submit"])
    t_ready, t_complete = time(ready), time(network[final_task]["a_complete"])
    metrics = dict(t_submit_ps=submit, t_data_commit_ps=time(commit), t_counter_ready_ps=t_ready,
                   t_a_complete_ps=t_complete,
                   b_ready_latency_ps=t_ready - submit if None not in (submit, t_ready) else None,
                   a_completion_latency_ps=t_complete - submit if None not in (submit, t_complete) else None,
                   counter_address=update["address"] if update else None,
                   counter_final=int(update["value"]) if update and update["value"] else None,
                   expected_total=64,
                   committed_ranges=[[commit["address"], hex(int(commit["address"], 16) +
                                      int(commit["byte_count"]))]] if commit and commit["address"] and commit["byte_count"] else [],
                   target_execution_count=len(select("target_execute")),
                   readiness_count=len(select("counter_ready")),
                   a_completion_count=len(select("a_complete")),
                   data_packet_count=len(select("request_tx", 1)))
    for key, value in metrics.items():
        require(type(summary.get(key)) is type(value) and summary.get(key) == value,
                f"summary disagrees with raw events: {key}")
    require(summary.get("schema_version") == 1 and summary.get("case_id") == variant and
            summary.get("variant") == variant, "summary identity/schema mismatch")
    require(result["execution_return_code"] == 0, "process did not finish successfully")
    require(summary.get("valid") is True and summary.get("errors") == [], "driver reported invalid run")
    fixed = {"schema_version": 1, "variant": variant, "time_resolution": "ps",
             "rate_bps_each_direction": 400000000000, "propagation_delay_ps_each_direction": 1000000,
             "target_execution_delay_ps": 0, "transport": "RTP", "service_mode": "ROI", "rtp_pairs": 1,
             "jetty": 3, "data_address": "0x1000", "counter_address": "0x2000", "counter_initial": 0,
             "data_bytes": 64, "expected_total": 64, "watchdog_ps": 1000000000}
    for key, value in fixed.items():
        require(type(parameters.get(key)) is type(value) and parameters.get(key) == value,
                f"parameter differs from fixed experiment: {key}")
    runtime = parameters.get("runtime_values", {})
    require(isinstance(runtime, dict) and bool(runtime), "missing resolved runtime attributes")
    if isinstance(runtime, dict):
        for key, value in {
            "A/port/ns3::UbPort::UbDataRate": "400000000000bps",
            "B/port/ns3::UbPort::UbDataRate": "400000000000bps",
            "link/ns3::UbLink::Delay": "+1000000ps",
            "A/controller/ns3::UbController::InitiatorProtocolDispatchDelay": "+0ps",
            "A/controller/ns3::UbController::InitiatorProtocolCompletionVisibilityDelay": "+0ps",
        }.items():
            actual = runtime.get(key)
            if value.endswith("ps") and not value.endswith("bps"):
                try:
                    matches = isinstance(actual, str) and actual.endswith("ps") and Fraction(actual[:-2]) == Fraction(value[:-2])
                except (ValueError, ZeroDivisionError):
                    matches = False
            else:
                matches = actual == value
            require(matches, f"actual attribute disagrees: {key}")
    identity = parameters.get("program_identity", {})
    require(isinstance(identity, dict) and all(re.fullmatch(r"[0-9a-f]{64}", str(identity.get(k, "")))
            for k in ("source_sha256", "binary_sha256")), "missing source/binary identity")
    result.update(valid=not errors, status="completed" if not errors else "failed", metrics=metrics)
    result["network"] = network
    return result


def attribution(case, parameters):
    """Account using observed boundaries and case-derived serialization, without tolerance."""
    rate = parameters["rate_bps_each_direction"]
    propagation = parameters["propagation_delay_ps_each_direction"]
    packets = []
    consistent = True
    for task, events in case["network"].items():
        for kind in ("request", "taack"):
            tx, rx = events[kind + "_tx"], events[kind + "_rx"]
            serialization = Fraction(int(tx["packet_bytes"]) * 8 * 10**12, rate)
            observed = rx["time_ps"] - tx["time_ps"]
            unexplained = Fraction(observed - propagation) - serialization
            consistent &= unexplained == 0
            packets.append(dict(task_id=task, kind=kind, packet_bytes=int(tx["packet_bytes"]),
                                payload_bytes=int(tx["byte_count"]),
                                modeled_nonpayload_bytes=int(tx["packet_bytes"]) - int(tx["byte_count"]),
                                serialization_ps=float(serialization), flight_ps=observed,
                                unexplained_flight_ps=float(unexplained),
                                tx_seq=tx["seq"], rx_seq=rx["seq"]))
    n = case["network"]
    metrics = case["metrics"]
    waits = {"submit_to_data_tx_ps": n[1]["request_tx"]["time_ps"] - metrics["t_submit_ps"],
             "data_rx_to_commit_ps": metrics["t_data_commit_ps"] - n[1]["request_rx"]["time_ps"]}
    if case["case_id"] == "control":
        waits.update(data_commit_to_taack_tx_ps=n[1]["taack_tx"]["time_ps"] - metrics["t_data_commit_ps"],
                     data_taack_rx_to_post_tx_ps=n[2]["request_tx"]["time_ps"] - n[1]["taack_rx"]["time_ps"],
                     post_rx_to_ready_ps=metrics["t_counter_ready_ps"] - n[2]["request_rx"]["time_ps"])
        path = [(1, "request"), (1, "taack"), (2, "request")]
    else:
        waits["commit_to_ready_ps"] = metrics["t_counter_ready_ps"] - metrics["t_data_commit_ps"]
        path = [(1, "request")]
    serial = sum(p["serialization_ps"] for p in packets if (p["task_id"], p["kind"]) in path)
    unexplained = metrics["b_ready_latency_ps"] - len(path) * propagation - serial - sum(waits.values())
    return dict(packets=packets, observed_waits_ps=waits, path_serialization_ps=serial,
                path_wait_ps=sum(waits.values()), unexplained_b_latency_ps=unexplained,
                all_packet_flights_explained=consistent)


def compare(control, treatment):
    cases = [validate(control, "control"), validate(treatment, "treatment")]
    report = {"schema_version": 1, "valid": False, "errors": [], "cases": cases,
              "prediction_status": "inconclusive", "delta_b_ps": None,
              "reduction_percent": None, "propagation_rtt_ps": None,
              "residual_vs_propagation_rtt_ps": None}
    if not all(c["valid"] for c in cases):
        report["errors"].append("one or more planned cases have invalid or missing evidence")
        return report
    cp, tp = (e["resolved-parameters.json"] for e in (control, treatment))
    left, right = ({k: v for k, v in p.items() if k != "variant"} for p in (cp, tp))
    if left != right:
        report["errors"].append("fixed parameters or executable identity differ between cases")
        return report
    if cases[0]["metrics"]["b_ready_latency_ps"] <= 0:
        report["errors"].append("control B latency must be positive")
        return report
    delta = cases[0]["metrics"]["b_ready_latency_ps"] - cases[1]["metrics"]["b_ready_latency_ps"]
    rtt = 2 * cp["propagation_delay_ps_each_direction"]
    for case, params in zip(cases, (cp, tp)):
        case["attribution"] = attribution(case, params)
    ca, ta = (c["attribution"] for c in cases)
    explained_residual = ca["path_serialization_ps"] - ta["path_serialization_ps"] + ca["path_wait_ps"] - ta["path_wait_ps"]
    unexplained = delta - rtt - explained_residual
    complete = (unexplained == 0 and all(c["attribution"]["all_packet_flights_explained"] and
                c["attribution"]["unexplained_b_latency_ps"] == 0 for c in cases))
    # Structural validity never depends on positive speedup or proximity to an expected time.
    prediction = "mismatched" if delta <= 0 else ("matched" if complete else "inconclusive")
    report.update(valid=True, delta_b_ps=delta,
                  delta_a_ps=cases[0]["metrics"]["a_completion_latency_ps"] - cases[1]["metrics"]["a_completion_latency_ps"],
                  reduction_percent=100 * delta / cases[0]["metrics"]["b_ready_latency_ps"],
                  propagation_rtt_ps=rtt, residual_vs_propagation_rtt_ps=delta - rtt,
                  explained_residual_ps=explained_residual, unexplained_residual_ps=unexplained,
                  prediction_status=prediction)
    cases[0]["prediction_status"] = "matched" if ca["all_packet_flights_explained"] and ca["unexplained_b_latency_ps"] == 0 else "inconclusive"
    cases[1]["prediction_status"] = prediction
    return report


def markdown(report):
    lines = ["# ROI write completion counter — measured comparison", "",
             f"Structural validity: **{report['valid']}**; hypothesis: **{report['prediction_status']}**.", "",
             "Primary metric is target B readiness; initiator A completion is auxiliary.", "",
             "| Case | Execution | Valid | Prediction | B latency (ps) | A latency (ps) |",
             "| --- | --- | --- | --- | ---: | ---: |"]
    for c in report["cases"]:
        m = c["metrics"]
        lines.append(f"| {c['case_id']} | {c.get('execution_status', 'missing')} | {c['valid']} | {c['prediction_status']} | "
                     f"{m.get('b_ready_latency_ps')} | {m.get('a_completion_latency_ps')} |")
    lines += ["", "## Prediction versus actual", "",
              "Pre-registered propagation-only prediction: control B/A = 3/4 us; treatment B/A = 1/2 us.",
              "This is a propagation model, not a structural acceptance threshold.", ""]
    if report["valid"]:
        lines += [f"Measured ΔB: {report['delta_b_ps']} ps ({report['delta_b_ps']/1e6:.6f} us); "
                  f"relative reduction: {report['reduction_percent']:.6f}%.",
                  f"Measured ΔA: {report['delta_a_ps']} ps.",
                  f"Case-derived propagation RTT: {report['propagation_rtt_ps']} ps.",
                  f"Residual ΔB − RTT: {report['residual_vs_propagation_rtt_ps']} ps; "
                  f"accounted residual: {report['explained_residual_ps']} ps; "
                  f"unexplained residual: {report['unexplained_residual_ps']} ps.", "",
                  "## Packet and waiting evidence", "",
                  "Transmission trace is the accepted port transmission start; receive trace is full "
                  "arrival before switch processing. Packet sizes include modeled link and protocol "
                  "headers, excluding physical preamble/FEC. TPACK events are separate from TAACK.", "",
                  "| Case | Task | Kind | Bytes | Nonpayload bytes | Serialization (ps) | Flight (ps) | Tx→Rx event seq |",
                  "| --- | ---: | --- | ---: | ---: | ---: | ---: | --- |"]
        for c in report["cases"]:
            for p in c["attribution"]["packets"]:
                lines.append(f"| {c['case_id']} | {p['task_id']} | {p['kind']} | {p['packet_bytes']} | "
                             f"{p['modeled_nonpayload_bytes']} | {p['serialization_ps']} | {p['flight_ps']} | {p['tx_seq']}→{p['rx_seq']} |")
        for c in report["cases"]:
            lines += ["", f"Observed waits for {c['case_id']} (ps):", ""]
            lines += [f"- `{k}`: {v}" for k, v in c["attribution"]["observed_waits_ps"].items()]
        lines += ["", "The control chain is data request → B commit → data TAACK → SO post → B ready. "
                  "Its post transmission follows data TAACK reception by event sequence. The treatment "
                  "commits then adds the decoded notify value in one callback, without the post exchange. "
                  "Packet flights are checked against propagation plus size/rate serialization exactly; "
                  "the remaining measured boundary intervals are reported above. Those intervals include "
                  "queue/allocation waits; this does not claim a separate measurement of each internal component.",
                  "No arbitrary speedup tolerance is used. Missing flight attribution gives inconclusive; "
                  "valid zero/negative benefit is retained as mismatched."]
    errors = report["errors"] + [f"{c['case_id']}: {e}" for c in report["cases"] for e in c["errors"]]
    if errors:
        lines += ["", "## Diagnostics", ""] + [f"- {e}" for e in errors]
    lines += ["", "## Evidence and limits", "",
              "Raw CSV timestamps/sequences and decoded inputs are measured evidence. Latencies and "
              "differences are trace-derived; serialization and propagation RTT are case-derived. "
              "Console and process return codes are log-derived. JSON includes SHA-256 for every input artifact.", "",
              "- [Control events](../cases/control/output/events.csv), [summary](../cases/control/output/summary.json), "
              "[parameters](../cases/control/output/resolved-parameters.json), [console](../cases/control/output/console.log).",
              "- [Treatment events](../cases/treatment/output/events.csv), [summary](../cases/treatment/output/summary.json), "
              "[parameters](../cases/treatment/output/resolved-parameters.json), [console](../cases/treatment/output/console.log).",
              "- [Run ledger](../run-ledger.md), [plan](../experiment-plan.md), [manifest](../command-manifest.yaml).", "",
              "B-ready is a model completion indicator: no target software polling or CPU scheduling, "
              "no byte-by-byte data memory model, and no multi-packet atomicity, concurrency or retry "
              "deduplication claim. One deterministic measurement per variant is not statistical significance.", ""]
    return "\n".join(lines)


def self_test(control, treatment):
    """Mutate evidence only; these are not additional network experiments."""
    assert compare(control, treatment)["valid"], "self-test requires real valid baseline evidence"
    checks = []
    for kind in ("early-ready", "overshoot", "missing-event", "parameter-mismatch",
                 "summary-mismatch", "noninteger-time", "missing-case"):
        c, t = copy.deepcopy(control), copy.deepcopy(treatment)
        rows = t["events"]
        if kind == "early-ready":
            ready = next(r for r in rows if r["event"] == "counter_ready")
            rows.remove(ready)
            rows.insert(next(i for i, r in enumerate(rows) if r["event"] == "data_commit"), ready)
            for i, row in enumerate(rows, 1):
                row["seq"] = str(i)
        elif kind == "overshoot":
            next(r for r in rows if r["event"] == "counter_update")["value"] = "65"
        elif kind == "missing-event":
            rows.remove(next(r for r in rows if r["event"] == "data_commit"))
        elif kind == "parameter-mismatch":
            t["resolved-parameters.json"]["runtime_values"]["global/RngSeed"] = "2"
        elif kind == "summary-mismatch":
            t["summary.json"]["b_ready_latency_ps"] += 1
        elif kind == "noninteger-time":
            rows[0]["time_ps"] = "0.5"
        else:
            t["load_errors"].append("events.csv: missing")
        assert not compare(c, t)["valid"], f"accepted invalid sample: {kind}"
        checks.append(kind)
    # Add only pre-transmit waiting, preserving all protocol event order and packet flights.
    c, t = copy.deepcopy(control), copy.deepcopy(treatment)
    delay = c["summary.json"]["b_ready_latency_ps"] - t["summary.json"]["b_ready_latency_ps"]
    for row in t["events"]:
        if int(row["time_ps"]) > 0:
            row["time_ps"] = str(int(row["time_ps"]) + delay)
    for key in ("t_data_commit_ps", "t_counter_ready_ps", "t_a_complete_ps",
                "b_ready_latency_ps", "a_completion_latency_ps"):
        t["summary.json"][key] += delay
    report = compare(c, t)
    assert report["valid"] and report["delta_b_ps"] == 0 and report["prediction_status"] == "mismatched"
    checks.append("valid-no-speedup")
    print(json.dumps({"self_test": "passed", "checks": checks}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control-dir", type=Path, default=PACKAGE / "cases/control/output")
    parser.add_argument("--treatment-dir", type=Path, default=PACKAGE / "cases/treatment/output")
    parser.add_argument("--output-dir", type=Path, default=PACKAGE / "output")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    control, treatment = load_case(args.control_dir), load_case(args.treatment_dir)
    if args.self_test:
        self_test(control, treatment)
        return 0
    report = compare(control, treatment)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name in ("comparison.json", "comparison.md"):
        if (args.output_dir / name).exists():
            parser.error("refusing to overwrite prior comparison; archive it and record why first")
    (args.output_dir / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.output_dir / "comparison.md").write_text(markdown(report))
    print(f"valid={report['valid']} hypothesis={report['prediction_status']}")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    sys.exit(main())
