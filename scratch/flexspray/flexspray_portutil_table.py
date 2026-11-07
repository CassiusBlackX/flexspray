#!/usr/bin/env python3
"""Tables from flexspray_portutil.py output: worst device per type (min over devices, directions
and seeds), never averaged.

Usage: flexspray_portutil_table.py portutil.jsonl [--scheme flex+spraycc] > tables.md
"""
import argparse
import collections
import json

TYPES = ["NPU", "CPU", "NIC", "SSD"]
SC_ORDER = ["d2d", "d2dread", "h2d", "a2a", "mixed", "a2aread", "mixedread", "stagger", "osub"]


def worst(rec, window, metric="util", only_saturated=True):
    """{type: (value, dev, dir, need)} minimum over devices of the given window."""
    out = {}
    per = rec["dev"].get(window) or {}
    for d, dirs in per.items():
        t = rec["types"][d]
        for dr, v in dirs.items():
            if only_saturated and v["need"] < 0.9:
                continue
            cur = out.get(t)
            if cur is None or v[metric] < cur[0]:
                out[t] = (v[metric], d, dr, v["need"])
    return out


def fmt(v):
    return "" if v is None else f"{v[0]:.3f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("jsonl")
    ap.add_argument("--scheme", default="flex+spraycc")
    a = ap.parse_args()
    recs = [json.loads(l) for l in open(a.jsonl)]
    recs = [r for r in recs if "error" not in r]
    by = collections.defaultdict(list)
    for r in recs:
        by[(r["scenario"], r["fault"], r["scheme"])].append(r)
    scen = sorted({r["scenario"] for r in recs}, key=lambda s: SC_ORDER.index(s) if s in SC_ORDER else 99)
    schemes = sorted({r["scheme"] for r in recs}, key=lambda s: (s != a.scheme, s))
    faults = sorted({r["fault"] for r in recs}, key=lambda f: (f != "F0", f))

    def agg(rs, window, metric="util"):
        res = {}
        for r in rs:
            for t, v in worst(r, window, metric).items():
                if t not in res or v[0] < res[t][0]:
                    res[t] = v + (r["seed"],)
        return res

    print("## A. 无故障：逐设备端口协议利用率，最差设备（所有设备、两个方向、所有种子中的最小值）\n")
    print("每格 = 全程（设备自己的忙碌期 [0, 最后一条流完成]）/ 稳态窗口 [10 µs, 0.95×首个完成]。\n")
    for sc in scen:
        print(f"### {sc}\n")
        types = [t for t in TYPES if any(t in worst(r, "busy") for r in recs if r["scenario"] == sc)]
        print("| 方案 | " + " | ".join(types) + " | 最差设备 |")
        print("|---|" + "---|" * (len(types) + 1))
        for sch in schemes:
            rs = by.get((sc, "F0", sch))
            if not rs:
                continue
            b, s = agg(rs, "busy"), agg(rs, "steady")
            cells = [f"{fmt(b.get(t))} / {fmt(s.get(t))}" for t in types]
            wt = min(b.items(), key=lambda kv: kv[1][0]) if b else None
            who = f"{wt[0]} dev{wt[1][1]} {wt[1][2]} s{wt[1][4]}" if wt else ""
            print(f"| {sch} | " + " | ".join(cells) + f" | {who} |")
        print()

    print(f"## B. 故障：{a.scheme} 的逐设备端口协议利用率，最差设备\n")
    print("每格 = 故障后稳态 [t_f+10 µs, …] / 故障瞬态 [t_f, t_f+10 µs]。"
          "上限已扣除被隔离端口、按降级速率计、并计入对端可达容量。"
          "最后一列为受损设备自身在全程（忙碌期）内的利用率。\n")
    for sc in scen:
        print(f"### {sc}\n")
        types = [t for t in TYPES if any(t in worst(r, "busy") for r in recs if r["scenario"] == sc)]
        print("| 故障 | " + " | ".join(types) + " | 受损设备全程 |")
        print("|---|" + "---|" * (len(types) + 1))
        for f in faults:
            rs = by.get((sc, f, a.scheme))
            if not rs:
                continue
            p = agg(rs, "post" if f != "F0" else "steady")
            tr = agg(rs, "trans") if f != "F0" else {}
            cells = [f"{fmt(p.get(t))}" + (f" / {fmt(tr.get(t))}" if f != "F0" else "")
                     for t in types]
            own = []
            for r in rs:
                for d in r.get("affected", []):
                    for dr, v in (r["dev"].get("busy", {}).get(str(d)) or {}).items():
                        own.append(v["util"])
            print(f"| {f} | " + " | ".join(cells) + f" | {min(own):.3f} |" if own else
                  f"| {f} | " + " | ".join(cells) + " | |")
        print()

    print("## C. 未饱和设备（需求 < 0.9 × 端口上限，不计入上面的最差值）\n")
    cnt = collections.Counter()
    for r in recs:
        for d, dirs in (r["dev"].get("busy") or {}).items():
            for dr, v in dirs.items():
                if v["need"] < 0.9:
                    cnt[(r["scenario"], r["fault"], r["scheme"], r["types"][d], dr)] += 1
    if not cnt:
        print("无。\n")
    else:
        for k, v in sorted(cnt.items()):
            print(f"- {k}: {v}")
        print()


if __name__ == "__main__":
    main()
