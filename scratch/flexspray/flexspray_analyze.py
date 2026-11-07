#!/usr/bin/env python3
"""Analyze one FlexSpray case run: ideal (LP) makespan bound vs measured completion and goodput.

Upper bound ("protocol upper bound"): a fluid LP with perfect per-plane splitting of every bulk
flow. Port constraints count wire bits: data packets cost (MTU+78)/MTU per payload bit and ACKs
cost ack_bytes/MTU on the reverse direction of the same port. L1 uplink/downlink aggregates are
constrained too (so switch-port faults are reflected). A fault at t_f switches capacities for the
remainder of the run (two-phase LP, bisection on the makespan).

Usage: flexspray_analyze.py CASE_DIR [--json OUT]
"""
import argparse
import collections
import csv
import glob
import json
import math
import os
import sys

import numpy as np
from scipy.optimize import linprog
from scipy.sparse import coo_matrix

MTU = 4096
DATA_HDR = 78


def load_case(case):
    meta = json.load(open(os.path.join(case, "case_meta.json")))
    tasks = json.load(open(os.path.join(case, "tasks_meta.json")))
    for h in meta["hosts"]:
        h["ports"] = {int(k): v for k, v in h["ports"].items()}
    return meta, tasks


def capacities(meta, faulted):
    """Return dev-port caps {(dev, plane): Gbps} and L1 aggregate caps {(p, s): Gbps up/down}."""
    rate = meta["rate_gbps"]
    hosts = meta["hosts"]
    cap = {(h["id"], p): float(rate) for h in hosts for p in h["planes"]}
    up = {}
    for key in meta["l1"]:
        p, s = map(int, key.split(","))
        up[(p, s)] = float(rate * meta["spines"] * meta["links_per_pair"])
    down = dict(up)
    if faulted:
        l1_by_node = {v: tuple(map(int, k.split(","))) for k, v in meta["l1"].items()}
        port_of = {(h["id"], h["ports"][p]): p for h in hosts for p in h["planes"]}
        for (_, node, port, action, value) in meta["fault_rows"]:
            if node in l1_by_node and action == "ISOLATE_NODE":
                p, s = l1_by_node[node]
                for h in hosts:
                    if h["server"] == s and p in h["planes"]:
                        cap[(h["id"], p)] = 0.0
            elif node in l1_by_node and action == "ISOLATE":
                p, s = l1_by_node[node]
                up[(p, s)] -= rate
                down[(p, s)] -= rate
            elif (node, port) in port_of:
                p = port_of[(node, port)]
                if action == "ISOLATE":
                    cap[(node, p)] = 0.0
                elif action == "DEGRADE":
                    num, den = map(int, value.split("/"))
                    cap[(node, p)] = rate * num / den
    return cap, up, down


def bulk_flows(meta, tasks):
    hosts = {h["id"]: h for h in meta["hosts"]}
    flows = []
    for t in tasks:
        if t["cls"] == "ctrl":
            continue
        src, dst = (t["dst"], t["src"]) if t["op"] == "URMA_READ" else (t["src"], t["dst"])
        planes = sorted(set(hosts[src]["planes"]) & set(hosts[dst]["planes"]))
        flows.append(dict(task=t["task"], src=src, dst=dst, bits=t["bytes"] * 8.0, planes=planes,
                          cross=hosts[src]["server"] != hosts[dst]["server"],
                          ssrv=hosts[src]["server"], dsrv=hosts[dst]["server"], cls=t["cls"]))
    return flows


