#!/usr/bin/env python3
"""Latency-aware fluid lower bound for one phase (round 13): time-slotted LP with per-flow delays.

The plain fluid LP lets every receiver start at full rate at t = 0. In reality a byte sent at t
reaches the receiver at t + delta_f and the sender learns it is done at t + alpha_f
(alpha_f = unloaded single-packet completion minus one packet serialisation, see
flexspray_dagbound.py; delta_f = alpha_f / 2, rounded down to the slot grid).
Model (healthy, device-level capacities = planes x rate; per-plane and L1 aggregates relaxed):
  x[f,k] >= 0   payload bits of flow f put on the wire by its sender in slot k (width dt)
  sum_k x[f,k] = bits_f
  egress(d,k):  w * sum_{f: src=d} x[f,k] + a * sum_{f: dst=d} x[f, k - s_f]       <= cap_d dt
  ingress(d,k): w * sum_{f: dst=d} x[f, k - s_f] + a * sum_{f: src=d} x[f, k - 2 s_f] <= cap_d dt
  (w = (MTU+78)/MTU data overhead, a = ACK bytes / MTU on the reverse direction)
  completion:   x[f,k] = 0 unless k dt + alpha_f <= T     (bytes of slot k may leave at its start)
Every rounding is in the relaxing direction, so the minimum feasible T (bisection) is a valid lower
bound on the phase duration of any schedule. Usage (library): shift_bound_us(meta, flows).
CLI: flexspray_shiftlp.py CASE PHASE [PHASE ...]
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1")
import sys

import numpy as np
import scipy.sparse as sp
from scipy.optimize import linprog

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402
from flexspray_dagbound import ALPHA_US, alone_us  # noqa: E402


def feasible(meta, flows, T, dt):
    w = (A.MTU + A.DATA_HDR) / A.MTU
    a = meta["ack_bytes"] / A.MTU
    hosts = {h["id"]: h for h in meta["hosts"]}
    K = int(np.floor(T / dt + 1e-9)) + 1
    devs = sorted({f["src"] for f in flows} | {f["dst"] for f in flows})
    di = {d: i for i, d in enumerate(devs)}
    cap = np.array([len(hosts[d]["planes"]) * meta["rate_gbps"] * 1e3 * dt for d in devs])
    var = []  # (f, k)
    for fi, f in enumerate(flows):
        kmax = int(np.floor((T - ALPHA_US[f["cross"]]) / dt + 1e-9))
        if kmax < 0:
            return False
        for k in range(min(kmax, K - 1) + 1):
            var.append((fi, k))
    nv = len(var)
    s = [int(np.floor(ALPHA_US[f["cross"]] / 2 / dt + 1e-9)) for f in flows]
    nd = len(devs)
    # rows: egress(d,k) = d*K + k ; ingress(d,k) = nd*K + d*K + k
    r, c, v = [], [], []
    for j, (fi, k) in enumerate(var):
        f = flows[fi]
        src, dst = di[f["src"]], di[f["dst"]]
        ks = k + s[fi]
        k2 = k + 2 * s[fi]
        r.append(src * K + k); c.append(j); v.append(w)
        if ks < K:
            r.append(nd * K + dst * K + ks); c.append(j); v.append(w)
            r.append(dst * K + ks); c.append(j); v.append(a)
        if k2 < K:
            r.append(nd * K + src * K + k2); c.append(j); v.append(a)
    Aub = sp.csr_matrix((v, (r, c)), shape=(2 * nd * K, nv))
    bub = np.concatenate([np.repeat(cap, K), np.repeat(cap, K)])
    er, ec = zip(*[(fi, j) for j, (fi, k) in enumerate(var)])
    Aeq = sp.csr_matrix((np.ones(nv), (er, ec)), shape=(len(flows), nv))
    beq = np.array([f["bits"] for f in flows])
    res = linprog(np.zeros(nv), A_ub=Aub, b_ub=bub, A_eq=Aeq, b_eq=beq, bounds=(0, None),
                  method=os.environ.get("FLEX_SHIFT_METHOD", "highs-ipm"))
    return res.status == 0


def shift_bound_us(meta, flows, lo=None, hi=None, dt=0.1, tol=0.05):
    if lo is None:
        lo = max(alone_us(meta, f) + ALPHA_US[f["cross"]] for f in flows) * 0.99
    if hi is None:
        hi = lo * 3
    while not feasible(meta, flows, hi, dt):
        lo, hi = hi, hi * 1.5
    while hi - lo > tol:
        mid = (lo + hi) / 2
        if feasible(meta, flows, mid, dt):
            hi = mid
        else:
            lo = mid
    return lo  # largest infeasible T: a valid lower bound (within the slot relaxation)


def main():
    case = sys.argv[1]
    meta, tasks = A.load_case(case)
    flows = {f["task"]: f for f in A.bulk_flows(meta, tasks)}
    for p in map(int, sys.argv[2:]):
        fl = [flows[t["task"]] for t in tasks if t.get("phase") == p and t["task"] in flows]
        print(p, fl[0]["cls"], "%.3f" % shift_bound_us(meta, fl), flush=True)


if __name__ == "__main__":
    main()
