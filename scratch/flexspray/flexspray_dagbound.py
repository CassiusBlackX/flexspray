#!/usr/bin/env python3
"""Latency-inclusive lower bounds for multi-phase (benchmark v1) healthy runs (round 13).

Why: the per-phase fluid LP of round 12 is a pure bandwidth bound. Short phases (a 1 MiB ring step
takes ~2.7 us at line rate) cannot reach it because every message also needs the unloaded path
latency. This evaluator adds the measured unavoidable latency and evaluates requirement 1/2 against

  alpha(path)   = unloaded completion time of a single 4 KB message minus the serialisation of that
                  one packet at the bottleneck (it is already counted by the bandwidth term).
                  Measured on the simulator (work/exp/round13/lat, frozen r12pgo): intra-server
                  0.455 us, cross-server 1.429 us (WRITE and READ identical); serialisation of a
                  4096 + 78 B packet at 400 Gbps = 0.0835 us. Check: a lone 1 MiB cross-server WRITE
                  completes in 4.022 us = 2.677 us (8 planes, header overhead) + 1.345 us.
  phase bound   B_p = max( LP_p + min_f alpha_f , max_f (alone_f + alpha_f) )
                  LP_p    = fluid LP of the phase's flows alone, released together (headers + ACKs);
                  alone_f = flow f alone on its common planes at line rate (headers only).
                  Valid: the bytes at the LP's bottleneck need the in/out path latency of at least
                  one packet; and no flow finishes faster than alone.
  DAG bound     EF_p = max( max_f (EF(deps) + delay_f + alone_f + alpha_f),
                            EF(deps) + min_f delay_f + B_p )      (earliest finish, from t = 0)
                  Ignores contention between phases, so it is a valid lower bound.
  device bound  B_dev = max( max_{p involves dev} EF_p,
                             max_t [ t + bytes released at or after t (by the tasks' earliest
                                     starts) * hdr / cap(dev) + alpha_min ] )  per direction
                  (the backlog bound: bursty releases such as KV requests at one trace time).

Outputs per case: per phase (cls, D, LP, B, eff = B/D, overlap = fraction of the phase's window
during which another bulk phase is active), per device (T_dev, B_dev, eff_total = B_dev/T_dev,
eff_comm = (B_dev - fixed delays on the critical chain)/(T_dev - same)), and the whole job.
A phase whose window overlaps other bulk phases is marked: its isolated bound ignores contention
that a fair scheme must accept, so eff < 0.9 there is not a verdict by itself.

Usage: flexspray_dagbound.py OUT.jsonl CASE_DIR [CASE_DIR ...] [--jobs N]
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1")
import argparse
import collections
import concurrent.futures as cf
import copy
import json
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402

ALPHA_US = {False: 0.4550 - 0.0835, True: 1.4288 - 0.0835}  # same server / cross server


def alone_us(meta, f):
    rate = meta["rate_gbps"] * 1e3  # bits per us per plane
    return f["bits"] * (A.MTU + A.DATA_HDR) / A.MTU / (len(f["planes"]) * rate)


def evaluate(case):
    meta, tasks = A.load_case(case)
    start, done = A.read_tasks(case)
    flows = {f["task"]: f for f in A.bulk_flows(meta, tasks)}
    ph = collections.defaultdict(list)
    for t in tasks:
        if t.get("phase", 0) >= 1 and t["cls"] != "ctrl":
            ph[t["phase"]].append(t)
    deps = {p: sorted({d for t in ts for d in t.get("deps", [])}) for p, ts in ph.items()}
    order = sorted(ph)  # deps always point to lower phase numbers in flexspray_bench
    EF, ES, B, LP, delay_sum = {}, {}, {}, {}, {}
    for p in order:
        fl = [flows[t["task"]] for t in ph[p]]
        al = [ALPHA_US[f["cross"]] for f in fl]
        lp = A.ideal_makespan_timeindexed_us(
            copy.deepcopy(meta), [dict(f, release_us=0.0) for f in fl], meta["ack_bytes"], None,
            with_fault=False)
        B[p] = max(lp + min(al), max(alone_us(meta, f) + a for f, a in zip(fl, al)))
        LP[p] = lp
        base = max((EF[d] for d in deps[p]), default=0.0)
        dl = [t.get("delay_ns", 0) / 1000.0 for t in ph[p]]
        ES[p] = base + min(dl)
        EF[p] = max(max(base + d + alone_us(meta, f) + a for d, f, a in zip(dl, fl, al)),
                    ES[p] + B[p])
        prev = max(deps[p], key=lambda d: EF[d]) if deps[p] else None
        delay_sum[p] = (delay_sum[prev] if prev is not None else 0.0) + min(dl)
    # measured phase windows
    win = {}
    for p in order:
        ids = [t["task"] for t in ph[p]]
        if all(i in start and i in done for i in ids):
            win[p] = (min(start[i] for i in ids), max(done[i] for i in ids))
    rows = []
    for p in order:
        if p not in win:
            rows.append(dict(kind="phase", phase=p, cls=ph[p][0]["cls"], missing=True))
            continue
        r, c = win[p]
        D = c - r
        # overlap with other bulk phases
        segs = sorted((max(r, a), min(c, b)) for q, (a, b) in win.items()
                      if q != p and b > r and a < c)
        cov, cur = 0.0, r
        for a, b in segs:
            if b > cur:
                cov += b - max(a, cur)
                cur = b
        rows.append(dict(kind="phase", phase=p, cls=ph[p][0]["cls"], D=D, LP=LP[p], B=B[p],
                         eff=B[p] / D if D > 0 else None, eff_lp=LP[p] / D if D > 0 else None,
                         overlap=cov / D if D > 0 else 0.0, n=len(ph[p])))
    # devices
    hosts = {h["id"]: h for h in meta["hosts"]}
    dev_ph = collections.defaultdict(set)
    dev_bits = collections.defaultdict(lambda: [0.0, 0.0])  # tx, rx (data direction)
    for p in order:
        for t in ph[p]:
            f = flows[t["task"]]
            dev_ph[f["src"]].add(p)
            dev_ph[f["dst"]].add(p)
            dev_bits[f["src"]][0] += f["bits"]
            dev_bits[f["dst"]][1] += f["bits"]
    # earliest start of every task (lower bound): EF of its phase's deps + its own delay
    t_es = {}
    for p in order:
        base = max((EF[d] for d in deps[p]), default=0.0)
        for t in ph[p]:
            t_es[t["task"]] = base + t.get("delay_ns", 0) / 1000.0
    for d, ps in sorted(dev_ph.items()):
        ids = [t["task"] for p in ps for t in ph[p]]
        if any(i not in done for i in ids):
            rows.append(dict(kind="device", dev=d, type=hosts[d]["type"], missing=True))
            continue
        T = max(done[i] for i in ids)
        cap = len(hosts[d]["planes"]) * meta["rate_gbps"] * 1e3
        # backlog bound per direction: bytes released at or after t all cross the device after t
        best_load, best_t = 0.0, 0.0
        for side in ("src", "dst"):
            rel = sorted(((t_es[i], flows[i]["bits"], flows[i]["cross"]) for i in ids
                          if flows[i][side] == d), reverse=True)
            acc, amin = 0.0, None
            for t0, bits, cross in rel:
                acc += bits
                amin = ALPHA_US[cross] if amin is None else min(amin, ALPHA_US[cross])
                v = t0 + acc * (A.MTU + A.DATA_HDR) / A.MTU / cap + amin
                if v > best_load:
                    best_load, best_t = v, t0
        crit = max(ps, key=lambda p: EF[p])
        bdev = max(EF[crit], best_load)
        fixed = delay_sum[crit] if EF[crit] >= best_load else best_t
        rows.append(dict(kind="device", dev=d, type=hosts[d]["type"], T=T, B=bdev,
                         eff_total=bdev / T, eff_comm=(bdev - fixed) / (T - fixed) if T > fixed else None,
                         bound_by="chain" if EF[crit] >= best_load else "backlog"))
    # Busy periods per device and direction (requirement 2 under bursty demand): maximal unions of
    # the device's [start, done] task intervals. Conditional bound: given the tasks' actual release
    # (WQE start) times -- upstream phases are judged on their own -- bound = max(backlog bound
    # max_t (t + bits released at or after t / device capacity + alpha), period LP with those
    # releases + alpha). eff = (bound - s) / (end - s): utilisation while the device has backlog.
    for d, ps in sorted(dev_ph.items()):
        cap = len(hosts[d]["planes"]) * meta["rate_gbps"] * 1e3
        for side in ("src", "dst"):
            iv = sorted((start[i], done[i], i) for p in ps for t in ph[p]
                        for i in [t["task"]] if flows[i][side] == d and i in start and i in done)
            periods, cur = [], []
            for a0, b0, i in iv:
                if cur and a0 > max(x[1] for x in cur):
                    periods.append(cur)
                    cur = []
                cur.append((a0, b0, i))
            if cur:
                periods.append(cur)
            for per in periods:
                s0 = min(x[0] for x in per)
                e0 = max(x[1] for x in per)
                rel = sorted(((start[i], flows[i]["bits"], flows[i]["cross"])
                              for _, _, i in per), reverse=True)
                acc, amin, bnd = 0.0, None, 0.0
                for t0, bits, cross in rel:
                    acc += bits
                    amin = ALPHA_US[cross] if amin is None else min(amin, ALPHA_US[cross])
                    bnd = max(bnd, t0 + acc * (A.MTU + A.DATA_HDR) / A.MTU / cap + amin)
                lp = None
                if acc * (A.MTU + A.DATA_HDR) / A.MTU / cap >= 5.0:
                    fl = [dict(flows[i], release_us=start[i] - s0) for _, _, i in per]
                    lp = A.ideal_makespan_timeindexed_us(copy.deepcopy(meta), fl, meta["ack_bytes"],
                                                         None, with_fault=False)
                    bnd = max(bnd, s0 + lp + amin)
                rows.append(dict(kind="busy", dev=d, type=hosts[d]["type"], lp=lp,
                                 dir="tx" if side == "src" else "rx", s=s0, e=e0, B=bnd,
                                 work_us=acc * (A.MTU + A.DATA_HDR) / A.MTU / cap, n=len(per),
                                 eff=(bnd - s0) / (e0 - s0) if e0 > s0 else None))
    allT = max(done[f] for f in flows if f in done)
    job = max(EF.values())
    rows.append(dict(kind="job", T=allT, B=job, eff_total=job / allT))
    for r in rows:
        r.update(case=os.path.basename(case.rstrip("/")), scenario=meta["scenario"],
                 scheme=meta["scheme"], seed=meta["seed"], fault=meta["fault"])
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("cases", nargs="+")
    ap.add_argument("--jobs", type=int, default=32)
    a = ap.parse_args()
    with cf.ProcessPoolExecutor(a.jobs) as ex, open(a.out, "w") as fo:
        for rows in ex.map(evaluate, a.cases, chunksize=1):
            for r in rows:
                fo.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    main()
