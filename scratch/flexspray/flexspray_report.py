#!/usr/bin/env python3
"""Summarize FlexSpray matrix results: efficiency vs. the LP bound and fault bandwidth loss vs. 1/N.

Usage: flexspray_report.py MATRIX_DIR [MATRIX_DIR ...] [--md OUT.md]

Per (scenario, scheme): healthy efficiency (T_ideal / makespan), and for every fault:
  * eff      : T_ideal(fault) / makespan(fault)  (LP accounts for the reduced capacity)
  * loss_win : 1 - BW_fault / BW_healthy of the affected device (tx for near-end faults, rx for
               far-end faults, server-0 devices for a plane failure, all NPUs for a switch port),
               measured in the same post-fault window [t_f + 10us, ...] of both runs.
  * ideal    : the same loss for the LP bound (capacity actually removed) for reference.
  * 1/N      : the requirement threshold.
"""
import argparse
import collections
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402


def load(dirs):
    recs = {}
    for d in dirs:
        f = os.path.join(d, "results.jsonl")
        if not os.path.exists(f):
            continue
        for line in open(f):
            r = json.loads(line)
            r["_dir"] = os.path.join(d, r["case"])
            recs[r["case"]] = r  # last record wins
    return list(recs.values())


def device_rate(case_dir, devs, direction, t0, t1):
    meta, _ = A.load_case(case_dir)
    hosts = {h["id"]: h for h in meta["hosts"]}
    samples = A.read_samples(case_dir)
    idx = 1 if direction == "tx" else 2
    total = 0.0
    for d in devs:
        for port in hosts[d]["ports"].values():
            total += A.window_rate(samples.get((d, port)), t0, t1, idx)
    return total


def affected(meta, fi):
    hosts = meta["hosts"]
    if fi.get("dev") == "server0":
        return [h["id"] for h in hosts if h["server"] == 0 and 3 in h["planes"]], "both"
    if fi.get("dev") is None:
        return [h["id"] for h in hosts if h["type"] == "NPU"], "both"
    return [fi["dev"]], fi.get("dir", "tx")


def rebalance_time_us(case_dir, devs, dirs, tf, post_mean, step=1.0, span=2.0):
    """First time after the fault where the affected devices' 2us-average rate is >= 95% of the
    post-fault steady mean (rate held for 3 consecutive windows)."""
    if post_mean <= 0:
        return None
    t = tf
    ok = 0
    while t < tf + 60:
        r = sum(device_rate(case_dir, devs, d, t, t + span) for d in dirs)
        ok = ok + 1 if r >= 0.95 * post_mean else 0
        if ok >= 3:
            return t - 2 * step - tf
        t += step
    return None


def fault_loss(rec, healthy):
    meta, _ = A.load_case(rec["_dir"])
    fi = meta.get("fault_info") or {}
    devs, direction = affected(meta, fi)
    t0, t1 = rec["windows"]["post"]
    if t1 <= t0 + 5:
        return None
    dirs = ["tx", "rx"] if direction == "both" else [direction]
    bw_f = sum(device_rate(rec["_dir"], devs, d, t0, t1) for d in dirs)
    bw_h = sum(device_rate(healthy["_dir"], devs, d, t0, t1) for d in dirs)
    # Whole-network (all devices, tx) loss in the same window.
    all_devs = [h["id"] for h in meta["hosts"]]
    agg_f = device_rate(rec["_dir"], all_devs, "tx", t0, t1)
    agg_h = device_rate(healthy["_dir"], all_devs, "tx", t0, t1)
    tf = meta["fault_ns"] / 1000.0
    return dict(loss=1 - bw_f / bw_h if bw_h > 0 else float("nan"), bw_f=bw_f, bw_h=bw_h,
                agg_loss=1 - agg_f / agg_h if agg_h > 0 else float("nan"),
                n=fi.get("n"), window=(t0, t1),
                rebalance_us=rebalance_time_us(rec["_dir"], devs, dirs, tf, bw_f))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--md")
    a = ap.parse_args()
    recs = load(a.dirs)
    by = collections.defaultdict(dict)
    for r in recs:
        by[(r["scenario"], r["scheme"], r["seed"])][r["fault"]] = r
    lines = ["| scenario | scheme | seed | fault | eff | makespan us | T_ideal us | dev loss | "
             "net loss | ideal mk loss | 1/N | rebal us | pass |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for (sc, scheme, seed), faults in sorted(by.items()):
        h = faults.get("F0")
        for fault in sorted(faults, key=lambda f: (f != "F0", f)):
            r = faults[fault]
            if r.get("rc") != 0 or "error" in r:
                lines.append(f"| {sc} | {scheme} | {seed} | {fault} | FAIL rc={r.get('rc')} |"
                             " | | | | | | | |")
                continue
            eff = r.get("eff", 0.0)
            loss = ideal = ""
            ninv = ""
            ok = ""
            net = reb = ""
            if fault != "F0" and h is not None and h.get("rc") == 0:
                fl = fault_loss(r, h)
                if fl:
                    loss = f"{fl['loss'] * 100:.2f}%"
                    ninv = f"{100 / fl['n']:.2f}%" if fl["n"] else ""
                    ideal = f"{(1 - h['t_ideal_us'] / r['t_ideal_us']) * 100:.2f}% (mk)"
                    ok = "yes" if fl["n"] and fl["loss"] < 1.0 / fl["n"] else "no"
                    net = f"{fl['agg_loss'] * 100:.2f}%"
                    reb = "" if fl["rebalance_us"] is None else f"{fl['rebalance_us']:.0f}"
            elif fault == "F0":
                target = 0.90 if sc in ("a2a", "mixed") else 0.95
                ok = "yes" if eff >= target else "no"
            lines.append(f"| {sc} | {scheme} | {seed} | {fault} | {eff:.4f} | "
                         f"{r.get('makespan_us', 0):.1f} | {r.get('t_ideal_us', 0):.1f} | {loss} | "
                         f"{net} | {ideal} | {ninv} | {reb} | {ok} |")
    out = "\n".join(lines)
    print(out)
    if a.md:
        open(a.md, "w").write(out + "\n")


if __name__ == "__main__":
    main()
