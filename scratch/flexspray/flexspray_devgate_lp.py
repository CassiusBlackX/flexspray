#!/usr/bin/env python3
"""Per-device deadline LP for the single-fault gates (round 10).

Question: can ANY schedule finish every task of the damaged device d (as source or destination)
by the gate's deadline, given the fault?  Only d's flows get a deadline; all other flows get none
(the most permissive relaxation: if this is infeasible, no scheduler meets d's gate, whatever the
rest of the network does).  The schedule is clairvoyant (it may favour d's flows before t_f), so
infeasibility is a proof; feasibility is only "not excluded".

Two gates are evaluated side by side (EVAL_PROTOCOL round 10):
  * original  : loss < 1/N, N = d's physical planes          -> T_dev(fault) < T_ref / (1 - 1/N)
  * engineered: loss <= removed fraction + 0.5 pp (lane gate) -> T_dev(fault) <= T_ref / (1 - k/(N*lanes) - 0.005)
with loss = 1 - T_ref / T_dev(fault).

T_ref (healthy reference, --ref):
  measured = last completion of d's tasks in the healthy run of the same scheme (the gate's
             definition; scheme-dependent);
  ideal    = the minimum T_dev in the healthy network with only d constrained (scheme-independent,
             the most demanding reference).
Also reported: T_lp_min = the minimum T_dev achievable under the fault (bisection), the binding
resources (positive duals) at T_lp_min, i.e. the capacity cut, and the loss lower bound
1 - T_ref / T_lp_min.

Usage: flexspray_devgate_lp.py FAULT_CASE HEALTHY_CASE [--ref measured|ideal|both]
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402
import flexspray_jointfeas as J  # noqa: E402


def dev_flows(flows, dev):
    return [fi for fi, f in enumerate(flows) if dev in (f["src"], f["dst"])]


def min_completion(meta, flows, fis, tf, ack, hi):
    """Smallest D such that all flows fis can be delivered by D (bisection on the LP)."""
    lo = max(f["release_us"] for f in (flows[i] for i in fis))
    if not J.lp(meta, flows, {fi: hi for fi in fis}, tf, hi * 4, ack)[0]:
        return float("inf")
    for _ in range(36):
        mid = (lo + hi) / 2
        if J.lp(meta, flows, {fi: mid for fi in fis}, tf, hi * 4, ack)[0]:
            hi = mid
        else:
            lo = mid
    return hi


def cut_at(meta, flows, fis, tf, ack, d):
    ok, s, res, info = J.lp(meta, flows, {fi: d for fi in fis}, tf, d * 4, ack, scale_obj=True)
    cut = []
    if res.status == 0 and hasattr(res, "ineqlin"):
        duals = -np.asarray(res.ineqlin.marginals)
        for i, dv in enumerate(duals):
            if dv > 1e-9 and info[i][0] != "deliver":
                k, t0, t1, name, key, capv = info[i]
                cut.append(dict(phase=[round(t0, 2), round(t1, 2)], res=name, key=list(key),
                                cap_gbps=round(capv, 1), dual=round(float(dv), 5)))
    cut.sort(key=lambda c: -c["dual"])
    return s, cut


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fault_case"); ap.add_argument("healthy_case")
    ap.add_argument("--ref", choices=["measured", "ideal", "both"], default="both")
    a = ap.parse_args()
    meta, tasks = A.load_case(a.fault_case)
    flows = A.bulk_flows(meta, tasks)
    ack = meta["ack_bytes"]
    tf = meta["fault_ns"] / 1000.0
    fi_info = meta["fault_info"]
    dev = fi_info["dev"]
    hosts = {h["id"]: h for h in meta["hosts"]}
    nphys = len(hosts[dev]["planes"])
    lanes = meta.get("lanes", 4)
    k = fi_info.get("lanes_lost") or {"F3near": 1, "F3far": 1, "F3near2": 2, "F3far2": 2,
                                      "F3near3": 3, "F3far3": 3}.get(meta.get("fault"), 0)
    fis = dev_flows(flows, dev)
    _s, done_f = A.read_tasks(a.fault_case)
    _s, done_h = A.read_tasks(a.healthy_case)
    t_meas_h = max(done_h[flows[fi]["task"]] for fi in fis)
    t_meas_f = max(done_f.get(flows[fi]["task"], float("inf")) for fi in fis)
    hi = max(t_meas_h, t_meas_f) * 4
    t_ideal_h = min_completion(meta, flows, fis, None, ack, hi)
    t_lp_f = min_completion(meta, flows, fis, tf, ack, hi)
    peers = sorted({(flows[fi]["dst"] if flows[fi]["src"] == dev else flows[fi]["src"]) for fi in fis})
    shared = sorted({p for fi in fis for p in flows[fi]["planes"]})
    allowed = {"orig_1/N": 1.0 / nphys,
               "lane_gate": (k / (nphys * lanes) + 0.005) if k else None}
    refs = {"measured": t_meas_h, "ideal": t_ideal_h}
    out = dict(case=os.path.basename(a.fault_case.rstrip("/")), dev=dev, type=hosts[dev]["type"],
               N_phys=nphys, planes_used_by_dev_flows=shared, peers=peers, lanes_lost=k,
               tf_us=tf, T_meas_F0=round(t_meas_h, 3), T_meas_fault=round(t_meas_f, 3),
               T_ideal_F0=round(t_ideal_h, 3), T_lp_min_fault=round(t_lp_f, 3), gates={})
    for rname, tref in refs.items():
        if a.ref != "both" and a.ref != rname:
            continue
        for gname, al in allowed.items():
            if al is None:
                continue
            treq = tref / (1 - al)
            out["gates"][f"{rname}/{gname}"] = dict(
                allowed_pct=round(100 * al, 3), T_req=round(treq, 3),
                feasible=t_lp_f <= treq + 1e-6,
                loss_lb_pct=round(100 * (1 - tref / t_lp_f), 3),
                loss_meas_pct=round(100 * (1 - tref / t_meas_f), 3),
                meas_pass=t_meas_f <= treq)
    s, cut = cut_at(meta, flows, fis, tf, ack, t_lp_f * 0.999)
    out["cut_at_T_lp_min"] = cut[:12]
    print(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
