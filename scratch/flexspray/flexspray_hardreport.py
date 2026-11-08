#!/usr/bin/env python3
"""Hard link-down (link_health DOWN) recovery report, one row per case.

  done        all bulk tasks completed (no permanent hang)
  eff_fixed   T_ideal (capacity of the dead resource is 0 from t_f) / makespan
  lost_KB     bytes the dead links actually dropped (sum of hard_drop_bytes, both ends)
  redisp      segments re-sent on another TP after their TP was declared dead (FailureEvent)
  t_local     first local detection (own port down) after t_f, us
  t_remote    first / last remote detection (probe timeout) after t_f, us
  false_dead  TPs declared dead by probe timeout whose path was not actually broken
              (their own port and the peer port are both up) - must be 0
  suspects    reversible suspicions (cleared by a probe reply or by ACK progress)
Usage: flexspray_hardreport.py MATRIX_DIR [...] [--md]
"""
import argparse
import csv
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze as A  # noqa: E402


def one(case, rec):
    meta, tasks = A.load_case(case)
    tf = (meta.get("fault_ns") or 0) / 1000.0
    lost = 0
    last = {}
    gp = os.path.join(case, "runlog", "GoodputSample.csv")
    if os.path.exists(gp):
        with open(gp) as f:
            for r in csv.DictReader(f):
                last[(r["node"], r["port"])] = int(r.get("hard_drop_bytes") or 0)
    lost = sum(last.values())
    redisp = 0
    t_local, t_remote, suspects, dead_remote = [], [], 0, []
    fe = os.path.join(case, "runlog", "FailureEvent.csv")
    if os.path.exists(fe):
        with open(fe) as f:
            for r in csv.DictReader(f):
                t = int(r["time_ns"]) / 1000.0 - tf
                ev = r["event"]
                if ev.startswith("REDISPATCH"):
                    redisp += int(ev.split()[1])
                elif ev == "local-port-down":
                    t_local.append(t)
                elif ev == "probe-timeout":
                    t_remote.append(t)
                    dead_remote.append((int(r["node"]), int(r["sport"]), int(r["dest"])))
                elif ev == "SUSPECT":
                    suspects += 1
    # A remote-detected dead TP is legitimate if its peer device has a down port on the same
    # plane (TP sport == plane index for devices with all 8 planes; check via fault rows).
    down_dev_ports = set()
    hosts = meta["hosts"]
    l1 = {v: tuple(map(int, k.split(","))) for k, v in meta["l1"].items()}
    for (_, node, port, action, _v) in meta.get("fault_rows") or []:
        if action == "DOWN" and node < len(hosts):
            down_dev_ports.add((node, port))
        elif action == "DOWN_NODE" and node in l1:
            p, s = l1[node]
            for h in hosts:
                if h["server"] == s and p in h["ports"]:
                    down_dev_ports.add((h["id"], h["ports"][p]))
        elif action == "DOWN" and node in l1:
            pass  # fabric link: no device port is down; TPs must survive via retransmission
    false_dead = 0
    for (node, sport, dest) in dead_remote:
        h = hosts[node]
        plane = next(p for p, pt in h["ports"].items() if pt == sport)
        peer = hosts[dest]
        peer_port = peer["ports"].get(plane)
        if (node, sport) not in down_dev_ports and (dest, peer_port) not in down_dev_ports:
            false_dead += 1
    return dict(case=os.path.basename(case), scenario=meta["scenario"], fault=meta["fault"],
                scheme=meta["scheme"], seed=meta["seed"],
                done=rec.get("missing_tasks", 1) == 0 and rec.get("rc") == 0,
                eff_fixed=rec.get("eff_fixed"), makespan=rec.get("makespan_us"),
                lost_kb=lost / 1024, redisp=redisp,
                t_local=min(t_local) if t_local else None,
                t_remote_first=min(t_remote) if t_remote else None,
                t_remote_last=max(t_remote) if t_remote else None,
                false_dead=false_dead, suspects=suspects)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    a = ap.parse_args()
    rows = []
    for d in a.dirs:
        recs = {}
        for l in open(os.path.join(d, "results.jsonl")):
            r = json.loads(l)
            recs[r["case"]] = r
        for c, r in sorted(recs.items()):
            rows.append(one(os.path.join(d, c), r))
    print(json.dumps(rows))


if __name__ == "__main__":
    main()
