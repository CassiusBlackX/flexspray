#!/usr/bin/env python3
"""Incast (congestion-tree) report, one row per case of scenario `incast`.

  victim_eff        T_ideal(victim alltoall alone) / victim makespan (from results.jsonl)
  t_victim_us       when the last victim task completed
  ssd_util_victim   SSD0 rx payload rate over [0, t_victim] / SSD0 port payload capacity
                    (do the culprits still get the hotspot's bandwidth while victims run?)
  ssd_util_all      same over [0, last incast completion] (incast goodput over its own lifetime)
  incast_frac_at_v  fraction of incast bytes completed (task-completion credit) at t_victim
  incast_eff        (incast bytes / SSD0 rx capacity) / last incast completion
Usage: flexspray_incastreport.py MATRIX_DIR [...]
"""
import argparse
import csv
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402

DATA_W = (4096 + 78) / 4096


def one(case, rec):
    meta, tasks = A.load_case(case)
    _s, done = A.read_tasks(case)
    inc = [t for t in tasks if t["cls"] == "incast"]
    vic = [t for t in tasks if t["cls"] == "a2a"]
    if not inc or not vic:
        return None
    ssd = inc[0]["dst"]
    t_v = max(done.get(t["task"], float("inf")) for t in vic)
    t_i = max(done.get(t["task"], float("inf")) for t in inc)
    rx = {}
    rate = {}
    with open(os.path.join(case, "runlog", "GoodputSample.csv")) as f:
        for r in csv.DictReader(f):
            if int(r["node"]) != ssd:
                continue
            rx.setdefault(r["port"], []).append((int(r["time_ns"]) / 1000.0, int(r["rx_payload_bytes"])))
            rate[r["port"]] = int(r["rate_bps"])

    def util(t1):
        got = cap = 0.0
        for p, s in rx.items():
            if rate[p] <= 0:
                continue
            v = [b for (t, b) in s if t <= t1]
            got += v[-1] if v else 0
            cap += rate[p] / 8 * t1 * 1e-6 / DATA_W
        return got / cap if cap else None

    ib = sum(t["bytes"] for t in inc)
    cap_bps = sum(r for r in rate.values()) / DATA_W
    return dict(case=os.path.basename(case), scheme=meta["scheme"], seed=meta["seed"],
                fault=meta["fault"], victim_eff=rec.get("victim_eff"), t_victim_us=round(t_v, 1),
                ssd_util_victim=round(util(t_v), 4), ssd_util_all=round(util(t_i), 4),
                incast_frac_at_v=round(sum(t["bytes"] for t in inc if done.get(t["task"], 1e18) <= t_v) / ib, 4),
                t_incast_us=round(t_i, 1), incast_eff=round(ib * 8 / cap_bps * 1e6 / t_i, 4))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    a = ap.parse_args()
    for d in a.dirs:
        recs = {}
        for l in open(os.path.join(d, "results.jsonl")):
            r = json.loads(l)
            recs[r["case"]] = r
        for c, r in sorted(recs.items()):
            if r.get("scenario") != "incast":
                continue
            row = one(os.path.join(d, c), r)
            if row:
                print(json.dumps(row))


if __name__ == "__main__":
    main()
