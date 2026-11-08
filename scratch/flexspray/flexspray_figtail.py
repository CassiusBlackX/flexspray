#!/usr/bin/env python3
"""Read-mix tail figure: hot-reader ingress per plane group and SSD0 egress over time, for two
cases (e.g. v5.2 vs v5.3, same workload and seed). One panel per case, one shared y scale.

Usage: flexspray_figtail.py CASE_A LABEL_A CASE_B LABEL_B OUT.png [--bin-us 4]
"""
import argparse
import collections
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402

DATA_W = (4096 + 78) / 4096
COLORS = ["#2a78d6", "#eb6834", "#1baf7a"]  # categorical slots 1-3 (validated default palette)


def series(case, bin_us):
    meta, tasks = A.load_case(case)
    hosts = {h["id"]: h for h in meta["hosts"]}
    readers = sorted({t["src"] for t in tasks if t["cls"] == "kv_read_hot"})
    ssd = next(t["dst"] for t in tasks if t["cls"] == "kv_read_hot")
    want = {}
    for r in readers:
        for p, port in hosts[r]["ports"].items():
            want[(r, port)] = ("hot" if p in hosts[ssd]["planes"] else "other", "rx")
    for p, port in hosts[ssd]["ports"].items():
        want[(ssd, port)] = ("ssd", "tx")
    acc = collections.defaultdict(lambda: collections.defaultdict(float))
    nports = collections.Counter(g for g, _ in want.values())
    prev = {}
    with open(os.path.join(case, "runlog", "GoodputSample.csv")) as f:
        for row in csv.DictReader(f):
            k = (int(row["node"]), int(row["port"]))
            if k not in want:
                continue
            g, dr = want[k]
            t = int(row["time_ns"])
            v = int(row["rx_payload_bytes" if dr == "rx" else "tx_payload_bytes"])
            if k in prev:
                acc[g][t // (bin_us * 1000)] += v - prev[k]
            prev[k] = v
    cap = 400e9 / 8 * bin_us * 1e-6 / DATA_W
    out = {}
    for g in ("other", "hot", "ssd"):
        bins = sorted(acc[g])
        out[g] = ([b * bin_us for b in bins], [acc[g][b] / cap / nports[g] for b in bins])
    return out, len(readers), sorted(hosts[ssd]["planes"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case_a"); ap.add_argument("label_a")
    ap.add_argument("case_b"); ap.add_argument("label_b")
    ap.add_argument("out")
    ap.add_argument("--bin-us", type=int, default=4)
    a = ap.parse_args()
    fig, axes = plt.subplots(1, 2, figsize=(11, 3.6), sharey=True)
    for ax, (case, label) in zip(axes, ((a.case_a, a.label_a), (a.case_b, a.label_b))):
        s, nr, planes = series(case, a.bin_us)
        names = {"other": f"hot readers, other planes (rx)",
                 "hot": f"hot readers, planes {planes[0]}/{planes[1]} (rx)",
                 "ssd": "SSD0 (tx, hotspot)"}
        for c, g in zip(COLORS, ("other", "hot", "ssd")):
            ax.plot(*s[g], color=c, linewidth=2, label=names[g])
        ax.set_title(label, fontsize=10, loc="left")
        ax.set_xlabel("time (µs)")
        ax.set_xlim(120, 185)
        ax.grid(axis="y", color="#e6e5e1", linewidth=0.8)
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)
    axes[0].set_ylabel("payload / port line rate\n(mean per port, %d µs bins)" % a.bin_us)
    axes[0].set_ylim(0, 1.05)
    axes[1].legend(loc="lower left", frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(a.out, dpi=150)


if __name__ == "__main__":
    main()
