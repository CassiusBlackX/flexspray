#!/usr/bin/env python3
"""Joint feasibility of the single-fault loss requirement (fluid LP, maximise the common margin).

Question: is there ANY schedule under which every affected device d (and direction) keeps at least
(1 - 1/N_d + eps) of its healthy goodput at the same time?  We maximise eps.

Model (same as flexspray_analyze's makespan LP):
  * flows = bulk tasks (READ counted at its data source), each restricted to the planes its two
    endpoints share; rates r[k, f, p] per phase k;
  * capacity rows per phase: device port egress / ingress (data x (MTU+hdr)/MTU, reverse ACK x
    ack/MTU), L1 uplink / downlink aggregates; healthy capacities before t_f, faulted after (a
    graceful isolation is charged from t_f, like a hard down - this only makes the target harder
    for the optimiser by at most the 2 us drain, see --notify-us);
  * a constrained device-direction (d, dir) with healthy completion T_H must have all its flows
    (bits) delivered by  D = T_H / (1 - 1/N + eps)  (goodput = bytes / completion time);
  * flows of unaffected devices get NO deadline (the requirement says nothing about them), the most
    permissive relaxation: if the problem is infeasible here it is infeasible for every scheduler.

Healthy reference T_H (--healthy): `measured` = the device's last completion in the healthy run of
the same scheme/seed (the requirement's reference), or `ideal` = the device's minimum completion
time in the healthy network when it alone is constrained (an LP, the most demanding reference).
N (--n): `phys` = the device's physical planes; `shared` = planes the device shares with the peers
that carry the constrained traffic (max over its peers of |planes(d) & planes(peer)|).

Output per case: eps* (bisection, tolerance 1e-4), per-device single-device eps (each alone), and the
binding resources at eps* (positive duals of a max-scale LP: these form the capacity cut).
Usage: flexspray_jointfeas.py FAULT_CASE HEALTHY_CASE [--healthy measured|ideal] [--n phys|shared]
"""
import argparse
import collections
import json
import os
import sys

import numpy as np
from scipy.optimize import linprog
from scipy.sparse import coo_matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402
import flexspray_lossreport as L  # noqa: E402


def lp(meta, flows, deadline, tf, horizon, ack, scale_obj=False):
    """Feasibility of: every flow with a deadline delivers bits by it (time-indexed LP).
    scale_obj: maximise s with deliveries >= s * bits (s* >= 1 <=> feasible); returns (s, res, rowinfo)."""
    bps = sorted({0.0, horizon} | ({tf} if tf is not None else set()) |
                 {dl for dl in deadline.values() if dl < horizon} | {f["release_us"] for f in flows})
    caps_h = A.capacities(meta, faulted=False)
    caps_f = A.capacities(meta, faulted=True) if tf is not None else caps_h
    var = {}
    for k in range(len(bps) - 1):
        for fi, f in enumerate(flows):
            if fi in deadline and f["release_us"] <= bps[k] + 1e-9 and deadline[fi] >= bps[k + 1] - 1e-9:
                for p in f["planes"]:
                    var[(k, fi, p)] = len(var)
    si = len(var)
    rows, cols, vals, b, info = [], [], [], [], []
    for k in range(len(bps) - 1):
        cap, up, down = caps_f if (tf is not None and bps[k] >= tf - 1e-9) else caps_h
        vk = {(fi, p): v for (kk, fi, p), v in var.items() if kk == k}
        idx = sorted({fi for (fi, p) in vk})
        sflows = [flows[fi] for fi in idx]
        svar = {(j, p): vk[(fi, p)] for j, fi in enumerate(idx) for p in flows[fi]["planes"]}
        groups = {"eg": collections.defaultdict(list), "ing": collections.defaultdict(list),
                  "l1u": collections.defaultdict(list), "l1d": collections.defaultdict(list)}
        data_w = (A.MTU + A.DATA_HDR) / A.MTU
        ack_w = ack / A.MTU
        for j, f in enumerate(sflows):
            for p in f["planes"]:
                v = svar[(j, p)]
                groups["eg"][(f["src"], p)].append((v, data_w)); groups["ing"][(f["src"], p)].append((v, ack_w))
                groups["ing"][(f["dst"], p)].append((v, data_w)); groups["eg"][(f["dst"], p)].append((v, ack_w))
                if f["cross"]:
                    groups["l1u"][(p, f["ssrv"])].append((v, data_w)); groups["l1d"][(p, f["ssrv"])].append((v, ack_w))
                    groups["l1d"][(p, f["dsrv"])].append((v, data_w)); groups["l1u"][(p, f["dsrv"])].append((v, ack_w))
        for name, capd in (("eg", cap), ("ing", cap), ("l1u", up), ("l1d", down)):
            for key, terms in groups[name].items():
                r = len(b)
                for v, w in terms:
                    rows.append(r); cols.append(v); vals.append(w)
                b.append(capd.get(key, 0.0))
                info.append((k, bps[k], bps[k + 1], name, key, capd.get(key, 0.0)))
    for fi in deadline:
        r = len(b)
        f = flows[fi]
        for k in range(len(bps) - 1):
            for p in f["planes"]:
                v = var.get((k, fi, p))
                if v is not None:
                    rows.append(r); cols.append(v); vals.append(-(bps[k + 1] - bps[k]))
        if scale_obj:
            rows.append(r); cols.append(si); vals.append(f["bits"] / 1e3); b.append(0.0)
        else:
            b.append(-f["bits"] / 1e3)
        info.append(("deliver", fi))
    nv = si + (1 if scale_obj else 0)
    Am = coo_matrix((vals, (rows, cols)), shape=(len(b), max(nv, 1))).tocsr()
    c = np.zeros(max(nv, 1))
    if scale_obj:
        c[si] = -1.0
    res = linprog(c, A_ub=Am, b_ub=np.array(b), bounds=[(0, None)] * max(nv, 1), method="highs")
    s = res.x[si] if (scale_obj and res.status == 0) else None
    return (res.status == 0), s, res, info


