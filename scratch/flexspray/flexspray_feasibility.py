#!/usr/bin/env python3
"""Is "every device port >= U busy-period utilization" achievable at all for a workload?

Fluid feasibility LP with deadlines. For every device d and direction (tx/rx) with traffic:
    deadline(d, dir) = bits(d, dir) / (U * ceil(d, dir))
where ceil is the payload capacity of the ports the device uses in that direction (wire rate
x MTU/(MTU+78); the reverse-ACK overhead is charged in the capacity rows exactly like the
makespan LP). Busy-period utilization >= U holds iff the device's last flow in that direction
finishes by its deadline. Each flow inherits min(deadline(src, tx), deadline(dst, rx)).
Time-indexed LP over the sorted deadlines: per phase, rates r_fp of flows not yet due, port/L1
capacity rows; each flow must deliver its bits by its deadline. Feasible => some schedule
reaches U on every device simultaneously; infeasible => no scheduler can (the requirement is
not achievable for this workload). We also report the largest feasible U (bisection).

Usage: flexspray_feasibility.py CASE_DIR [--u 0.9]
"""
import argparse
import collections
import os
import sys

import numpy as np
from scipy.optimize import linprog
from scipy.sparse import coo_matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402

DATA_W = (A.MTU + A.DATA_HDR) / A.MTU


def deadlines(meta, flows, u):
    rate = meta["rate_gbps"]
    used = {"tx": collections.defaultdict(set), "rx": collections.defaultdict(set)}
    bits = {"tx": collections.Counter(), "rx": collections.Counter()}
    for f in flows:
        used["tx"][f["src"]].update(f["planes"]); bits["tx"][f["src"]] += f["bits"]
        used["rx"][f["dst"]].update(f["planes"]); bits["rx"][f["dst"]] += f["bits"]
    dl = {}
    for dr in ("tx", "rx"):
        for d, pl in used[dr].items():
            ceil_gbps = len(pl) * rate / DATA_W
            dl[(d, dr)] = bits[dr][d] / 1e3 / (u * ceil_gbps)  # us
    return dl


def feasible(meta, flows, ack, u):
    dl = deadlines(meta, flows, u)
    fdl = [min(dl[(f["src"], "tx")], dl[(f["dst"], "rx")]) for f in flows]
    bps = sorted(set([0.0] + fdl))
    cap, up, down = A.capacities(meta, faulted=False)
    var = {}
    for k in range(len(bps) - 1):
        for fi, f in enumerate(flows):
            if fdl[fi] >= bps[k + 1] - 1e-9:
                for p in f["planes"]:
                    var[(k, fi, p)] = len(var)
    rows, cols, vals, b = [], [], [], []
    for k in range(len(bps) - 1):
        vk = {(fi, p): v for (kk, fi, p), v in var.items() if kk == k}
        idx = sorted({fi for (fi, p) in vk})
        sflows = [flows[fi] for fi in idx]
        svar = {(j, p): vk[(fi, p)] for j, fi in enumerate(idx) for p in flows[fi]["planes"]}
        for terms, c, _ in A._capacity_rows(sflows, cap, up, down, svar, ack):
            r = len(b)
            for v, w in terms:
                rows.append(r); cols.append(v); vals.append(w)
            b.append(c)
    for fi, f in enumerate(flows):
        r = len(b)
        for k in range(len(bps) - 1):
            for p in f["planes"]:
                v = var.get((k, fi, p))
                if v is not None:
                    rows.append(r); cols.append(v); vals.append(-(bps[k + 1] - bps[k]))
        b.append(-f["bits"] / 1e3)
    A_ = coo_matrix((vals, (rows, cols)), shape=(len(b), len(var))).tocsr()
    res = linprog(np.zeros(len(var)), A_ub=A_, b_ub=np.array(b), bounds=[(0, None)] * len(var),
                  method="highs-ipm")
    return res.status == 0, dl


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case")
    ap.add_argument("--u", type=float, default=0.9)
    ap.add_argument("--no-bisect", action="store_true")
    a = ap.parse_args()
    meta, tasks = A.load_case(a.case)
    flows = A.bulk_flows(meta, tasks)
    ok, dl = feasible(meta, flows, meta["ack_bytes"], a.u)
    lo, hi = 0.5, 1.0
    for _ in range(0 if a.no_bisect else 14):
        mid = (lo + hi) / 2
        if feasible(meta, flows, meta["ack_bytes"], mid)[0]:
            lo = mid
        else:
            hi = mid
    t_ideal = A.ideal_makespan_us(meta, flows, meta["ack_bytes"])
    print(f"{os.path.basename(os.path.normpath(a.case))}: U={a.u} feasible={ok}; "
          f"max simultaneous busy utilization U*={'n/a' if a.no_bisect else f'{lo:.3f}'}; "
          f"T_ideal={t_ideal:.1f} us; "
          f"tightest deadline {min(dl.values()):.1f} us")


if __name__ == "__main__":
    main()
