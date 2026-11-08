#!/usr/bin/env python3
"""Per-device port bandwidth utilization (the reviewers' "every device port >= X%" reading).

For every device and direction (tx / rx), over a steady window, this reports:

  meas     measured TP payload rate (Gbps), summed over the device's ports that the workload uses
           in that direction (a port is "used" if some bulk flow of the device can send/receive
           data on that plane; isolated ports are excluded after a fault).
  ceil     protocol payload ceiling of those ports in the window:
               sum_port (C_port * dt - nondata_wire_bytes_port) / data_w
           C_port = the port's current rate (lane degrade lowers it), data_w = (4096+78)/4096,
           nondata = wire bytes that are not data packets (ACKs for the opposite direction,
           credits, control) measured on that port. So the ceiling already charges the
           device's own reverse-direction ACK overhead, exactly like the LP bound.
  util     meas / ceil  -> "port protocol utilization" (the primary per-device number)
  line     meas / (used ports x line rate) -> payload / line-rate utilization
  wire     wire bytes / (used ports x current rate) -> raw link utilization
  util_biz (busy window only) business goodput / ceil: every task byte the device sources (tx) or
           sinks (rx) and that completed inside the window, counted ONCE (GBN retransmissions and
           hard-down re-dispatched copies are excluded; meas/util count every TP payload byte the
           ports carried, so util >= util_biz and the gap is the recovery overhead).
  need     the rate the device must sustain to finish with the healthy ideal makespan
           (device bytes in that direction / T_ideal_healthy), as a fraction of ceil: need < 1 means the
           workload does not demand the device's full port capacity, so util < 1 is not a loss.

Windows: healthy = [10 us, 0.95 * first bulk completion]; with a fault, pre = [10 us, t_f] and
post = [t_f + 10 us, 0.95 * first completion] (plus `trans` = [t_f, t_f + 10 us] for the
transient).

Usage: flexspray_portutil.py CASE_DIR [CASE_DIR ...] [--jobs N] [--out results.jsonl]
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1")
import argparse
import bisect
import collections
import concurrent.futures as cf
import json
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402

MTU = 4096
DATA_W = (MTU + 78) / MTU


def _delta(series, t0_us, t1_us, dead_after_us=None):
    """(dt_ns, capacity_bytes, d_tx_wire, d_rx_wire, d_tx_payload, d_rx_payload).

    capacity_bytes integrates the port's sampled rate over the window (so a lane degrade inside
    the window is charged from when it happens). After `dead_after_us` (port isolated) the port
    contributes only the bytes it actually carried while draining, never spare capacity."""
    ts = [x[0] for x in series]
    i0 = max(0, bisect.bisect_right(ts, t0_us * 1000) - 1)
    i1 = max(0, bisect.bisect_right(ts, t1_us * 1000) - 1)
    a, b = series[i0], series[i1]
    cap_tx = cap_rx = 0.0
    for i in range(i0, i1):
        dt = series[i + 1][0] - series[i][0]
        c = series[i + 1][3] / 8.0 * dt * 1e-9
        if dead_after_us is not None and series[i][0] >= dead_after_us * 1000:
            cap_tx += series[i + 1][4] - series[i][4]
            cap_rx += series[i + 1][5] - series[i][5]
        else:
            cap_tx += c
            cap_rx += c
    return (b[0] - a[0], (cap_tx, cap_rx), b[4] - a[4], b[5] - a[5], b[1] - a[1], b[2] - a[2])


def isolated_ports(meta):
    """{(host, port)} isolated by the fault (device port, or every port toward an isolated L1)."""
    out = set()
    l1_by_node = {v: tuple(map(int, k.split(","))) for k, v in meta["l1"].items()}
    for (_, node, port, action, _v) in meta.get("fault_rows") or []:
        if action in ("ISOLATE_NODE", "DOWN_NODE") and node in l1_by_node:
            p, s = l1_by_node[node]
            for h in meta["hosts"]:
                if h["server"] == s and p in h["ports"]:
                    out.add((h["id"], h["ports"][p]))
        elif action in ("ISOLATE", "DOWN") and node < len(meta["hosts"]):
            out.add((node, port))
    return out


def device_windows(case):
    meta, tasks = A.load_case(case)
    flows = A.bulk_flows(meta, tasks)
    start, done = A.read_tasks(case)
    comp = [done[f["task"]] for f in flows if f["task"] in done]
    if not comp or len(comp) < len(flows):
        return None
    samples = A.read_samples(case)
    # Demand is judged against the healthy ideal makespan (the workload's design pace): a fault
    # stretches T_ideal and would otherwise make every other device look "unsaturated".
    t_ideal = 1.0 / A.solve_lambda(meta, flows, [A.capacities(meta, faulted=False)], [1.0],
                                   meta["ack_bytes"])
    end = 0.95 * min(comp)
    tf = meta["fault_ns"] / 1000.0 if meta.get("fault_ns") else None
    wins = {"steady": (10.0, end)} if tf is None else {
        "pre": (10.0, tf), "trans": (tf, tf + 10.0), "post": (tf + 10.0, max(end, tf + 20.0))}
    hosts = {h["id"]: h for h in meta["hosts"]}
    used = {"tx": collections.defaultdict(set), "rx": collections.defaultdict(set)}
    dev_bits = {"tx": collections.Counter(), "rx": collections.Counter()}
    # peers[dir][d][plane] = set of far-end devices of d's flows on that plane
    peers = {"tx": collections.defaultdict(lambda: collections.defaultdict(set)),
             "rx": collections.defaultdict(lambda: collections.defaultdict(set))}
    for f in flows:
        used["tx"][f["src"]].update(f["planes"])
        used["rx"][f["dst"]].update(f["planes"])
        for p in f["planes"]:
            peers["tx"][f["src"]][p].add(f["dst"])
            peers["rx"][f["dst"]][p].add(f["src"])
        dev_bits["tx"][f["src"]] += f["bits"]
        dev_bits["rx"][f["dst"]] += f["bits"]
    iso = isolated_ports(meta)
    line = meta["rate_gbps"] * 1e9
    # Last completion per device and direction -> the device's own busy period [0, t_last].
    t_last = {"tx": collections.defaultdict(float), "rx": collections.defaultdict(float)}
    for f in flows:
        t_last["tx"][f["src"]] = max(t_last["tx"][f["src"]], done[f["task"]])
        t_last["rx"][f["dst"]] = max(t_last["rx"][f["dst"]], done[f["task"]])

    # Business bytes per device and direction for the busy window (all tasks incl. control, each
    # byte once, credited when its task completed).
    biz = []
    for t in tasks:
        src, dst = (t["dst"], t["src"]) if t["op"] == "URMA_READ" else (t["src"], t["dst"])
        if t["task"] in done:
            biz.append((src, dst, t["bytes"], done[t["task"]]))

    def biz_bits(d, dr, t1):
        return 8.0 * sum(b for s, e, b, tc in biz if (s if dr == "tx" else e) == d and tc <= t1)

    def measure(d, h, dr, t0, t1, skip_iso, dead_after, with_biz=False):
        planes = used[dr].get(d)
        if not planes or t1 <= t0 + 2:
            return None
        meas = ceil = wire = cap_line = cap_cur = 0.0
        nports = 0
        span = None
        for p in sorted(planes):
            port = h["ports"][p]
            if skip_iso and (d, port) in iso:
                continue
            s = samples.get((d, port))
            if not s:
                continue
            dt, caps, txw, rxw, txp, rxp = _delta(
                s, t0, t1, dead_after if (d, port) in iso else None)
            if dt <= 0:
                continue
            span = dt
            w, pay = (txw, txp) if dr == "tx" else (rxw, rxp)
            cap_bytes = caps[0] if dr == "tx" else caps[1]
            nondata = max(0.0, w - pay * DATA_W)
            own = max(0.0, cap_bytes - nondata) / DATA_W
            # The far ends of this plane can absorb/supply at most their own port capacity in the
            # opposite direction (an isolated or degraded far-end port lowers this device's
            # reachable ceiling on that plane). Upper bound: far-end ACK overhead is ignored.
            far = 0.0
            for e in peers[dr][d][p]:
                if skip_iso and (e, hosts[e]["ports"][p]) in iso:
                    continue
                se = samples.get((e, hosts[e]["ports"][p]))
                if not se:
                    continue
                _, ecaps, *_ = _delta(se, t0, t1,
                                      dead_after if (e, hosts[e]["ports"][p]) in iso else None)
                far += (ecaps[1] if dr == "tx" else ecaps[0]) / DATA_W
            meas += pay * 8 / dt
            ceil += min(own, far) * 8 / dt
            wire += w * 8 / dt
            cap_line += line / 1e9
            cap_cur += cap_bytes * 8 / dt
            nports += 1
        if nports == 0 or ceil <= 0:
            return None
        need = dev_bits[dr][d] / 1e3 / t_ideal  # Gbit*1e3/us -> Gbps
        r = dict(meas=meas, ceil=ceil, util=meas / ceil, line=meas / cap_line,
                 wire=wire / cap_cur if cap_cur else 0.0, need=need / ceil, ports=nports)
        if with_biz:
            r["util_biz"] = biz_bits(d, dr, t1) / span / ceil
        return r

    out = {}
    for wname, (t0, t1) in list(wins.items()) + [("busy", (0.0, 0.0))]:
        per = {}
        for d, h in hosts.items():
            rec = {}
            for dr in ("tx", "rx"):
                if wname == "busy":
                    if d not in t_last[dr]:
                        continue
                    r = measure(d, h, dr, 0.0, t_last[dr][d], False, tf, with_biz=True)
                    if r:
                        r["t_last_us"] = t_last[dr][d]
                else:
                    r = measure(d, h, dr, t0, t1, wname == "post", tf)
                if r:
                    rec[dr] = r
            if rec:
                per[d] = rec
        out[wname] = per
    fi = meta.get("fault_info") or {}
    if fi.get("dev") == "server0":
        affected = [h["id"] for h in meta["hosts"]
                    if h["server"] == 0 and fi.get("plane", 3) in h["planes"]]
    elif isinstance(fi.get("dev"), int):
        affected = [fi["dev"]]
    else:
        affected = []
    return dict(case=os.path.basename(os.path.normpath(case)), scenario=meta["scenario"],
                affected=affected,
                fault=meta["fault"], scheme=meta["scheme"], seed=meta["seed"],
                types={str(d): h["type"] for d, h in hosts.items()},
                windows={k: list(v) for k, v in wins.items()},
                dev={w: {str(d): v for d, v in per.items()} for w, per in out.items()})


def _safe(case):
    try:
        return device_windows(case)
    except Exception as e:  # keep going; report the failure
        return dict(case=os.path.basename(os.path.normpath(case)), error=str(e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="+")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    with cf.ProcessPoolExecutor(a.jobs) as ex, open(a.out, "w") as f:
        for r in ex.map(_safe, a.cases, chunksize=1):
            if r is not None:
                f.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    main()
