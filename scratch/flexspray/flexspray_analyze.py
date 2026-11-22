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
FIXED_NOTIFY_NS = 2000  # fixed-rule reference: remote ends learn of an isolation 2 us after t_f


def load_case(case):
    meta = json.load(open(os.path.join(case, "case_meta.json")))
    tasks = json.load(open(os.path.join(case, "tasks_meta.json")))
    for h in meta["hosts"]:
        h["ports"] = {int(k): v for k, v in h["ports"].items()}
    return meta, tasks


def capacities(meta, faulted, t_ns=None):
    """Return dev-port caps {(dev, plane): Gbps} and fabric aggregate caps {(k): Gbps up/down}.

    When ``t_ns`` is specified, replays only fault rows with ``row_t_ns <= t_ns`` in chronological
    order so ``DEGRADE``/``RESTORE``/``DOWN``/``UP`` transitions and interior uplink ``DEGRADE``s
    yield exact piecewise capacities without perturbing single-fault runs.
    """
    rate = meta["rate_gbps"]
    hosts = meta["hosts"]
    cap = {(h["id"], p): float(rate) for h in hosts for p in h["planes"]}
    if meta.get("topo") == "ub_mesh":
        up = {(p, s1, s2): float(rate * meta["lrs_links"])
              for p in range(meta["p_lrs"])
              for s1 in range(meta["servers"])
              for s2 in range(meta["servers"]) if s1 != s2}
        down = {}
    else:
        up = {}
        for key in meta["l1"]:
            p, s = map(int, key.split(","))
            up[(p, s)] = float(rate * meta["spines"] * meta["links_per_pair"])
        down = dict(up)
    if faulted:
        l1_by_node = {v: tuple(map(int, k.split(","))) for k, v in meta["l1"].items()}
        lrs_bundle = {tuple(map(int, k.split(","))): tuple(v)
                      for k, v in (meta.get("lrs_port_to_bundle") or {}).items()}
        port_of = {(h["id"], h["ports"][p]): p for h in hosts for p in h["planes"]}
        l1_link_factor = {}
        node_zero = set()
        for (rt, node, port, action, value) in sorted(meta.get("fault_rows") or [], key=lambda r: r[0]):
            if t_ns is not None and rt > t_ns:
                continue
            if node in l1_by_node and action in ("ISOLATE_NODE", "DOWN_NODE"):
                p, s = l1_by_node[node]
                for h in hosts:
                    if h["server"] == s and p in h["planes"]:
                        node_zero.add((h["id"], p))
            elif node in l1_by_node and action in ("ISOLATE", "DOWN", "DEGRADE", "RESTORE", "UP"):
                if action in ("ISOLATE", "DOWN"):
                    f = 0.0
                elif action in ("RESTORE", "UP"):
                    f = 1.0
                else:
                    num, den = map(int, value.split("/"))
                    f = num / den
                old = l1_link_factor.get((node, port), 1.0)
                l1_link_factor[(node, port)] = f
                delta = (f - old) * rate
                if (node, port) in lrs_bundle:
                    p, s1, s2 = lrs_bundle[(node, port)]
                    up[(p, s1, s2)] = max(0.0, up[(p, s1, s2)] + delta)
                    up[(p, s2, s1)] = max(0.0, up[(p, s2, s1)] + delta)
                else:
                    p, s = l1_by_node[node]
                    up[(p, s)] = max(0.0, up[(p, s)] + delta)
                    down[(p, s)] = max(0.0, down[(p, s)] + delta)
            elif (node, port) in port_of:
                p = port_of[(node, port)]
                if action in ("ISOLATE", "DOWN"):
                    cap[(node, p)] = 0.0
                elif action in ("RESTORE", "UP"):
                    cap[(node, p)] = float(rate)
                elif action == "DEGRADE":
                    num, den = map(int, value.split("/"))
                    cap[(node, p)] = rate * num / den
        for k in node_zero:
            cap[k] = 0.0
    return cap, up, down