def build_constraints(meta, flows, caps_list, durations, ack):
    """Variables: r[phase][f][plane] (Gbps). Returns A_ub, b_ub rows for capacity constraints."""
    var = {}
    for ph in range(len(caps_list)):
        for fi, f in enumerate(flows):
            for p in f["planes"]:
                var[(ph, fi, p)] = len(var)
    rows, cols, vals, b = [], [], [], []
    data_w = (MTU + DATA_HDR) / MTU
    ack_w = ack / MTU
    for ph, (cap, up, down) in enumerate(caps_list):
        eg = collections.defaultdict(list)
        ing = collections.defaultdict(list)
        l1u = collections.defaultdict(list)
        l1d = collections.defaultdict(list)
        for fi, f in enumerate(flows):
            for p in f["planes"]:
                v = var[(ph, fi, p)]
                eg[(f["src"], p)].append((v, data_w)); ing[(f["src"], p)].append((v, ack_w))
                ing[(f["dst"], p)].append((v, data_w)); eg[(f["dst"], p)].append((v, ack_w))
                if f["cross"]:
                    l1u[(p, f["ssrv"])].append((v, data_w)); l1d[(p, f["ssrv"])].append((v, ack_w))
                    l1d[(p, f["dsrv"])].append((v, data_w)); l1u[(p, f["dsrv"])].append((v, ack_w))
        for group, capd in ((eg, cap), (ing, cap), (l1u, up), (l1d, down)):
            for key, terms in group.items():
                r = len(b)
                for v, w in terms:
                    rows.append(r); cols.append(v); vals.append(w)
                b.append(capd.get(key, 0.0))
    return var, rows, cols, vals, b


def solve_lambda(meta, flows, caps_list, durations, ack):
    """Max lambda s.t. sum_phase sum_plane r*dur >= lambda*bits_f for all flows (rates in Gbps)."""
    var, rows, cols, vals, b = build_constraints(meta, flows, caps_list, durations, ack)
    nv = len(var) + 1  # last = lambda
    lam = nv - 1
    r0 = len(b)
    for fi, f in enumerate(flows):
        r = r0 + fi
        for ph, dur in enumerate(durations):
            for p in f["planes"]:
                rows.append(r); cols.append(var[(ph, fi, p)]); vals.append(-dur)  # Gbit/us*1e3
        rows.append(r); cols.append(lam); vals.append(f["bits"] / 1e3)  # bits in Gbit*us units
        b.append(0.0)
    A = coo_matrix((vals, (rows, cols)), shape=(len(b), nv)).tocsr()
    c = np.zeros(nv); c[lam] = -1.0
    res = linprog(c, A_ub=A, b_ub=np.array(b), bounds=[(0, None)] * nv, method=os.environ.get("FLEX_LP_METHOD", "highs-ipm"))
    if res.status != 0:
        raise RuntimeError(res.message)
    return res.x[lam]


def _capacity_rows(flows, cap, up, down, var, ack):
    """Yield (key, [(var_index, weight)], capacity) port/L1 constraint rows for one phase."""
    data_w = (MTU + DATA_HDR) / MTU
    ack_w = ack / MTU
    groups = {"eg": collections.defaultdict(list), "ing": collections.defaultdict(list),
              "l1u": collections.defaultdict(list), "l1d": collections.defaultdict(list)}
    for fi, f in enumerate(flows):
        for p in f["planes"]:
            v = var[(fi, p)]
            groups["eg"][(f["src"], p)].append((v, data_w)); groups["ing"][(f["src"], p)].append((v, ack_w))
            groups["ing"][(f["dst"], p)].append((v, data_w)); groups["eg"][(f["dst"], p)].append((v, ack_w))
            if f["cross"]:
                groups["l1u"][(p, f["ssrv"])].append((v, data_w)); groups["l1d"][(p, f["ssrv"])].append((v, ack_w))
                groups["l1d"][(p, f["dsrv"])].append((v, data_w)); groups["l1u"][(p, f["dsrv"])].append((v, ack_w))
    for name, capd in (("eg", cap), ("ing", cap), ("l1u", up), ("l1d", down)):
        for key, terms in groups[name].items():
            yield terms, capd.get(key, 0.0)


