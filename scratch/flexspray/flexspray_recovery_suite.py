#!/usr/bin/env python3
"""Minimal hard link-down recovery cases + oracle (see flexspray_recovery_oracle.py).

Two servers, the FlexSpray scheme (or --scheme), small transfers so a fault at 5 us lands in the
middle. Each case states the failure and the traffic; the oracle verdicts are printed per case.

Usage: flexspray_recovery_suite.py --out DIR [--bin BIN] [--scheme flex+spraycc] [--only NAME,...]
"""
import argparse
import concurrent.futures as cf
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_case as C  # noqa: E402
import flexspray_recovery_oracle as O  # noqa: E402

MB = 1 << 20


def cases(t):
    a, b = t.find_host("NPU", 0, 0), t.find_host("NPU", 1, 0)
    p = 3
    l1_b = t.l1[(p, 1)]
    up = t.l1_up_port[(p, 0, 0, 0)]
    W, R = "URMA_WRITE", "URMA_READ"
    far = [(5000, b["id"], b["ports"][p], "DOWN", "")]
    near = [(5000, a["id"], a["ports"][p], "DOWN", "")]
    return {
        # far-end port dies mid-WRITE: data already delivered on plane 3 has its ACKs lost
        "write_far": ([(a["id"], b["id"], 4 * MB, W, 0, "")], far),
        "write_near": ([(a["id"], b["id"], 4 * MB, W, 0, "")], near),
        "read_far": ([(a["id"], b["id"], 4 * MB, R, 0, "")], far),
        "read_near": ([(a["id"], b["id"], 4 * MB, R, 0, "")], near),
        # two dependency-ordered WRITEs to the same addresses; the failure hits the first one
        "overlap_ordered": ([(a["id"], b["id"], 4 * MB, W, 0, ""),
                             (a["id"], b["id"], 4 * MB, W, 1, "0")], far),
        # concurrent overlapping WRITEs and a concurrent READ of the same region (no order promise)
        "overlap_concurrent": ([(a["id"], b["id"], 4 * MB, W, 0, ""),
                                (a["id"], b["id"], 4 * MB, W, 0, ""),
                                (a["id"], b["id"], 4 * MB, R, 0, "")], far),
        # both directions at once: b also writes to a while a's plane-3 port dies
        "bidir": ([(a["id"], b["id"], 4 * MB, W, 0, ""), (b["id"], a["id"], 4 * MB, W, 0, ""),
                   (b["id"], a["id"], 4 * MB, R, 0, "")], near),
        # fabric uplink dies: packets queued on it are lost, the TPs survive (GBN retransmission)
        "fabric_uplink": ([(a["id"], b["id"], 4 * MB, W, 0, ""), (a["id"], b["id"], 4 * MB, R, 0, "")],
                          [(5000, t.l1[(p, 0)], up, "DOWN", "")]),
        # whole plane-3 L1 of server 1 dies
        "l1_node": ([(a["id"], b["id"], 4 * MB, W, 0, ""), (a["id"], b["id"], 4 * MB, R, 0, "")],
                    [(5000, l1_b, 0, "DOWN_NODE", "")]),
        # near-end port dies while the target's ingress is congested by an incast from the other
        # seven NPUs: packets that left A on plane 3 before the failure sit in a deep queue and
        # may reach B after A already re-sent them elsewhere (stale-delivery hazard for ordered
        # overlapping writes).
        "near_stale": ([(a["id"], b["id"], 4 * MB, W, 0, ""), (a["id"], b["id"], 4 * MB, W, 1, "0")] +
                       [(t.find_host("NPU", 0, i)["id"], b["id"], 16 * MB, W, 2, "")
                        for i in range(1, 8)], near),
        "near_stale_read": ([(a["id"], b["id"], 4 * MB, R, 0, ""), (a["id"], b["id"], 4 * MB, W, 1, "0")] +
                            [(t.find_host("NPU", 0, i)["id"], b["id"], 16 * MB, W, 2, "")
                             for i in range(1, 8)], near),
        # stale-delivery stress: B's plane-3 port runs at 1/4 rate from t=0 (deep, slow queue on
        # the old path), then A's plane-3 port dies; re-sent copies travel fast planes. A second,
        # dependent WRITE to the same addresses follows; a stale copy executed after it would
        # overwrite newer data.
        "near_stale_slow": ([(a["id"], b["id"], 4 * MB, W, 0, ""), (a["id"], b["id"], 4 * MB, W, 1, "0"),
                             (a["id"], b["id"], 4 * MB, W, 2, "1")] +
                            [(t.find_host("NPU", 0, i)["id"], b["id"], 4 * MB, W, 3, "")
                             for i in range(1, 8)],
                            [(0, b["id"], b["ports"][p], "DEGRADE", "1/4")] + near),
        # failure right at the start (before any ACK) and near the end (most data acknowledged)
        "write_far_early": ([(a["id"], b["id"], 4 * MB, W, 0, "")],
                            [(300, b["id"], b["ports"][p], "DOWN", "")]),
        "write_far_late": ([(a["id"], b["id"], 4 * MB, W, 0, "")],
                           [(11000, b["id"], b["ports"][p], "DOWN", "")]),
    }