def setup(case_f, case_h, healthy, nmode):
    meta, tasks = A.load_case(case_f)
    flows = A.bulk_flows(meta, tasks)
    _s, done_h = A.read_tasks(case_h)
    hosts = {h["id"]: h for h in meta["hosts"]}
    aff = L.affected(meta)
    cons = {}  # (dev, dir) -> (T_H, N, flow indices)
    for dev in aff:
        for dr in ("tx", "rx"):
            fis = [fi for fi, f in enumerate(flows) if (f["src"] if dr == "tx" else f["dst"]) == dev]
            if not fis:
                continue
            th = max(done_h[flows[fi]["task"]] for fi in fis)
            peers = {(flows[fi]["dst"] if dr == "tx" else flows[fi]["src"]) for fi in fis}
            nphys = len(hosts[dev]["planes"])
            nshared = max(len(set(hosts[dev]["planes"]) & set(hosts[q]["planes"])) for q in peers)
            cons[(dev, dr)] = [th, nphys if nmode == "phys" else nshared, fis]
    return meta, flows, cons


def ideal_healthy(meta, flows, cons, ack):
    """Per constrained (d, dir): the minimum completion time in the healthy network (bisection)."""
    out = {}
    for key, (th, n, fis) in cons.items():
        lo, hi = 0.0, th * 2
        for _ in range(40):
            mid = (lo + hi) / 2
            ok, *_ = lp(meta, flows, {fi: mid for fi in fis}, None, hi * 4, ack)
            lo, hi = (lo, mid) if ok else (mid, hi)
        out[key] = hi
    return out


def deadlines_for(cons, eps, only=None):
    dl = {}
    for key, (th, n, fis) in cons.items():
        if only is not None and key != only:
            continue
        d = th / (1 - 1.0 / n + eps)
        for fi in fis:
            dl[fi] = min(dl.get(fi, float("inf")), d)
    return dl


def max_eps(meta, flows, cons, tf, ack, only=None):
    horizon = max(c[0] for c in cons.values()) * 50
    keys = [only] if only is not None else list(cons)
    nmin = min(cons[k][1] for k in keys)
    lo, hi = max(-0.5, -(1 - 1.0 / nmin) + 1e-3), 0.5  # keep 1 - 1/N + eps > 0
    if not lp(meta, flows, deadlines_for(cons, lo, only), tf, horizon, ack)[0]:
        return float("-inf")
    for _ in range(30):
        mid = (lo + hi) / 2
        if lp(meta, flows, deadlines_for(cons, mid, only), tf, horizon, ack)[0]:
            lo = mid
        else:
            hi = mid
    return lo


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fault_case"); ap.add_argument("healthy_case")
    ap.add_argument("--healthy", choices=["measured", "ideal"], default="measured")
    ap.add_argument("--n", choices=["phys", "shared"], default="phys")
    a = ap.parse_args()
    meta, flows, cons = setup(a.fault_case, a.healthy_case, a.healthy, a.n)
    ack = meta["ack_bytes"]
    tf = meta["fault_ns"] / 1000.0
    if a.healthy == "ideal":
        for key, t in ideal_healthy(meta, flows, cons, ack).items():
            cons[key][0] = t
    eps = max_eps(meta, flows, cons, tf, ack)
    single = {f"{k[0]}{k[1]}": round(max_eps(meta, flows, cons, tf, ack, only=k), 4) for k in cons}
    # binding resources at eps*: max-scale LP, positive duals of capacity rows
    horizon = max(c[0] for c in cons.values()) * 50
    ok, s, res, info = lp(meta, flows, deadlines_for(cons, eps), tf, horizon, ack, scale_obj=True)
    cut = []
    if res.status == 0 and hasattr(res, "ineqlin"):
        duals = -np.asarray(res.ineqlin.marginals)
        for i, dv in enumerate(duals):
            if dv > 1e-9 and info[i][0] != "deliver":
                k, t0, t1, name, key, capv = info[i]
                cut.append(dict(phase=[round(t0, 2), round(t1, 2)], resource=name, key=list(key),
                                cap_gbps=capv, dual=round(float(dv), 6)))
    hosts = {h["id"]: h for h in meta["hosts"]}
    out = dict(fault_case=os.path.basename(a.fault_case.rstrip("/")), healthy=a.healthy, n=a.n,
               tf_us=tf, eps_star=round(eps, 5),
               strict_feasible=eps > 1e-4, le_feasible=eps > -1e-4,
               constraints={f"{d}{dr}": dict(type=hosts[d]["type"], T_H_us=round(v[0], 3), N=v[1],
                                             bits=sum(flows[fi]["bits"] for fi in v[2]),
                                             peers=sorted({(flows[fi]['dst'] if dr == 'tx' else flows[fi]['src']) for fi in v[2]}))
                            for (d, dr), v in cons.items()},
               single_device_eps=single, scale_at_eps=s, binding=cut[:40])
    print(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
