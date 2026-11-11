#!/usr/bin/env python3
"""Single-fault loss under every reading required by REQUESTS (CLAUDE.md section 4), round 11.

For each fault case paired with the healthy (F0) case of the same set / scenario / scheme / seed,
and for each damaged device d (lane / device-port faults: the device of the faulted port; L1 node
faults: every device of that server with a port on that plane; L1 uplink faults: same scope), it
reports:

windows (loss = 1 - bandwidth(fault) / bandwidth(healthy), device bandwidth = payload bytes on all
ports of the device, received + sent, from the 1 us GoodputSample port counters; see device_series):
  steady   [t_f + T_rebal, min(t80_healthy, t80_fault)], t80 = time the device reached 80 % of its
           bytes; "n/a" if shorter than 10 us (the job is too short to have a post-rebalance
           steady state - reported, not stretched);
  tdev     1 - T_dev(F0) / T_dev(fault), T_dev = last completion of a task of d (completion window);
  trans    [t_f, t_f + 10 us].
T_rebal    smallest tau >= 0 such that the 5 us rate of d stays >= 95 % of its post-fault steady
           level (rate over [t_f + 20 us, t80_fault]) on [t_f + tau, t80_fault] (the plateau ends before the 90 % point when some of the device's flows finish early).
N          N_dev = planes of d; 4N = total lanes of d; Nscope = planes d's flows can use (shared with
           their peers); LP = per-device deadline LP lower bound on the tdev loss
           (flexspray_devgate_lp.py logic, healthy reference = measured T_dev(F0)).
Pass marks for each N: loss < 1/N (strict, the original relation) and, extra, loss <= 1/N.
For LP: margin = loss - LP lower bound (>= 0 always; physically infeasible iff the LP bound itself
is >= 1/N, reported as lp_infeasible_<N>).
Fault semantics: graceful isolation (ISOLATE*), hard down (DOWN*), lane degrade (DEGRADE).

Usage: flexspray_lossmatrix.py OUT.jsonl DIR [DIR ...] [--jobs N] [--no-lp]
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1")
import argparse
import bisect
import collections
import concurrent.futures as cf
import csv
import glob
import json
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402


def device_series(case, dev):
    """[(t_ns, cumulative payload bytes received + sent on all ports of node dev)].

    Port payload counters, not the node_biz counters: those advance only when whole messages
    complete (0 -> 17 Tbps swings in 5 us windows). Receive bytes count each PSN once (duplicates
    are dropped before counting); transmit bytes include retransmissions, so a scheme with many
    spurious retransmissions (B0) looks slightly better in the windowed losses than it is."""
    acc = collections.defaultdict(int)
    with open(os.path.join(case, "runlog", "GoodputSample.csv")) as f:
        for r in csv.DictReader(f):
            if int(r["node"]) == dev:
                acc[int(r["time_ns"])] += int(r["rx_payload_bytes"]) + int(r["tx_payload_bytes"])
    return sorted(acc.items())


def at(series, t):
    """Cumulative bytes at time t (step interpolation)."""
    ts = [x[0] for x in series]
    i = bisect.bisect_right(ts, t) - 1
    return series[i][1] if i >= 0 else 0


def t_reach(series, b):
    for t, v in series:
        if v >= b:
            return t
    return series[-1][0] if series else 0


def rate(series, t0, t1):
    return (at(series, t1) - at(series, t0)) * 8.0 / max(t1 - t0, 1)  # Gbps (bytes/ns * 8)


def semantics(meta):
    acts = {r[3] for r in meta.get("fault_rows") or []}
    if acts & {"DEGRADE"}:
        return "lane"
    if acts & {"DOWN", "DOWN_NODE"}:
        return "hard"
    return "graceful"


def damaged(meta):
    hosts = {h["id"]: h for h in meta["hosts"]}
    l1 = {v: tuple(map(int, k.split(","))) for k, v in meta["l1"].items()}
    out = set()
    for (_t, node, port, action, _v) in meta.get("fault_rows") or []:
        if node in hosts:
            out.add(node)
        elif node in l1:
            p, s = l1[node]
            out |= {h["id"] for h in meta["hosts"] if h["server"] == s and p in h["planes"]}
    return sorted(out)


def lane_k(meta):
    for r in meta.get("fault_rows") or []:
        if r[3] == "DEGRADE":
            num, den = map(int, r[4].split("/"))
            return round(4 * (1 - num / den))
    return 0


def lp_bound(fault_case, t_ref, dev, meta, flows, tasks_f):
    import flexspray_devgate_lp as D
    fis = D.dev_flows(flows, dev)
    if not fis:
        return None
    _s, done_f = A.read_tasks(fault_case)
    hi = max(t_ref, max(done_f.get(flows[fi]["task"], 0) for fi in fis)) * 4
    t_lp = D.min_completion(meta, flows, fis, meta["fault_ns"] / 1000.0, meta["ack_bytes"], hi)
    return 1 - t_ref / t_lp if t_lp < float("inf") else 1.0


def one(job):
    key, fcase, hcase, use_lp = job
    meta, tasks = A.load_case(fcase)
    hosts = {h["id"]: h for h in meta["hosts"]}
    tf = meta["fault_ns"]
    _s, done_f = A.read_tasks(fcase)
    _s, done_h = A.read_tasks(hcase)
    flows = A.bulk_flows(meta, tasks)
    rows = []
    for dev in damaged(meta):
        mine = [t["task"] for t in tasks if dev in (t["src"], t["dst"])]
        if not mine:
            continue
        missing = sum(1 for t in mine if t not in done_f)
        tdh = max(done_h.get(t, 0) for t in mine) * 1000
        tdf = max(done_f.get(t, 0) for t in mine) * 1000
        sh, sf = device_series(hcase, dev), device_series(fcase, dev)
        tot = max(sh[-1][1] if sh else 0, 1)
        t90 = min(t_reach(sh, 0.8 * tot), t_reach(sf, 0.8 * tot))
        # rebalance time
        lvl0 = tf + 20000
        trebal = None
        if t_reach(sf, 0.8 * tot) - lvl0 >= 10000:
            t90f = t_reach(sf, 0.8 * tot)
            level = rate(sf, lvl0, t90f)
            trebal = 0
            t = tf
            while t + 5000 <= t90f:
                if rate(sf, t, t + 5000) < 0.95 * level:
                    trebal = t + 5000 - tf
                t += 1000
        w0 = tf + (trebal or 0)
        steady = None
        if trebal is not None and t90 - w0 >= 10000:
            rh, rf = rate(sh, w0, t90), rate(sf, w0, t90)
            steady = 1 - rf / rh if rh > 0 else None
        rh, rf = rate(sh, tf, tf + 10000), rate(sf, tf, tf + 10000)
        trans = 1 - rf / rh if rh > 0 else None
        tdev = (1 - tdh / tdf) if (missing == 0 and tdf > 0) else None
        ndev = len(hosts[dev]["planes"])
        scope = len({p for f in flows if dev in (f["src"], f["dst"]) for p in f["planes"]})
        lpb = None
        if use_lp and missing == 0:
            try:
                lpb = lp_bound(fcase, tdh / 1000.0, dev, meta, flows, tasks)
            except Exception as e:  # noqa: BLE001
                lpb = None
        rows.append(dict(key=key, dev=dev, type=hosts[dev]["type"], sem=semantics(meta), lanes_lost=lane_k(meta),
                         missing=missing, N_dev=ndev, N_4N=4 * ndev, N_scope=scope,
                         loss_steady=steady, loss_tdev=tdev, loss_trans=trans,
                         t_rebal_us=None if trebal is None else trebal / 1000.0,
                         window_us=(t90 - w0) / 1000.0 if trebal is not None else None,
                         lp_lb=lpb))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("dirs", nargs="+")
    ap.add_argument("--jobs", type=int, default=48); ap.add_argument("--no-lp", action="store_true")
    a = ap.parse_args()
    rows = {}
    for d in a.dirs:
        ds = os.path.basename(d.rstrip("/"))
        for l in open(os.path.join(d, "results.jsonl")):
            r = json.loads(l)
            rows[(ds, r["scenario"], r["fault"], r["scheme"], r["seed"])] = os.path.join(d, r["case"])
    def params(case):
        m = json.load(open(os.path.join(case, "case_meta.json")))
        return (m.get("scale"), m.get("links_per_pair"), m.get("traffic_map"), m.get("seed"),
                m.get("scenario"), m.get("scheme"), m.get("servers"), m.get("planes"))
    healthy = collections.defaultdict(list)  # params -> F0 cases (any set)
    for (ds, sc, f, sch, sd), case in rows.items():
        if f == "F0":
            healthy[params(case)].append(case)
    jobs = []
    for (ds, sc, f, sch, sd), case in rows.items():
        if f == "F0":
            continue
        # healthy twin: same set first, else any set whose case parameters are identical
        h = rows.get((ds, sc, "F0", sch, sd)) or next(iter(healthy.get(params(case), [])), None)
        if h:
            jobs.append(((ds, sc, f, sch, sd), case, h, not a.no_lp))
    with cf.ProcessPoolExecutor(a.jobs) as ex, open(a.out, "w") as fo:
        for res in ex.map(one, jobs, chunksize=1):
            for r in res:
                fo.write(json.dumps(r) + "\n")
    print(len(jobs), "cases")


if __name__ == "__main__":
    main()
