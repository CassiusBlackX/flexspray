#!/usr/bin/env python3
"""Per-phase completion-time inflation against the fluid LP (round 12).

For a multi-phase case (flexspray_bench.py) and its healthy twin, for every phase p >= 1:
  release r_p  = first WQE start of a task of the phase (the simulator releases a phase when its
                 dependencies completed, plus the task delay);
  duration D_p = last completion of a task of the phase - r_p;
  bound L_p    = minimum completion time of the phase's flows alone, released together at r_p, under
                 the capacities the network has during the phase (healthy before t_f, faulted
                 after; a phase that spans t_f gets the fault at t_f - r_p);
  inflation I_p = D_p / L_p.
Pairing the fault run with the healthy run phase by phase:
  excess_p = (D_p^fault - D_p^healthy) - (L_p^fault - L_p^healthy)
           = time the phase lost beyond what the removed capacity forces.
Position: pre (ends before t_f), span (contains t_f), post (starts after t_f).
Verdict (per case): the post-fault tail is
  one-off    if the median excess of the post phases is <= 10 % of their median healthy duration
             and the span phase carries the excess;
  per-phase  if the median post-phase excess is > 10 % of their median healthy duration.
Usage: flexspray_phaseinfl.py OUT.jsonl DIR [DIR ...] [--jobs N]
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1")
import argparse
import collections
import concurrent.futures as cf
import copy
import json
import statistics as S
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402


def phases(case):
    meta, tasks = A.load_case(case)
    start, done = A.read_tasks(case)
    flows = {f["task"]: f for f in A.bulk_flows(meta, tasks)}
    by = collections.defaultdict(list)
    for t in tasks:
        if t.get("phase", 0) >= 1:
            by[t["phase"]].append(t)
    out = {}
    for ph, ts in by.items():
        ids = [t["task"] for t in ts]
        if any(i not in done for i in ids) or any(i not in start for i in ids):
            out[ph] = dict(missing=True, cls=ts[0]["cls"])
            continue
        r = min(start[i] for i in ids)
        c = max(done[i] for i in ids)
        out[ph] = dict(cls=ts[0]["cls"], r=r, c=c, D=c - r,
                       flows=[dict(flows[i], release_us=0.0) for i in ids if i in flows])
    return meta, out


def bound(meta, flows, r_us, c_us):
    if not flows:
        return None
    m = copy.deepcopy(meta)
    tf = meta["fault_ns"] / 1000.0 if meta.get("fault_rows") else None
    if tf is None or c_us <= tf:
        return A.ideal_makespan_timeindexed_us(m, flows, m["ack_bytes"], None, with_fault=False)
    m["fault_ns"] = max(0.0, (tf - r_us)) * 1000.0
    return A.ideal_makespan_timeindexed_us(m, flows, m["ack_bytes"], None, with_fault=True)


def one(job):
    key, fcase, hcase = job
    mf, pf = phases(fcase)
    mh, ph = phases(hcase)
    tf = mf["fault_ns"] / 1000.0 if mf.get("fault_rows") else None
    rows = []
    for p in sorted(pf):
        a, h = pf[p], ph.get(p)
        if a.get("missing") or h is None or h.get("missing"):
            rows.append(dict(key=key, phase=p, cls=a["cls"], missing=True))
            continue
        lf = bound(mf, a["flows"], a["r"], a["c"])
        lh = bound(mh, h["flows"], h["r"], h["c"])
        pos = "na" if tf is None else ("pre" if a["c"] <= tf else ("span" if a["r"] < tf else "post"))
        rows.append(dict(key=key, phase=p, cls=a["cls"], pos=pos, r=a["r"], D=a["D"], L=lf,
                         Dh=h["D"], Lh=lh, I=a["D"] / lf if lf else None,
                         Ih=h["D"] / lh if lh else None,
                         excess=(a["D"] - h["D"]) - ((lf or 0) - (lh or 0))))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("dirs", nargs="+")
    ap.add_argument("--jobs", type=int, default=32)
    a = ap.parse_args()
    rows = {}
    for d in a.dirs:
        ds = os.path.basename(d.rstrip("/"))
        for l in open(os.path.join(d, "results.jsonl")):
            r = json.loads(l)
            rows[(ds, r["scenario"], r["fault"], r["scheme"], r["seed"])] = os.path.join(d, r["case"])
    jobs = [(k, c, rows[(k[0], k[1], "F0", k[3], k[4])]) for k, c in rows.items()
            if k[2] != "F0" and (k[0], k[1], "F0", k[3], k[4]) in rows]
    with cf.ProcessPoolExecutor(a.jobs) as ex, open(a.out, "w") as fo:
        for res in ex.map(one, jobs, chunksize=1):
            for r in res:
                fo.write(json.dumps(r) + "\n")
    print(len(jobs), "fault cases")


def verdict(rows):
    """rows of one case -> (verdict, details)."""
    post = [r for r in rows if r.get("pos") == "post" and not r.get("missing")]
    span = [r for r in rows if r.get("pos") == "span" and not r.get("missing")]
    if any(r.get("missing") for r in rows):
        return "missing", {}
    if not post:
        return "no-post-phase", {}
    ex = S.median(r["excess"] for r in post)
    base = S.median(r["Dh"] for r in post)
    sp = sum(r["excess"] for r in span)
    return ("per-phase" if ex > 0.1 * base else "one-off"), dict(
        post_phases=len(post), post_median_excess_us=ex, post_median_healthy_us=base,
        post_excess_frac=ex / base if base else None, span_excess_us=sp,
        post_total_excess_us=sum(r["excess"] for r in post),
        post_median_I=S.median(r["I"] for r in post if r["I"]),
        post_median_Ih=S.median(r["Ih"] for r in post if r["Ih"]))


if __name__ == "__main__":
    main()
