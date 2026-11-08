#!/usr/bin/env python3
"""Probe / retransmission-timeout / CC interaction report, one row per case (FailureEvent.csv).

  suspects / recovers   reversible suspicions and how many were cleared (RECOVER)
  dead_local            TPs declared dead by their own port going down
  dead_fence            TPs declared dead because the peer re-sent their pair's work elsewhere
                        (received replay fence: the pair is dead in both directions)
  dead_remote           TPs declared dead by probe timeout; false_dead = those whose path was not
                        broken (own port and the peer's port on that plane are up) - must be 0
  probes                PROBEs sent; replies split into CLEARS (ended a suspicion), DUPLICATE (TP
                        already healthy) and LATE (TP already dead); unanswered = probes - replies
  rto                   retransmission timeouts; rto_fault_plane = on a TP whose path is broken or
                        that uses the faulted plane of a hard fault (packets were lost there),
                        rto_spurious = all others (no loss on their path: queueing outlasted the
                        RTO; with a graceful fault every RTO is spurious); rto_suspect = timeouts
                        while the TP was suspect
  det_fault_us          remote detection time from the fault: t_dead - t_f   (min/median/max)
  det_use_us            remote detection time from first use: t_dead - silence start (the last
                        answer from the peer, or the first send after an idle period)
Usage: flexspray_fdreport.py MATRIX_DIR [...] [--md]
"""
import argparse
import csv
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402


def _stats(xs):
    if not xs:
        return None
    return [round(min(xs), 2), round(statistics.median(xs), 2), round(max(xs), 2)]


def one(case):
    meta, _tasks = A.load_case(case)
    tf_ns = meta.get("fault_ns") or 0
    hosts = meta["hosts"]
    hard = any(r[3] in ("DOWN", "DOWN_NODE") for r in (meta.get("fault_rows") or []))
    # broken device ports (same rule as flexspray_hardreport)
    down = set()
    l1 = {v: tuple(map(int, k.split(","))) for k, v in meta["l1"].items()}
    for (_, node, port, action, _v) in meta.get("fault_rows") or []:
        if action == "DOWN" and node < len(hosts):
            down.add((node, port))
        elif action == "DOWN_NODE" and node in l1:
            p, s = l1[node]
            for h in hosts:
                if h["server"] == s and p in h["ports"]:
                    down.add((h["id"], h["ports"][p]))

    def plane_of(node, sport):
        h = hosts[node] if node < len(hosts) else None
        if h is None:
            return None
        return next((p for p, pt in h["ports"].items() if pt == sport), None)

    # Faulted plane: from a device port, or from the L1 switch (fabric link / whole L1).
    fplane = None
    for (_, node, port, action, _v) in meta.get("fault_rows") or []:
        if node < len(hosts):
            fplane = plane_of(node, port)
        elif node in l1:
            fplane = l1[node][0]

    def broken(node, sport, dest):
        p = plane_of(node, sport)
        peer_port = hosts[dest]["ports"].get(p) if p is not None and dest < len(hosts) else None
        return (node, sport) in down or (dest, peer_port) in down

    r = dict(case=os.path.basename(os.path.normpath(case)), scenario=meta["scenario"],
             fault=meta["fault"], scheme=meta["scheme"], seed=meta["seed"], suspects=0,
             recovers=0, dead_local=0, dead_fence=0, dead_remote=0, false_dead=0, probes=0, replies_clears=0,
             replies_duplicate=0, replies_late=0, rto=0, rto_fault_plane=0, rto_spurious=0,
             rto_suspect=0, rto_bytes=0)
    det_fault, det_use = [], []
    silence = {}
    fe = os.path.join(case, "runlog", "FailureEvent.csv")
    if os.path.exists(fe):
        for e in csv.DictReader(open(fe)):
            ev, t = e["event"], int(e["time_ns"])
            node, sport, dest = int(e["node"]), int(e["sport"]), int(e["dest"])
            key = (node, int(e["tpn"]))
            if ev == "SUSPECT":
                r["suspects"] += 1
            elif ev == "RECOVER":
                r["recovers"] += 1
            elif ev == "local-port-down":
                r["dead_local"] += 1
            elif ev == "replay-fence":
                r["dead_fence"] += 1
            elif ev.startswith("SILENCE"):
                silence[key] = int(ev.split()[1].split("=")[1])
            elif ev == "probe-timeout":
                r["dead_remote"] += 1
                det_fault.append((t - tf_ns) / 1000.0)
                if key in silence:
                    det_use.append((t - silence[key]) / 1000.0)
                if not broken(node, sport, dest):
                    r["false_dead"] += 1
            elif ev.startswith("PROBE_REPLY"):
                kind = ev.split()[1].lower()
                r["replies_" + kind] += 1
            elif ev.startswith("PROBE "):
                r["probes"] += 1
            elif ev.startswith("RTO"):
                kv = dict(x.split("=") for x in ev.split()[1:])
                r["rto"] += 1
                r["rto_bytes"] += int(kv["bytes"])
                r["rto_suspect"] += kv["state"] == "SUSPECT"
                if hard and (broken(node, sport, dest) or plane_of(node, sport) == fplane):
                    r["rto_fault_plane"] += 1
                else:
                    r["rto_spurious"] += 1
    r["unanswered"] = r["probes"] - r["replies_clears"] - r["replies_duplicate"] - r["replies_late"]
    r["det_fault_us"] = _stats(det_fault)
    r["det_use_us"] = _stats(det_use)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--md", action="store_true")
    a = ap.parse_args()
    rows = []
    for d in a.dirs:
        for c in sorted(os.listdir(d)):
            p = os.path.join(d, c)
            if os.path.isdir(p) and os.path.exists(os.path.join(p, "runlog")):
                rows.append(one(p))
    if not a.md:
        print(json.dumps(rows))
        return
    cols = ["case", "suspects", "recovers", "dead_local", "dead_fence", "dead_remote", "false_dead", "probes",
            "replies_clears", "replies_duplicate", "replies_late", "unanswered", "rto",
            "rto_fault_plane", "rto_spurious", "rto_suspect", "det_fault_us", "det_use_us"]
    print("| " + " | ".join(cols) + " |")
    print("|" + "---|" * len(cols))
    for r in rows:
        print("| " + " | ".join(str(r[c]) for c in cols) + " |")


if __name__ == "__main__":
    main()
