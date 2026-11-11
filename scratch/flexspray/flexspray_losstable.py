#!/usr/bin/env python3
"""Summarise flexspray_lossmatrix.py output into the REQUESTS performance-4 tables (round 11).

Per (fault semantics, scenario, fault, scheme): worst damaged device over devices and seeds.
For every window (steady / tdev / trans) and every N reading (N_dev, 4N = total lanes, Nscope =
planes the device's flows can use), a cell passes iff loss < 1/N (the original strict relation).
Classification of a failing cell:
  phys  physical limit, proven: tdev window - the per-device LP lower bound itself is >= 1/N;
  phys* steady / trans windows - the capacity removed from the device's usable planes (lane k: k/4
        of one of Nscope planes; port or L1-node fault: 1 of Nscope planes) is >= 1/N. NOT an LP
        proof: valid only if the device is port-bound in that window;
  impl  not excluded by those bounds: protocol or implementation shortfall;
  miss  tasks missing (P0);
  na    not judged (steady window shorter than 10 us): counted separately, never dropped from the
        denominator (the n/a rows are often the worst completion-time rows).
Usage: flexspray_losstable.py ROWS.jsonl [ROWS.jsonl ...] [--names a=b,...] [--detail]
"""
import argparse
import collections
import json

WIN = ("steady", "tdev", "trans")
NS = ("N_dev", "N_4N", "N_scope")


def removed_frac(r):
    k = r["lanes_lost"]
    if r["sem"] == "lane":
        return k / 4.0 / r["N_scope"]
    return 1.0 / r["N_scope"]


REL = "lt"


def fails(x, lim):
    """Original relation: loss < 1/N, so loss >= 1/N fails; with --rel le (extra column) only
    loss > 1/N fails."""
    return x >= lim if REL == "lt" else x > lim


def cell(rows, win, nk):
    """(status, worst loss) over rows (devices x seeds): pass if every row passes; else phys only if
    every failing row is physically excluded, impl otherwise; miss if any task is missing."""
    if any(r["missing"] for r in rows):
        return "miss", None
    vals = [r for r in rows if r["loss_" + win] is not None]
    if not vals:
        return "na", None
    worst = max(r["loss_" + win] for r in vals)
    failing = [r for r in vals if fails(r["loss_" + win], 1.0 / r[nk])]
    if not failing:
        return "pass", worst

    def phys(r):
        lim = 1.0 / r[nk]
        if win == "tdev":
            return r["lp_lb"] is not None and fails(r["lp_lb"], lim)
        return fails(removed_frac(r), lim)
    return ("phys" if all(phys(r) for r in failing) else "impl"), worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows", nargs="+")
    ap.add_argument("--names", default="")
    ap.add_argument("--detail", action="store_true")
    ap.add_argument("--rel", choices=["lt", "le"], default="lt")
    a = ap.parse_args()
    global REL
    REL = a.rel
    names = dict(x.rsplit("=", 1) for x in a.names.split(",") if x)
    G = collections.defaultdict(list)
    for fn in a.rows:
        for l in open(fn):
            r = json.loads(l)
            ds, sc, f, sch, sd = r["key"]
            G[(r["sem"], sc, f, names.get(sch, sch))].append(r)
    schemes = sorted({k[3] for k in G})
    cnt = collections.Counter()
    tot = collections.Counter()
    detail = []
    for (sem, sc, f, sch), rows in sorted(G.items()):
        cells = {}
        for w in WIN:
            for nk in NS:
                st, loss = cell(rows, w, nk)
                cells[(w, nk)] = (st, loss)
                if st != "na":
                    tot[(sem, sch, w, nk)] += 1
                cnt[(sem, sch, w, nk, st)] += 1
        reb = max((r["t_rebal_us"] for r in rows if r["t_rebal_us"] is not None), default=None)
        lp = max((r["lp_lb"] for r in rows if r["lp_lb"] is not None), default=None)
        detail.append((sem, sc, f, sch, cells, reb, lp))
    print("## Pass counts: cells with loss %s 1/N (worst device, worst seed) / all cells; the rest "
          "split into phys (LP-proven, tdev) or phys* (removed capacity, port-bound assumption, "
          "steady / trans), impl, miss, n/a (not judged)\n" % ("<" if REL == "lt" else "<="))
    print("| semantics | scheme | window | " + " | ".join(NS) + " |")
    print("|---|---|---|" + "---|" * len(NS))
    for sem in ("graceful", "hard", "lane"):
        for sch in schemes:
            for w in WIN:
                if not any(tot[(sem, sch, w, nk)] + cnt[(sem, sch, w, nk, "na")] for nk in NS):
                    continue
                parts = []
                for nk in NS:
                    t = tot[(sem, sch, w, nk)]
                    phys = "phys" if w == "tdev" else "phys*"
                    parts.append("%d/%d (%s %d, impl %d, miss %d, n/a %d)" % (
                        cnt[(sem, sch, w, nk, "pass")], t + cnt[(sem, sch, w, nk, "na")], phys,
                        cnt[(sem, sch, w, nk, "phys")], cnt[(sem, sch, w, nk, "impl")],
                        cnt[(sem, sch, w, nk, "miss")], cnt[(sem, sch, w, nk, "na")]))
                print("| %s | %s | %s | %s |" % (sem, sch, w, " | ".join(parts)))
    if a.detail:
        print("\n## Rows (loss %% per window: steady / tdev / trans; marks per N_dev,4N,Nscope: "
              "+ pass, P phys, i impl, M miss, . n/a)\n")
        for sem, sc, f, sch, cells, reb, lp in detail:
            s = []
            for w in WIN:
                st, loss = cells[(w, "N_dev")]
                marks = "".join({"pass": "+", "phys": "P", "impl": "i", "miss": "M", "na": "."}[cells[(w, nk)][0]] for nk in NS)
                s.append(("%5.1f" % (100 * loss) if loss is not None else "  n/a") + " " + marks)
            print("%-8s %-9s %-8s %-4s | %s | rebal %s us | LP %s" % (
                sem, sc, f, sch, " | ".join(s), "-" if reb is None else "%.0f" % reb,
                "-" if lp is None else "%.1f%%" % (100 * lp)))


if __name__ == "__main__":
    main()
