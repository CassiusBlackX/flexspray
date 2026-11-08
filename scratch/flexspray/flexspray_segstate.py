#!/usr/bin/env python3
"""READ business-unit state decomposition (needs UB_RECOVERY_TRACE_ENABLE: SegState.csv,
TargetExec.csv, SegComplete.csv).

One business unit = one READ segment, key (requester, jetty, TaSsn). States (exclusive in time):
  S0  request admitted to a requester TP, not fully sent
  S1  request sent, response not generated yet (in flight / queued / executing at the responder)
  S2  response generated, not admitted to a responder TP (transaction queue; no PSN)
  S3  response admitted to a TP (PSN assigned), no byte sent
  S4  response partly sent
  S5  response fully sent, not completed at the requester (in switch queues / links)
  done
A unit is counted in exactly one state at any time, so per-state sums never double count.

Usage: flexspray_segstate.py CASE --node N --plane P [--times 40,50,60,100,160]
Reports, at each time, bytes per state for (a) units whose requester is N and whose response uses
plane P (N's ingress), (b) units whose responder is N on plane P (N's egress); then the bytes in
S2/S3 (legally re-bindable: no byte in the network) vs S4/S5 at the fault time, and the units that
finish last with their state durations.
"""
import argparse
import collections
import csv
import json
import os


def load(case):
    ev = collections.defaultdict(dict)
    meta = json.load(open(os.path.join(case, "case_meta.json")))
    port_plane = {}
    for h in meta["hosts"]:
        for p, port in h["ports"].items():
            port_plane[(h["id"], int(port))] = int(p)
    with open(os.path.join(case, "runlog", "SegState.csv")) as f:
        for r in csv.DictReader(f):
            if int(r["opcode"]) != 6:  # READ units only
                continue
            k = (int(r["requester"]), int(r["jetty"]), int(r["tassn"]))
            side = "resp" if r["response"] == "1" else "req"
            t = int(r["time_ns"]) / 1000.0
            name = f"{side}_{r['event']}"
            if name not in ev[k] or r["event"] == "TX_LAST":   # keep the last TX_LAST (retransmits)
                ev[k][name] = t
            ev[k]["bytes"] = int(r["bytes"])
            if side == "resp":
                ev[k]["responder"] = int(r["responder"])
                ev[k]["resp_plane"] = port_plane.get((int(r["node"]), int(r["port"])))
            else:
                ev[k]["req_plane"] = port_plane.get((int(r["node"]), int(r["port"])))
    with open(os.path.join(case, "runlog", "TargetExec.csv")) as f:
        for r in csv.DictReader(f):
            if int(r["opcode"]) != 6:
                continue
            k = (int(r["initiator"]), int(r["jetty"]), int(r["tassn"]))
            if k in ev:
                ev[k].setdefault("exec", int(r["time_ns"]) / 1000.0)
    with open(os.path.join(case, "runlog", "SegComplete.csv")) as f:
        for r in csv.DictReader(f):
            k = (int(r["node"]), int(r["jetty"]), int(r["tassn"]))
            if k in ev:
                ev[k]["done"] = int(r["time_ns"]) / 1000.0
    return meta, ev


ORDER = ["S0", "S1", "S2", "S3", "S4", "S5"]


def state(u, t):
    marks = [("S0", u.get("req_ADMIT")), ("S1", u.get("req_TX_LAST")), ("S2", u.get("exec")),
             ("S3", u.get("resp_ADMIT")), ("S4", u.get("resp_TX_FIRST")), ("S5", u.get("resp_TX_LAST")),
             ("done", u.get("done"))]
    cur = None
    for name, tm in marks:
        if tm is None:
            break
        if tm <= t:
            cur = name
        else:
            break
    return cur


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case")
    ap.add_argument("--node", type=int, required=True)
    ap.add_argument("--plane", type=int, required=True)
    ap.add_argument("--times", default="40,50,60,100,160,200")
    a = ap.parse_args()
    meta, ev = load(a.case)
    tf = (meta.get("fault_ns") or 0) / 1000.0
    groups = {
        "a: requester=node, response plane=P (node's ingress)":
            [u for k, u in ev.items() if k[0] == a.node and u.get("resp_plane", u.get("req_plane")) == a.plane],
        "b: responder=node, plane=P (node's egress)":
            [u for k, u in ev.items() if u.get("responder") == a.node and u.get("resp_plane") == a.plane],
    }
    out = {"tf_us": tf}
    for g, units in groups.items():
        rows = {}
        for t in [float(x) for x in a.times.split(",")] + [tf]:
            c = collections.Counter()
            for u in units:
                s = state(u, t)
                if s and s != "done":
                    c[s] += u["bytes"]
            rows[f"{t:g}us"] = {s: round(c[s] / 1024) for s in ORDER if c[s]}
        at_f = rows[f"{tf:g}us"]
        mig = sum(at_f.get(s, 0) for s in ("S0", "S1", "S2", "S3"))
        fixed = sum(at_f.get(s, 0) for s in ("S4", "S5"))
        last = sorted((u for u in units if "done" in u), key=lambda u: -u["done"])[:5]
        out[g] = dict(units=len(units), kb_per_state=rows,
                      at_fault_kb=dict(not_in_network_S0toS3=mig, in_network_S4S5=fixed),
                      last=[{k: (round(v, 1) if isinstance(v, float) else v) for k, v in u.items()} for u in last])
    print(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