def ideal_makespan_us(meta, flows, ack):
    """Minimum makespan (us) with perfect per-plane splitting (fluid LP).

    Healthy: max lambda s.t. rates r (Gbps) with sum_p r_fp >= lambda * bits_f; T = 1/lambda.
    Fault at t_f: one LP. Phase 0 uses rates r0 (Gbps) for t_f us under healthy capacities; phase 1
    moves b1 (Gbit*1e-3 = Gbps*us) under reduced capacities C1 during z us: sum w*b1 <= C1*z.
    Minimise z subject to t_f*sum_p r0_fp + sum_p b1_fp >= bits_f. T = t_f + z.
    """
    caps0 = capacities(meta, faulted=False)
    lam = solve_lambda(meta, flows, [caps0], [1.0], ack)  # lambda per us
    t_healthy = 1.0 / lam
    if not meta.get("fault_rows"):
        return t_healthy
    tf = meta["fault_ns"] / 1000.0
    if t_healthy <= tf:
        return t_healthy
    cap0, up0, down0 = caps0
    cap1, up1, down1 = capacities(meta, faulted=True)
    var0, var1 = {}, {}
    for fi, f in enumerate(flows):
        for p in f["planes"]:
            var0[(fi, p)] = len(var0)
    n0 = len(var0)
    for fi, f in enumerate(flows):
        for p in f["planes"]:
            var1[(fi, p)] = n0 + len(var1)
    zi = n0 + len(var1)
    rows, cols, vals, b = [], [], [], []
    for terms, c in _capacity_rows(flows, cap0, up0, down0, var0, ack):
        r = len(b)
        for v, w in terms:
            rows.append(r); cols.append(v); vals.append(w)
        b.append(c)
    for terms, c in _capacity_rows(flows, cap1, up1, down1, var1, ack):
        r = len(b)
        for v, w in terms:
            rows.append(r); cols.append(v); vals.append(w)
        rows.append(r); cols.append(zi); vals.append(-c)
        b.append(0.0)
    for fi, f in enumerate(flows):
        r = len(b)
        for p in f["planes"]:
            rows.append(r); cols.append(var0[(fi, p)]); vals.append(-tf)
            rows.append(r); cols.append(var1[(fi, p)]); vals.append(-1.0)
        b.append(-f["bits"] / 1e3)
    nv = zi + 1
    A = coo_matrix((vals, (rows, cols)), shape=(len(b), nv)).tocsr()
    c = np.zeros(nv); c[zi] = 1.0
    res = linprog(c, A_ub=A, b_ub=np.array(b), bounds=[(0, None)] * nv, method=os.environ.get("FLEX_LP_METHOD", "highs-ipm"))
    if res.status != 0:
        raise RuntimeError(res.message)
    return tf + res.x[zi]


def read_tasks(case):
    start, done = {}, {}
    for fn in glob.glob(os.path.join(case, "runlog", "TaskTrace_*.csv")):
        with open(fn, errors="replace") as f:
            for r in csv.DictReader(line for line in f if "\0" not in line):
                try:
                    tid = int(r["task_id"])
                    int(r["time_ps"])
                except (TypeError, ValueError):
                    continue  # skip a malformed (e.g. partially written) row
                if r["event"] == "WQE Starts":
                    start.setdefault(tid, int(r["time_ps"]) / 1e6)
                elif r["event"] == "WQE Completes":
                    done[tid] = int(r["time_ps"]) / 1e6
    return start, done


def read_samples(case):
    """{(node, port): [(t_ns, tx_payload, rx_payload, rate_bps, tx_wire, rx_wire)]}"""
    out = collections.defaultdict(list)
    fn = os.path.join(case, "runlog", "GoodputSample.csv")
    if not os.path.exists(fn):
        return out
    with open(fn) as f:
        for r in csv.DictReader(f):
            out[(int(r["node"]), int(r["port"]))].append(
                (int(r["time_ns"]), int(r["tx_payload_bytes"]), int(r["rx_payload_bytes"]),
                 int(r["rate_bps"]), int(r["tx_wire_bytes"]), int(r["rx_wire_bytes"])))
    for v in out.values():
        v.sort()
    return out


def window_rate(series, t0_us, t1_us, idx):
    """Average Gbps of cumulative column idx between t0 and t1."""
    if not series or t1_us <= t0_us:
        return 0.0
    ts = [x[0] for x in series]
    import bisect
    i0 = max(0, bisect.bisect_right(ts, t0_us * 1000) - 1)
    i1 = max(0, bisect.bisect_right(ts, t1_us * 1000) - 1)
    dt = (series[i1][0] - series[i0][0])
    return (series[i1][idx] - series[i0][idx]) * 8.0 / dt if dt > 0 else 0.0


