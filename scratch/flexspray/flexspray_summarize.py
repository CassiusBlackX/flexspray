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
ORDER = ["d2d", "d2dread", "h2d", "a2a", "mixed", "a2aread", "mixedread", "stagger", "staggermixed", "incast"]
scen = sorted({r["scenario"] for r in recs}, key=lambda s: ORDER.index(s) if s in ORDER else 99)
FORDER = ["F0", "F1near", "F1far", "F2up", "F3near", "F3far", "F4", "F1ssd", "F3ssd", "F1nic", "F3nic", "F4p4"]
faults = sorted({r["fault"] for r in recs} | {"F0"}, key=lambda f: FORDER.index(f) if f in FORDER else 99)
def work(item):
    (sc, scheme, seed), f, r, h = item
    d = dict(eff=None if dl(r) else r["eff"], dl=dl(r))
    if f != "F0" and h is not None and not dl(r) and not dl(h):
        fl = R.fault_loss(r, h)
        if fl:
            d.update(loss=fl["loss"], net=fl["agg_loss"], n=(1.0 / fl["frac"]) if fl.get("frac") else fl["n"], reb=fl["rebalance_us"],
                     ideal=1 - h["t_ideal_us"] / r["t_ideal_us"], full=fl["loss_full"],
                     trans=fl["loss_trans"], expo=r.get("exposure_bytes"), drain=r.get("drain_us"),
                     effnd=r.get("eff_nodrain"))
    return (sc, scheme, f), d
import concurrent.futures as cf
items = [(k, f, r, fs.get("F0")) for k, fs in by.items() for f, r in fs.items()]
cell = collections.defaultdict(list)   # (sc, scheme, fault) -> list of dicts
with cf.ProcessPoolExecutor(48) as ex:
    for key, d in ex.map(work, items, chunksize=2):
        cell[key].append(d)
def mean(v): v = [x for x in v if x is not None]; return st.mean(v) if v else None
def mx(v): v = [x for x in v if x is not None]; return max(v) if v else None
def mn(v): v = [x for x in v if x is not None]; return min(v) if v else None
out = {}
for k, v in cell.items():
    out["|".join(k)] = dict(eff=mean([x["eff"] for x in v]), dl=sum(x["dl"] for x in v), n=len(v),
                            loss=mean([x.get("loss") for x in v]), net=mean([x.get("net") for x in v]),
                            reb=mean([x.get("reb") for x in v]), N=next((x.get("n") for x in v if x.get("n")), None),
                            loss_max=mx([x.get("loss") for x in v]), full_max=mx([x.get("full") for x in v]),
                            trans_max=mx([x.get("trans") for x in v]), net_max=mx([x.get("net") for x in v]),
                            expo_max=mx([x.get("expo") for x in v]), drain_max=mx([x.get("drain") for x in v]),
                            effnd=mean([x.get("effnd") for x in v]), eff_min=mn([x["eff"] for x in v]))
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
            row.append(f"{fmt(c['eff'])} / {fmt(c['loss'], True)} (floor={100/n:.1f}%) / {fmt(c['net'], True)} / {'-' if c['reb'] is None else round(c['reb'])}" if n else fmt(c["eff"]))
        print(f"| {s} | " + " | ".join(row) + " |")

print("\n## Fault detail (worst seed): eff_min / loss steady / loss incl. transient [t_f, end] / "
      "loss first 10 us / network loss / exposure KB (bytes through isolated resources after t_f) / "
      "drain us\n")
for sc in scen:
    print(f"\n### {sc}\n")
    print("| scheme | fault | eff min | eff (no drain allowance) | loss | loss incl. transient | first 10 us | net | removed capacity (floor) | exposure KB | drain us |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for s in schemes:
        for f in faults[1:]:
            c = out.get(f"{sc}|{s}|{f}")
            if not c or c["eff"] is None:
                continue
            n = c["N"]
            e = c["expo_max"]
            print(f"| {s} | {f} | {fmt(c['eff_min'])} | {fmt(c['effnd'])} | {fmt(c['loss_max'], True)} | "
                  f"{fmt(c['full_max'], True)} | {fmt(c['trans_max'], True)} | {fmt(c['net_max'], True)} | "
                  f"{'' if not n else f'{100/n:.1f}%'} | {'' if e is None else round(e/1024)} | "
                  f"{'' if c['drain_max'] is None else round(c['drain_max'])} |")
