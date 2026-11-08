#!/usr/bin/env python3
"""Fault time series: what happens around t_f, for one or more schemes of the same case.

Usage: flexspray_timeseries.py CASE_DIR [CASE_DIR ...] --out PREFIX [--window-us 1]

For the affected devices (fault_info: one device and direction, or every server-0 device on the
faulted plane for an L1 failure) it extracts, from the 200 ns goodput samples:
  * total payload rate of the affected devices (Gbps, 1 us moving window),
  * share of that traffic carried by the faulted plane,
  * peak output-queue bytes over all switch ports of the faulted plane,
and the four instants the reviewers asked for:
  t_f       fault injected,
  t_notify  local isolation is immediate; remote ends learn after UB_LINK_HEALTH_REMOTE_NOTIFY_NS
            (isolations only; a lane degrade has no notification at all - in-band signals only),
  t_qpeak   the faulted plane's switch backlog stops growing (senders have adjusted),
  t_settle  the backlog is back to its pre-fault level for good.
Writes PREFIX.png (small multiples, one shared time axis) and PREFIX.json.
"""
import argparse
import bisect
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402

SERIES = ["#2a78d6", "#eb6834", "#1baf7a"]   # categorical slots 1-3 (validated reference palette)
INK, INK2, GRID, SURF = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"


def affected(meta):
    fi = meta.get("fault_info") or {}
    plane = fi.get("plane", 3)
    if fi.get("dev") == "server0":
        devs = [h["id"] for h in meta["hosts"] if h["server"] == 0 and plane in h["planes"]]
        return devs, ["tx", "rx"], plane
    if fi.get("dev") is None:
        return [h["id"] for h in meta["hosts"] if h["type"] == "NPU"], ["tx"], plane
    d = fi.get("dir", "tx")
    return [fi["dev"]], (["tx", "rx"] if d == "both" else [d]), plane


def series(case, win_us):
    meta, _ = A.load_case(case)
    samples = A.read_samples(case)
    qcol = _queue_samples(case)
    devs, dirs, plane = affected(meta)
    hosts = {h["id"]: h for h in meta["hosts"]}
    ts = sorted({x[0] for d in devs for x in samples[(d, hosts[d]["ports"][plane])]})
    step = ts[1] - ts[0]
    k = max(1, int(round(win_us * 1000 / step)))
    idx = {"tx": 1, "rx": 2}

    def cum(d, p, dr):
        s = samples[(d, hosts[d]["ports"][p])]
        return [x[idx[dr]] for x in s]

    tot = [0.0] * len(ts)
    onp = [0.0] * len(ts)
    for d in devs:
        for dr in dirs:
            for p in hosts[d]["planes"]:
                c = cum(d, p, dr)
                for i in range(len(ts)):
                    tot[i] += c[i]
                    if p == plane:
                        onp[i] += c[i]
    t_us, rate, share = [], [], []
    for i in range(k, len(ts)):
        dt = ts[i] - ts[i - k]
        r = (tot[i] - tot[i - k]) * 8 / dt
        rp = (onp[i] - onp[i - k]) * 8 / dt
        t_us.append((ts[i] - dt / 2) / 1000.0)
        rate.append(r)
        share.append(rp / r if r > 0 else 0.0)
    # Peak switch queue on the faulted plane.
    sw_nodes = {v for k_, v in meta["l1"].items() if int(k_.split(",")[0]) == plane} | \
               {v for k_, v in meta["l2"].items() if int(k_.split(",")[0]) == plane}
    qt = sorted(qcol)
    qmax = []
    for t in t_us:
        j = bisect.bisect_right(qt, t * 1000) - 1
        qmax.append(max((b for (n, _p), b in qcol[qt[j]].items() if n in sw_nodes), default=0)
                    if j >= 0 else 0)
    return meta, dict(t=t_us, rate=rate, share=share, qmax_kb=[q / 1024 for q in qmax])


def _queue_samples(case):
    import csv
    out = {}
    with open(os.path.join(case, "runlog", "GoodputSample.csv")) as f:
        for r in csv.DictReader(f):
            out.setdefault(int(r["time_ns"]), {})[(int(r["node"]), int(r["port"]))] = int(r["queue_bytes"])
    return out


