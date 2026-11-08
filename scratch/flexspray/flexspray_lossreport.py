#!/usr/bin/env python3
"""Single-fault loss per affected device (the "< 1/N" requirement, reported per device).

For every fault case with a healthy (F0) twin of the same scenario / scheme / seed / map:

  per affected device and direction (tx = bytes it sources, rx = bytes it sinks, READ counted at
  the data source):
    gp_healthy, gp_fault   device goodput = its task bytes / completion time of its last task in
                           that direction (each business byte once), Gbps
    loss                   1 - gp_fault / gp_healthy  (relative-to-healthy throughput loss)
    planes_phys            the device's own physical planes (ports)
    planes_usable          planes still usable after the fault (degraded ones count as usable)
    removed                fraction of the device's own port capacity the fault removes
                           (a lost plane = 1/planes_phys; a lane degrade k/L on one port =
                           (1 - k/L)/planes_phys)
    loss_vs_1/N            loss - 1/planes_phys (negative = below the device's own 1/N)
    loss_vs_removed        loss - removed (> 0: the device lost more than the capacity removed)
  network: loss of total bytes / makespan.

"Affected" = devices owning a faulted port, or every device of the server behind a faulted L1
plane. Note: "<" in the requirement vs "<=" here; ties are listed.
Usage: flexspray_lossreport.py MATRIX_DIR [...] [--md]
"""
import argparse
import collections
import json
import os
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402


def device_gp(meta, tasks, done):
    by = collections.defaultdict(lambda: [0, 0.0])
    for t in tasks:
        if t["task"] not in done:
            continue
        src, dst = (t["dst"], t["src"]) if t["op"] == "URMA_READ" else (t["src"], t["dst"])
        for d, dr in ((src, "tx"), (dst, "rx")):
            by[(d, dr)][0] += t["bytes"]
            by[(d, dr)][1] = max(by[(d, dr)][1], done[t["task"]])
    return {k: (b * 8 / 1e3 / tl if tl > 0 else 0.0) for k, (b, tl) in by.items()}


def affected(meta):
    hosts = {h["id"]: h for h in meta["hosts"]}
    l1 = {v: tuple(map(int, k.split(","))) for k, v in meta["l1"].items()}
    out = {}  # device -> removed fraction of its own capacity
    for (_t, node, port, action, val) in meta.get("fault_rows") or []:
        if node in hosts:
            h = hosts[node]
            n = len(h["ports"])
            frac = Fraction(1, n)
            if action == "DEGRADE":
                frac = (1 - Fraction(val)) / n
            out[node] = out.get(node, 0) + frac
        elif node in l1 and action in ("ISOLATE_NODE", "DOWN_NODE"):
            p, s = l1[node]
            for h in meta["hosts"]:
                if h["server"] == s and p in h["ports"]:
                    out[h["id"]] = out.get(h["id"], 0) + Fraction(1, len(h["ports"]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--md", action="store_true")
    a = ap.parse_args()
    cases = {}
    for d in a.dirs:
        for c in os.listdir(d):
            p = os.path.join(d, c)
            if os.path.isdir(p) and os.path.exists(os.path.join(p, "case_meta.json")):
                cases[c] = p
    rows = []
    for c, p in sorted(cases.items()):
        parts = c.split("__")
        if len(parts) < 4 or parts[1] == "F0":
            continue
        twin = "__".join([parts[0], "F0"] + parts[2:])
        if twin not in cases:
            continue
        meta, tasks = A.load_case(p)
        _s, done = A.read_tasks(p)
        mh, th = A.load_case(cases[twin])
        _s, dh = A.read_tasks(cases[twin])
        if len(done) < len(tasks):
            rows.append(dict(case=c, error="incomplete"))
            continue
        gf, gh = device_gp(meta, tasks, done), device_gp(mh, th, dh)
        hosts = {h["id"]: h for h in meta["hosts"]}
        net = 1 - max(dh.values()) / max(done.values())
        for dev, removed in sorted(affected(meta).items()):
            h = hosts[dev]
            n = len(h["ports"])
            for dr in ("tx", "rx"):
                if (dev, dr) not in gh or gh[(dev, dr)] <= 0:
                    continue
                loss = 1 - gf.get((dev, dr), 0) / gh[(dev, dr)]
                usable = n - sum(1 for r in meta["fault_rows"] if r[1] == dev and r[3] != "DEGRADE")
                rows.append(dict(case=c, scenario=parts[0], fault=parts[1], seed=parts[-1],
                                 dev=dev, type=h["type"], dir=dr, gp_healthy=round(gh[(dev, dr)], 1),
                                 gp_fault=round(gf[(dev, dr)], 1), loss=round(loss, 4),
                                 planes_phys=n, planes_usable=usable, removed=round(float(removed), 4),
                                 loss_vs_1_over_N=round(loss - 1 / n, 4),
                                 loss_vs_removed=round(loss - float(removed), 4), net_loss=round(net, 4)))
    if not a.md:
        print(json.dumps(rows))
        return
    cols = ["case", "dev", "type", "dir", "gp_healthy", "gp_fault", "loss", "planes_phys",
            "planes_usable", "removed", "loss_vs_1_over_N", "loss_vs_removed", "net_loss"]
    print("| " + " | ".join(cols) + " |")
    print("|" + "---|" * len(cols))
    for r in rows:
        print("| " + " | ".join(str(r.get(k, "")) for k in cols) + " |")


if __name__ == "__main__":
    main()
