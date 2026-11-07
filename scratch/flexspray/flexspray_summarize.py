#!/usr/bin/env python3
"""summarize.py MATRIX_DIR: seed-averaged tables (markdown) from results2.jsonl.
Healthy efficiency, and for faults: affected-device loss / network loss / rebalance time vs 1/N.
Runs with unfinished tasks at the 2 ms cap and zero residual traffic are marked DL (deadlock)."""
import os, sys, json, collections, statistics as st
os.environ["OMP_NUM_THREADS"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flexspray_report as R
root = sys.argv[1]
recs = [json.loads(l) for l in open(os.path.join(root, "results2.jsonl"))]
for r in recs:
    r["_dir"] = os.path.join(root, r["case"])
by = collections.defaultdict(dict)
for r in recs:
    by[(r["scenario"], r["scheme"], r["seed"])][r["fault"]] = r
def dl(r): return r.get("missing_tasks", 0) > 0 or r.get("rc") != 0 or "error" in r
schemes = sorted({r["scheme"] for r in recs}, key=lambda s: (s != "flex+spraycc", s))
scen = [s for s in ["d2d", "d2dread", "h2d", "a2a", "mixed"] if any(r["scenario"] == s for r in recs)]
faults = ["F0", "F1near", "F1far", "F2up", "F3near", "F3far", "F4"]
def work(item):
    (sc, scheme, seed), f, r, h = item
    d = dict(eff=None if dl(r) else r["eff"], dl=dl(r))
    if f != "F0" and h is not None and not dl(r) and not dl(h):
        fl = R.fault_loss(r, h)
        if fl:
            d.update(loss=fl["loss"], net=fl["agg_loss"], n=fl["n"], reb=fl["rebalance_us"],
                     ideal=1 - h["t_ideal_us"] / r["t_ideal_us"])
    return (sc, scheme, f), d
import concurrent.futures as cf
items = [(k, f, r, fs.get("F0")) for k, fs in by.items() for f, r in fs.items()]
cell = collections.defaultdict(list)   # (sc, scheme, fault) -> list of dicts
with cf.ProcessPoolExecutor(48) as ex:
    for key, d in ex.map(work, items, chunksize=2):
        cell[key].append(d)
def mean(v): v = [x for x in v if x is not None]; return st.mean(v) if v else None
out = {}
for k, v in cell.items():
    out["|".join(k)] = dict(eff=mean([x["eff"] for x in v]), dl=sum(x["dl"] for x in v), n=len(v),
                            loss=mean([x.get("loss") for x in v]), net=mean([x.get("net") for x in v]),
                            reb=mean([x.get("reb") for x in v]), N=next((x.get("n") for x in v if x.get("n")), None))
json.dump(out, open(os.path.join(root, "summary.json"), "w"), indent=1)
def fmt(x, pct=False):
    if x is None: return "-"
    return f"{x*100:.1f}%" if pct else f"{x:.3f}"
print("## Healthy efficiency (T_ideal / makespan, mean over seeds; DL = deadlocked run)\n")
print("| scheme | " + " | ".join(scen) + " |"); print("|---|" + "---|" * len(scen))
for s in schemes:
    row = []
    for sc in scen:
        c = out.get(f"{sc}|{s}|F0")
        row.append("-" if not c else (f"DL({c['dl']}/{c['n']})" if c["dl"] and c["eff"] is None else fmt(c["eff"]) + (f" DL{c['dl']}" if c["dl"] else "")))
    print(f"| {s} | " + " | ".join(row) + " |")
for sc in scen:
    print(f"\n## {sc}: fault cases — eff / affected-device loss (vs 1/N) / network loss / rebalance us\n")
    print("| scheme | " + " | ".join(faults[1:]) + " |"); print("|---|" + "---|" * (len(faults) - 1))
    for s in schemes:
        row = []
        for f in faults[1:]:
            c = out.get(f"{sc}|{s}|{f}")
            if not c: row.append("-"); continue
            if c["eff"] is None: row.append(f"DL"); continue
            n = c["N"]
            row.append(f"{fmt(c['eff'])} / {fmt(c['loss'], True)} (1/N={100/n:.1f}%) / {fmt(c['net'], True)} / {'-' if c['reb'] is None else round(c['reb'])}" if n else fmt(c["eff"]))
        print(f"| {s} | " + " | ".join(row) + " |")
