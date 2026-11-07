#!/usr/bin/env python3
"""Congestion and overhead metrics per case, from GoodputSample.csv (queue_bytes, fc_blocked_ns,
reorder columns) and the matrix results.jsonl (efficiency, control-message latency).

Per case:
  q_peak_kb       max output-queue bytes (switch VOQ + egress) over all switch ports and samples
  q_mean_peak_kb  mean over samples (until 0.95 x first completion) of the largest switch queue
  blocked_sw      CBFC credit-blocked time summed over switch ports / (ports x makespan)
  blocked_sw_max  the most-blocked switch port's blocked fraction
  blocked_host    the same over host ports (backpressure that reached the senders)
  reorder_max     largest receiver reorder distance (PSN) on any host port
  ooo_buf_max     largest out-of-order buffer of any TP (packets)
  ooo_frac        out-of-order arrivals / all data arrivals (host ports)
Usage: flexspray_ccmetrics.py MATRIX_DIR [MATRIX_DIR ...] --out metrics.jsonl [--jobs N]
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1")
import argparse
import concurrent.futures as cf
import csv
import json
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402


def one(arg):
    case, rec = arg
    try:
        meta, tasks = A.load_case(case)
        nh = len(meta["hosts"])
        start, done = A.read_tasks(case)
        flows = A.bulk_flows(meta, tasks)
        comp = [done[f["task"]] for f in flows if f["task"] in done]
        end_ns = 0.95 * min(comp) * 1000 if comp else 0
        mk_ns = (max(comp) if comp else 0) * 1000
        last = {}
        per_t = {}
        qpeak = 0
        with open(os.path.join(case, "runlog", "GoodputSample.csv")) as f:
            for r in csv.DictReader(f):
                t, n, p = int(r["time_ns"]), int(r["node"]), int(r["port"])
                last[(n, p)] = r
                if n >= nh:
                    q = int(r["queue_bytes"])
                    qpeak = max(qpeak, q)
                    if t <= end_ns and q > per_t.get(t, -1):
                        per_t[t] = q
        sw = [(k, r) for k, r in last.items() if k[0] >= nh]
        hs = [(k, r) for k, r in last.items() if k[0] < nh]
        bl = lambda rows: [int(r["fc_blocked_ns"]) / mk_ns for _, r in rows] if mk_ns else [0]
        bsw, bh = bl(sw), bl(hs)
        rx_pkts = sum(int(r["rx_payload_bytes"]) for _, r in hs) / 4096.0
        out = dict(case=os.path.basename(case), scenario=meta["scenario"], fault=meta["fault"],
                   scheme=meta["scheme"], seed=meta["seed"],
                   eff=rec.get("eff"), victim_eff=rec.get("victim_eff"),
                   ctrl_p99_us=(rec.get("ctrl_lat_us") or {}).get("p99"),
                   ctrl_max_us=(rec.get("ctrl_lat_us") or {}).get("max"),
                   q_peak_kb=qpeak / 1024,
                   q_mean_peak_kb=(sum(per_t.values()) / len(per_t) / 1024) if per_t else 0,
                   blocked_sw=sum(bsw) / len(bsw), blocked_sw_max=max(bsw),
                   blocked_host=sum(bh) / len(bh), blocked_host_max=max(bh),
                   reorder_max=max(int(r["max_reorder_psn"]) for _, r in hs),
                   ooo_buf_max=max(int(r["max_ooo_buffered"]) for _, r in hs),
                   ooo_frac=(sum(int(r["ooo_pkts"]) for _, r in hs) / rx_pkts) if rx_pkts else 0)
        return out
    except Exception as e:
        return dict(case=os.path.basename(case), error=str(e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=16)
    a = ap.parse_args()
    items = []
    for d in a.dirs:
        recs = {}
        for l in open(os.path.join(d, "results.jsonl")):
            r = json.loads(l)
            recs[r["case"]] = r
        items += [(os.path.join(d, c), r) for c, r in recs.items()
                  if os.path.exists(os.path.join(d, c, "runlog", "GoodputSample.csv"))]
    with cf.ProcessPoolExecutor(a.jobs) as ex, open(a.out, "w") as f:
        for r in ex.map(one, items, chunksize=1):
            f.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    main()