def analyze(case):
    meta, tasks = load_case(case)
    flows = bulk_flows(meta, tasks)
    ack = meta["ack_bytes"]
    t_ideal = ideal_makespan_us(meta, flows, ack)
    start, done = read_tasks(case)
    bulk_ids = [f["task"] for f in flows]
    missing = [i for i in bulk_ids if i not in done]
    comp = [done[i] for i in bulk_ids if i in done]
    makespan = max(comp) if comp else float("nan")
    res = dict(case=os.path.basename(os.path.normpath(case)), scenario=meta["scenario"],
               fault=meta["fault"], scheme=meta["scheme"], seed=meta["seed"],
               t_ideal_us=t_ideal, makespan_us=makespan, missing_tasks=len(missing),
               eff=t_ideal / makespan if comp and not missing else 0.0)
    by_cls = collections.defaultdict(list)
    for f in flows:
        if f["task"] in done:
            by_cls[f["cls"]].append(done[f["task"]])
    res["class_eff"] = {c: t_ideal / max(v) for c, v in by_cls.items()}
    # Per device: last completion among flows it sources or sinks (data direction).
    dev_last = collections.defaultdict(float)
    for f in flows:
        if f["task"] in done:
            for d in (f["src"], f["dst"]):
                dev_last[d] = max(dev_last[d], done[f["task"]])
    hosts = {h["id"]: h for h in meta["hosts"]}
    dev_eff = {d: t_ideal / t for d, t in dev_last.items()}
    type_eff = collections.defaultdict(list)
    for d, e in dev_eff.items():
        type_eff[hosts[d]["type"]].append(e)
    res["device_type_min_eff"] = {k: min(v) for k, v in type_eff.items()}
    res["device_min_eff"] = min(dev_eff.values()) if dev_eff else 0.0
    # Control-message latency.
    lat = []
    for t in tasks:
        if t["cls"] == "ctrl" and t["task"] in done:
            lat.append(done[t["task"]] - t["delay_ns"] / 1000.0)
    if lat:
        lat.sort()
        res["ctrl_lat_us"] = dict(p50=lat[len(lat) // 2], p99=lat[int(len(lat) * 0.99) - 1],
                                  max=lat[-1], n=len(lat))
    # Windowed goodput per device (payload, tx and rx summed over ports).
    samples = read_samples(case)
    first_done = min(comp) if comp else 0.0
    tf = meta["fault_ns"] / 1000.0 if meta.get("fault_ns") else None
    end = 0.95 * first_done
    windows = {"steady": (10.0, end)} if tf is None else {
        "pre": (10.0, tf), "post": (tf + 10.0, max(end, tf + 20.0))}
    res["windows"] = {k: list(v) for k, v in windows.items()}
    dev_win = {}
    for name, (a, b) in windows.items():
        per = {}
        for h in meta["hosts"]:
            tx = sum(window_rate(samples.get((h["id"], port)), a, b, 1) for port in h["ports"].values())
            rx = sum(window_rate(samples.get((h["id"], port)), a, b, 2) for port in h["ports"].values())
            per[h["id"]] = (tx, rx)
        dev_win[name] = per
    res["dev_window_gbps"] = {k: {str(d): v for d, v in per.items()} for k, per in dev_win.items()}
    res["agg_window_gbps"] = {k: sum(v[0] for v in per.values()) for k, per in dev_win.items()}
    fi = meta.get("fault_info") or {}
    res["fault_info"] = fi
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case")
    ap.add_argument("--json")
    a = ap.parse_args()
    res = analyze(a.case)
    out = json.dumps(res, indent=1)
    if a.json:
        open(a.json, "w").write(out)
    short = {k: v for k, v in res.items() if k not in ("dev_window_gbps",)}
    print(json.dumps(short, indent=1))


if __name__ == "__main__":
    main()