def key_times(meta, s, notify_ns):
    """t_detect: when endpoints learn of the fault (isolation: local immediately, remote ends
    after the management notification; lane degrade: never - in-band signals only).
    t_qpeak: the faulted plane's switch backlog stops growing = senders have adjusted.
    t_settle: backlog back under max(2 x pre-fault p95, 64 KB) for good (None = not within
    the window). Rate dip: lowest 2-us rate in [t_f, t_f+10] vs the post-fault mean."""
    tf = meta["fault_ns"] / 1000.0
    t, q, rate = s["t"], s["qmax_kb"], s["rate"]
    end = tf + 40
    pre_q = sorted(q[i] for i in range(len(t)) if tf - 20 <= t[i] < tf)
    base = max(2 * pre_q[int(0.95 * (len(pre_q) - 1))], 64.0) if pre_q else 64.0
    win = [i for i in range(len(t)) if tf <= t[i] <= end]
    ipk = max(win, key=lambda i: q[i])
    settle = None
    if q[ipk] <= base:
        # The faulted plane never built a backlog beyond its pre-fault level.
        t_qpeak, settle = None, tf
    else:
        t_qpeak = t[ipk]
        for i in [k for k in win if k >= ipk]:
            if all(q[j] <= base for j in win if j >= i):
                settle = t[i]
                break
    post_i = [i for i in range(len(t)) if tf + 10 <= t[i] <= end]
    post_rate = sum(rate[i] for i in post_i) / len(post_i)
    pre_rate = sum(rate[i] for i in range(len(t)) if tf - 20 <= t[i] < tf - 1) / \
        max(1, len([1 for i in range(len(t)) if tf - 20 <= t[i] < tf - 1]))
    dip = min(rate[i] for i in win if t[i] <= tf + 10)
    isolation = any(r[3] in ("ISOLATE", "ISOLATE_NODE") for r in meta["fault_rows"])  # not DOWN
    return dict(t_f=tf, t_detect_remote=tf + notify_ns / 1000.0 if isolation else None,
                t_qpeak=t_qpeak, q_peak_kb=q[ipk], q_base_kb=base, t_settle=settle,
                q_end_kb=q[win[-1]], rate_pre=pre_rate, rate_post=post_rate,
                dip_vs_post=dip / post_rate - 1 if post_rate else None,
                share_pre=sum(s["share"][i] for i in range(len(t)) if tf - 20 <= t[i] < tf) /
                max(1, len([1 for i in range(len(t)) if tf - 20 <= t[i] < tf])),
                share_post=sum(s["share"][i] for i in post_i) / len(post_i))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--window-us", type=float, default=2.0)
    ap.add_argument("--notify-ns", type=int, default=2000)
    ap.add_argument("--span", default="-10,40", help="plot range relative to t_f (us)")
    a = ap.parse_args()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.size": 9, "axes.edgecolor": INK2, "axes.labelcolor": INK2,
                         "xtick.color": INK2, "ytick.color": INK2, "figure.facecolor": SURF,
                         "axes.facecolor": SURF})
    lo, hi = map(float, a.span.split(","))
    res = {}
    fig, axes = plt.subplots(3, 1, figsize=(7.5, 6.6), sharex=True)
    meta0 = None
    for ci, case in enumerate(a.cases):
        meta, s = series(case, a.window_us)
        meta0 = meta0 or meta
        kt = key_times(meta, s, a.notify_ns)
        res[meta["scheme"]] = kt
        tf = kt["t_f"]
        x = [t - tf for t in s["t"]]
        sel = [i for i, v in enumerate(x) if lo <= v <= hi]
        col = SERIES[ci % len(SERIES)]
        axes[0].plot([x[i] for i in sel], [s["rate"][i] for i in sel], color=col, lw=2,
                     label=meta["scheme"])
        axes[1].plot([x[i] for i in sel], [100 * s["share"][i] for i in sel], color=col, lw=2)
        axes[2].plot([x[i] for i in sel], [s["qmax_kb"][i] for i in sel], color=col, lw=2)
        for key, mk in (("t_qpeak", "o"), ("t_settle", "s")):
            if kt["t_qpeak"] is None:
                break  # no backlog built up: nothing to mark
            if kt[key] is not None and lo <= kt[key] - tf <= hi:
                k = min(range(len(x)), key=lambda i: abs(x[i] - (kt[key] - tf)))
                axes[2].plot([x[k]], [s["qmax_kb"][k]], marker=mk, ms=8, color=col, mec=SURF,
                             mew=2, zorder=5)
    kt0 = next(iter(res.values()))
    # Hard link-down: mark transport detection events from the first case (FailureEvent.csv).
    fe = os.path.join(a.cases[0], "runlog", "FailureEvent.csv")
    if os.path.exists(fe):
        import csv as _csv
        ev = list(_csv.DictReader(open(fe)))
        dead = [int(r["time_ns"]) / 1000.0 - kt0["t_f"] for r in ev if r["event"] == "probe-timeout"]
        if dead:
            kt0["t_remote_dead_first"] = min(dead) + kt0["t_f"]
            for ax in axes:
                ax.axvline(min(dead), color=INK2, lw=1, ls=":")
    for ax in axes:
        ax.axvline(0, color=INK, lw=1)
        if kt0["t_detect_remote"] is not None:
            ax.axvline(kt0["t_detect_remote"] - kt0["t_f"], color=INK2, lw=1, ls="--")
        ax.grid(axis="y", color=GRID, lw=0.8)
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)
    axes[0].set_ylabel("affected devices\npayload Gbps")
    axes[1].set_ylabel(f"plane {(meta0.get('fault_info') or {}).get('plane', 3)} share %")
    axes[2].set_ylabel("peak switch queue\non that plane, KB")
    axes[2].set_xlabel("µs after fault.  solid: fault;  dashed: remote notified (isolation)\n"
                       "dotted: first remote probe-timeout (hard down)\n"
                       "●: backlog peak (senders adjusted);  ■: backlog back to pre-fault level")
    axes[0].legend(frameon=False, loc="lower left", labelcolor=INK)
    fi = meta0.get("fault_info") or {}
    axes[0].set_title(f"{meta0['scenario']} {meta0['fault']}: devices {fi.get('dev')} ({','.join(affected(meta0)[1])})",
                      loc="left", color=INK, fontsize=10)
    fig.tight_layout()
    fig.savefig(a.out + ".png", dpi=130)
    json.dump(res, open(a.out + ".json", "w"), indent=1)
    print(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