def add_flow_constraints(eg, ing, l1u, l1d, is_mesh, p_lrs, f, p, v, data_w, ack_w):
    """Append host and fabric resource terms for one flow f on plane p (Clos or ub_mesh)."""
    eg[(f["src"], p)].append((v, data_w))
    ing[(f["src"], p)].append((v, ack_w))
    ing[(f["dst"], p)].append((v, data_w))
    eg[(f["dst"], p)].append((v, ack_w))
    if not f["cross"]:
        return
    if is_mesh:
        if p < p_lrs:
            l1u[(p, f["ssrv"], f["dsrv"])].append((v, data_w))
            l1u[(p, f["dsrv"], f["ssrv"])].append((v, ack_w))
    else:
        l1u[(p, f["ssrv"])].append((v, data_w))
        l1d[(p, f["ssrv"])].append((v, ack_w))
        l1d[(p, f["dsrv"])].append((v, data_w))
        l1u[(p, f["dsrv"])].append((v, ack_w))


def bulk_flows(meta, tasks):
    hosts = {h["id"]: h for h in meta["hosts"]}
    flows = []
    for t in tasks:
        if t["cls"] == "ctrl":
            continue
        src, dst = (t["dst"], t["src"]) if t["op"] == "URMA_READ" else (t["src"], t["dst"])
        planes = sorted(set(hosts[src]["planes"]) & set(hosts[dst]["planes"]))
        flows.append(dict(task=t["task"], src=src, dst=dst, bits=t["bytes"] * 8.0, planes=planes,
                          release_us=t.get("delay_ns", 0) / 1000.0,
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
    is_mesh = meta.get("topo") == "ub_mesh"
    p_lrs = meta.get("p_lrs", 0)
    for ph, (cap, up, down) in enumerate(caps_list):
        eg = collections.defaultdict(list)
        ing = collections.defaultdict(list)
        l1u = collections.defaultdict(list)
        l1d = collections.defaultdict(list)
        for fi, f in enumerate(flows):
            for p in f["planes"]:
                add_flow_constraints(eg, ing, l1u, l1d, is_mesh, p_lrs, f, p, var[(ph, fi, p)], data_w, ack_w)
        for group, capd in ((eg, cap), (ing, cap), (l1u, up), (l1d, down)):
            for key, terms in group.items():
                r = len(b)
                for v, w in terms:
                    rows.append(r); cols.append(v); vals.append(w)
                b.append(capd.get(key, 0.0))
    return var, rows, cols, vals, b


def _solve_lp(c, A, b, nv):
    """Solve LP with primary solver (highs-ipm) and presolve-off / relaxed-IPM / simplex fallbacks."""
    primary = os.environ.get("FLEX_LP_METHOD", "highs-ipm")
    b_arr = np.array(b)
    bounds = [(0, None)] * nv
    res = linprog(c, A_ub=A, b_ub=b_arr, bounds=bounds, method=primary)
    if res.status != 0 and primary == "highs-ipm":
        res = linprog(c, A_ub=A, b_ub=b_arr, bounds=bounds, method="highs-ipm",
                      options={"presolve": False})
    if res.status != 0 and primary == "highs-ipm":
        res = linprog(c, A_ub=A, b_ub=b_arr, bounds=bounds, method="highs-ipm",
                      options={"dual_feasibility_tolerance": 1e-4, "ipm_optimality_tolerance": 1e-6})
    if res.status != 0:
        res = linprog(c, A_ub=A, b_ub=b_arr, bounds=bounds, method="highs-ds",
                      options={"time_limit": 180.0})
    if res.status != 0:
        raise RuntimeError(res.message)
    return res


def _agg_flows(flows):
    """Merge flows sharing identical routing, plane set, and release time (exact LP equivalence)."""
    merged = {}
    for f in flows:
        key = (f["src"], f["dst"], f["ssrv"], f["dsrv"], f["cross"],
               tuple(f["planes"]), round(f.get("release_us", 0.0), 9))
        if key not in merged:
            merged[key] = dict(f)
        else:
            merged[key]["bits"] += f["bits"]
    return list(merged.values())


def solve_lambda(meta, flows, caps_list, durations, ack):
    """Max lambda s.t. sum_phase sum_plane r*dur >= lambda*bits_f for all flows (rates in Gbps)."""
    flows = _agg_flows(flows)
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
    res = _solve_lp(c, A, b, nv)
    return res.x[lam]


def drained_after_fault(meta, samples):
    """Wire bytes that still crossed each isolated resource after t_f (graceful isolation drains).

    Returns {(group, key): bytes} with group in eg/ing (device port, key=(dev, plane)) and l1u/l1d
    (L1 aggregate, key=(plane, server)), plus a summary dict. The simulator lets queued and
    in-flight packets of an isolated port drain; the LP bound gets the same allowance so the bound
    and the run describe the same system (otherwise the bound assumes capacity vanishes at t_f and
    the efficiency can exceed 1). The drained bytes are also the *exposure*: what a hard link-down
    at t_f would have lost and had to resend."""
    if not meta.get("fault_rows"):
        return {}, {}
    tf_ns = meta["fault_ns"]
    hosts = meta["hosts"]
    l1_by_node = {v: tuple(map(int, k.split(","))) for k, v in meta["l1"].items()}
    up_port = {v: tuple(map(int, k.split(","))) for k, v in meta["l1_up_port"].items()}
    lrs_bundle = {tuple(map(int, k.split(","))): tuple(v)
                  for k, v in (meta.get("lrs_port_to_bundle") or {}).items()}
    allow = {}
    last_ns = tf_ns

    def after(node, port):
        nonlocal last_ns
        s = samples.get((node, port))
        if not s:
            return 0, 0
        ts = [x[0] for x in s]
        import bisect
        i0 = max(0, bisect.bisect_right(ts, tf_ns) - 1)
        tx = s[-1][4] - s[i0][4]
        rx = s[-1][5] - s[i0][5]
        for i in range(len(s) - 1, i0, -1):
            if s[i][4] != s[i - 1][4] or s[i][5] != s[i - 1][5]:
                last_ns = max(last_ns, s[i][0])
                break
        return tx, rx

    for (_, node, port, action, _v) in meta["fault_rows"]:
        if action == "ISOLATE_NODE" and node in l1_by_node:
            p, s_ = l1_by_node[node]
            for h in hosts:
                if h["server"] == s_ and p in h["ports"]:
                    tx, rx = after(h["id"], h["ports"][p])
                    allow[("eg", (h["id"], p))] = tx
                    allow[("ing", (h["id"], p))] = rx
        elif action == "ISOLATE" and (node, port) in lrs_bundle:
            p, s1, s2 = lrs_bundle[(node, port)]
            tx, rx = after(node, port)
            allow[("l1u", (p, s1, s2))] = tx
            allow[("l1u", (p, s2, s1))] = rx
        elif action == "ISOLATE" and node in l1_by_node:
            p, s_ = l1_by_node[node]
            tx, rx = after(node, port)  # L1 side of the uplink: tx = up, rx = down
            allow[("l1u", (p, s_))] = tx
            allow[("l1d", (p, s_))] = rx
        elif action == "ISOLATE" and node < len(hosts):
            h = hosts[node]
            p = next(pl for pl, pt in h["ports"].items() if pt == port)
            tx, rx = after(node, port)
            allow[("eg", (node, p))] = tx
            allow[("ing", (node, p))] = rx
    total = sum(allow.values())
    summary = dict(exposure_bytes=total, drain_us=(last_ns - tf_ns) / 1000.0 if total else 0.0)
    return allow, summary


def _capacity_rows(flows, cap, up, down, var, ack, allow=None, meta=None):
    """Yield ([(var_index, weight)], capacity, allowance_bytes) constraint rows for one phase."""
    data_w = (MTU + DATA_HDR) / MTU
    ack_w = ack / MTU
    is_mesh = (meta or {}).get("topo") == "ub_mesh"
    p_lrs = (meta or {}).get("p_lrs", 0)
    groups = {"eg": collections.defaultdict(list), "ing": collections.defaultdict(list),
              "l1u": collections.defaultdict(list), "l1d": collections.defaultdict(list)}
    for fi, f in enumerate(flows):
        for p in f["planes"]:
            add_flow_constraints(groups["eg"], groups["ing"], groups["l1u"], groups["l1d"],
                                 is_mesh, p_lrs, f, p, var[(fi, p)], data_w, ack_w)
    for name, capd in (("eg", cap), ("ing", cap), ("l1u", up), ("l1d", down)):
        for key, terms in groups[name].items():
            yield terms, capd.get(key, 0.0), (allow or {}).get((name, key), 0.0)


def _solve_timeindexed_bps(meta, flows, ack, allow, bps, ft0):
    caps_h = capacities(meta, faulted=False)
    nph = len(bps)  # phases 0..nph-2 fixed, nph-1 variable
    var = {}
    for k in range(nph):
        for fi, f in enumerate(flows):
            if f["release_us"] <= bps[k] + 1e-9:
                for p in f["planes"]:
                    var[(k, fi, p)] = len(var)
    zi = len(var)
    rows, cols, vals, b = [], [], [], []
    first_post = next((k for k in range(nph) if ft0 is not None and bps[k] >= ft0 - 1e-9), None)
    for k in range(nph):
        if ft0 is not None and bps[k] >= ft0 - 1e-9:
            cap, up, down = capacities(meta, True, t_ns=int(round(bps[k] * 1000.0)))
        else:
            cap, up, down = caps_h
        vk = {(fi, p): v for (kk, fi, p), v in var.items() if kk == k}
        sub = [f if any((fi, p) in vk for p in f["planes"]) else None for fi, f in enumerate(flows)]
        idx = [fi for fi, f in enumerate(sub) if f is not None]
        sflows = [flows[fi] for fi in idx]
        svar = {(j, p): vk[(fi, p)] for j, fi in enumerate(idx) for p in flows[fi]["planes"]}
        allow_k = allow if k == first_post else None
        dur = bps[k + 1] - bps[k] if k < nph - 1 else None
        for terms, c, d in _capacity_rows(sflows, cap, up, down, svar, ack, allow_k, meta):
            r = len(b)
            for v, w in terms:
                rows.append(r); cols.append(v); vals.append(w)
            if dur is None:
                rows.append(r); cols.append(zi); vals.append(-c)
                b.append(d * 8.0 / 1e3)
            else:
                b.append(c + d * 8.0 / 1e3 / dur)
    for fi, f in enumerate(flows):
        r = len(b)
        for k in range(nph):
            for p in f["planes"]:
                v = var.get((k, fi, p))
                if v is None:
                    continue
                rows.append(r); cols.append(v)
                vals.append(-(bps[k + 1] - bps[k]) if k < nph - 1 else -1.0)
        b.append(-f["bits"] / 1e3)
    nv = zi + 1
    A = coo_matrix((vals, (rows, cols)), shape=(len(b), nv)).tocsr()
    c = np.zeros(nv); c[zi] = 1.0
    res = _solve_lp(c, A, b, nv)
    return bps[-1] + res.x[zi]


def ideal_makespan_timeindexed_us(meta, flows, ack, allow=None, with_fault=True):
    """Exact fluid bound with per-flow release times and piecewise multi-fault timelines.

    Breakpoints = {0, release times, active fault times}. Solves prefix-by-prefix over fault
    timestamps so a job finishing before a later RESTORE/UP transition keeps exact causality."""
    flows = _agg_flows(flows)
    frows = (meta.get("fault_rows") or []) if with_fault else []
    ft_list = sorted({r[0] / 1000.0 for r in frows})
    rel_set = {f["release_us"] for f in flows}
    max_rel = max(rel_set, default=0.0)
    if not ft_list:
        return _solve_timeindexed_bps(meta, flows, ack, None, sorted({0.0} | rel_set), None)
    t_h = _solve_timeindexed_bps(meta, flows, ack, None, sorted({0.0} | rel_set), None)
    if t_h <= ft_list[0] + 1e-9:
        return t_h
    ft0 = ft_list[0]
    for m in range(1, len(ft_list) + 1):
        sub_ft = ft_list[:m]
        bps = sorted({0.0} | rel_set | set(sub_ft))
        t_m = _solve_timeindexed_bps(meta, flows, ack, allow, bps, ft0)
        if m == len(ft_list) or (t_m <= ft_list[m] + 1e-9 and max_rel <= ft_list[m] + 1e-9):
            return t_m
    return t_h


def ideal_makespan_us(meta, flows, ack, allow=None):
    flows = _agg_flows(flows)
    fault_times = {r[0] for r in (meta.get("fault_rows") or [])}
    if any(f.get("release_us", 0) > 0 for f in flows) or len(fault_times) > 1:
        return ideal_makespan_timeindexed_us(meta, flows, ack, allow)
    """Minimum makespan (us) with perfect per-plane splitting (fluid LP).

    Healthy: max lambda s.t. rates r (Gbps) with sum_p r_fp >= lambda * bits_f; T = 1/lambda.
    Fault at t_f: one LP. Phase 0 uses rates r0 (Gbps) for t_f us under healthy capacities; phase 1
    moves b1 (Gbit*1e-3 = Gbps*us) under reduced capacities C1 during z us: sum w*b1 <= C1*z + D,
    where D is the measured drain allowance of an isolated resource (wire bytes that still crossed
    it after t_f in the run; 0 for every other resource).
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
    for terms, c, _ in _capacity_rows(flows, cap0, up0, down0, var0, ack, None, meta):
        r = len(b)
        for v, w in terms:
            rows.append(r); cols.append(v); vals.append(w)
        b.append(c)
    for terms, c, d in _capacity_rows(flows, cap1, up1, down1, var1, ack, allow, meta):
        r = len(b)
        for v, w in terms:
            rows.append(r); cols.append(v); vals.append(w)
        rows.append(r); cols.append(zi); vals.append(-c)
        b.append(d * 8.0 / 1e3)  # bytes -> Gbps*us
    for fi, f in enumerate(flows):
        r = len(b)
        for p in f["planes"]:
            rows.append(r); cols.append(var0[(fi, p)]); vals.append(-tf)
            rows.append(r); cols.append(var1[(fi, p)]); vals.append(-1.0)
        b.append(-f["bits"] / 1e3)
    nv = zi + 1
    A = coo_matrix((vals, (rows, cols)), shape=(len(b), nv)).tocsr()
    c = np.zeros(nv); c[zi] = 1.0
    res = _solve_lp(c, A, b, nv)
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
    samples = read_samples(case)
    allow, drain = drained_after_fault(meta, samples)
    t_ideal_nodrain = ideal_makespan_us(meta, flows, ack)
    t_ideal = ideal_makespan_us(meta, flows, ack, allow) if allow else t_ideal_nodrain
    # Scheme-independent reference (fixed rule, agreed before running any scheme): an isolated
    # resource keeps full capacity until the remote ends are notified (t_f + notify delay) and has
    # none afterwards; a lane degrade takes effect at t_f. Unlike t_ideal (conditional on the
    # drain each scheme happened to get), every scheme is measured against the same bound, so
    # eff_fixed may exceed 1 when a scheme kept using an isolated port (see exposure).
    t_ideal_fixed = t_ideal_nodrain
    frows = meta.get("fault_rows") or []
    if len({r[0] for r in frows}) == 1 and any(r[3] in ("ISOLATE", "ISOLATE_NODE") for r in frows):
        shift_ns = meta["fault_ns"] + FIXED_NOTIFY_NS
        shift = dict(meta, fault_ns=shift_ns,
                     fault_rows=[[shift_ns] + list(r[1:]) for r in frows])
        t_ideal_fixed = ideal_makespan_us(shift, flows, ack)
    start, done = read_tasks(case)
    bulk_ids = [f["task"] for f in flows]
    missing = [i for i in bulk_ids if i not in done]
    comp = [done[i] for i in bulk_ids if i in done]
    makespan = max(comp) if comp else float("nan")
    res = dict(case=os.path.basename(os.path.normpath(case)), scenario=meta["scenario"],
               fault=meta["fault"], scheme=meta["scheme"], seed=meta["seed"],
               t_ideal_us=t_ideal, makespan_us=makespan, missing_tasks=len(missing),
               eff=t_ideal / makespan if comp and not missing else 0.0,
               t_ideal_nodrain_us=t_ideal_nodrain, t_ideal_fixed_us=t_ideal_fixed,
               eff_fixed=t_ideal_fixed / makespan if comp and not missing else 0.0,
               eff_nodrain=t_ideal_nodrain / makespan if comp and not missing else 0.0, **drain)
    by_cls = collections.defaultdict(list)
    for f in flows:
        if f["task"] in done:
            by_cls[f["cls"]].append(done[f["task"]])
    res["class_eff"] = {c: t_ideal / max(v) for c, v in by_cls.items()}
    if meta["scenario"] == "incast":
        # Victim bound: the alltoall alone (the incast is capped by SSD0's own 2 ports and shares no
        # bottleneck with it), so victim_eff = T_ideal(a2a alone) / a2a makespan.
        vf = [f for f in flows if f["cls"] == "a2a"]
        vt = [done[f["task"]] for f in vf if f["task"] in done]
        if vt and len(vt) == len(vf):
            res["victim_t_ideal_us"] = ideal_makespan_us(meta, vf, ack, allow or None)
            res["victim_eff"] = res["victim_t_ideal_us"] / max(vt)
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