def build(root, name, tasks, faults, scheme):
    d = os.path.join(root, name)
    os.makedirs(d, exist_ok=True)
    C.main(["--out", d, "--scenario", "d2d", "--scheme", scheme, "--servers", "2",
            "--sample-ns", "2000"])
    with open(os.path.join(d, "traffic.csv"), "w") as f:
        f.write("taskId,sourceNode,destNode,dataSize(Byte),opType,priority,delay,phaseId,dependOnPhases\n")
        for i, (s, dd, n, op, ph, dep) in enumerate(tasks):
            f.write(f"{i},{s},{dd},{n},{op},7,0ns,{ph},{dep}\n")
    with open(os.path.join(d, "link_health.csv"), "w") as f:
        f.write("timeNs,nodeId,portId,action,value\n")
        for r in faults:
            f.write(",".join(map(str, r)) + "\n")
    with open(os.path.join(d, "network_attribute.txt"), "a") as f:
        f.write('global UB_RECOVERY_TRACE_ENABLE "true"\n')
    return d


def run(args):
    d, binary = args
    with open(os.path.join(d, "stdout.txt"), "w") as out:
        rc = subprocess.run([binary, f"--case-path={d}", "--stop-ms=1"], stdout=out,
                            stderr=subprocess.STDOUT, timeout=1800).returncode
    try:
        res = O.check(d)
    except Exception as e:  # trace files missing etc.
        res = {"error": str(e)}
    res["rc"] = rc
    return os.path.basename(d), res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--bin", default=os.path.join(HERE, "..", "..", "build", "scratch",
                                                  "ns3.44-ub-quick-example"))
    ap.add_argument("--scheme", default="flex+spraycc")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    t = C.Topo(servers=2)
    cs = cases(t)
    if a.only:
        cs = {k: v for k, v in cs.items() if k in a.only.split(",")}
    dirs = [build(a.out, n, tk, fl, a.scheme) for n, (tk, fl) in cs.items()]
    out = {}
    with cf.ProcessPoolExecutor(len(dirs)) as ex:
        for name, res in ex.map(run, [(d, os.path.abspath(a.bin)) for d in dirs]):
            out[name] = res
    json.dump(out, open(os.path.join(a.out, "oracle.json"), "w"), indent=1)
    keys = ["rc", "tasks_missing", "P1_segments_completed_once", "P1_tasks_completed_once",
            "P2_every_completion_executed", "P3_coverage_exact", "P4_last_writer",
            "P5_no_exec_after_completion", "duplicate_target_executions", "duplicate_target_bytes",
            "late_execs_after_completion", "redispatched_segments", "P6_credits_returned_on_live_ports",
            "credits_stuck_live", "credits_stuck_on_down_ports", "error"]
    for name in cs:
        r = out[name]
        print(name.ljust(20), " ".join(f"{k.split('_')[0] if k.startswith('P') else k}={r.get(k)}"
                                       for k in keys if k in r))


if __name__ == "__main__":
    main()
