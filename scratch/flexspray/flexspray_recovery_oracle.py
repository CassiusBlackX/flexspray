#!/usr/bin/env python3
"""Recovery oracle: what a hard link-down + cross-TP re-dispatch run actually did.

Inputs (case run with UB_RECOVERY_TRACE_ENABLE=true): runlog/TargetExec.csv (every target-side
execution of a request), runlog/SegComplete.csv (every initiator segment completion),
runlog/TaskTrace_*.csv (WQE completion events), traffic.csv (tasks and phase dependencies).

What it can prove (event level - the simulator keeps no memory contents):
  P1 completion-once      every (initiator, jetty, wqe, TaSsn) completes exactly once and every task
                          ("WQE Completes") completes exactly once
  P2 executed             every completed segment was executed at least once at its target, with the
                          same opcode, address and length
  P3 coverage             the completed segments of each task cover [0, task bytes) exactly once
  P4 last-writer          for WRITEs between the same pair, at each address the LAST target execution
                          belongs to the task that is last in phase-dependency order (only checked
                          when the tasks are ordered by a dependency; concurrent overlapping writes
                          have no defined winner)
  P5 no-late-exec         no target execution of a segment after its initiator completion
It also counts what is NOT exactly-once: duplicate target executions (remote work done twice).
What it cannot prove: data contents, or ordering promises finer than segment granularity.
Usage: flexspray_recovery_oracle.py CASE_DIR [--json]
"""
import argparse
import collections
import csv
import glob
import json
import os

OP_READ, OP_WRITE = 6, 3


def load(case):
    exe = list(csv.DictReader(open(os.path.join(case, "runlog", "TargetExec.csv"))))
    comp = list(csv.DictReader(open(os.path.join(case, "runlog", "SegComplete.csv"))))
    tasks = list(csv.DictReader(open(os.path.join(case, "traffic.csv"))))
    done = collections.Counter()
    for fn in glob.glob(os.path.join(case, "runlog", "TaskTrace_*.csv")):
        for r in csv.DictReader(open(fn, errors="replace")):
            if r.get("event") == "WQE Completes":
                done[int(r["task_id"])] += 1
    return exe, comp, tasks, done


def check(case):
    exe, comp, tasks, done = load(case)
    res = {}
    # P1
    seg_count = collections.Counter((r["node"], r["jetty"], r["wqe"], r["tassn"]) for r in comp)
    res["P1_segments_completed_once"] = all(v == 1 for v in seg_count.values())
    res["P1_tasks_completed_once"] = all(done[int(t["taskId"])] == 1 for t in tasks)
    res["tasks_missing"] = [int(t["taskId"]) for t in tasks if done[int(t["taskId"])] == 0]
    # P2 / duplicates. Key: (initiator, jetty, TaSsn).
    execs = collections.defaultdict(list)
    for r in exe:
        execs[(r["initiator"], r["jetty"], r["tassn"])].append(r)
    p2 = True
    dup_execs = dup_bytes = 0
    late = 0
    for r in comp:
        k = (r["node"], r["jetty"], r["tassn"])
        ex = execs.get(k, [])
        if not ex or any(e["opcode"] != r["opcode"] or e["address"] != r["address"] or
                         e["bytes"] != r["bytes"] for e in ex):
            p2 = False
        if len(ex) > 1:
            dup_execs += len(ex) - 1
            dup_bytes += (len(ex) - 1) * int(r["bytes"])
        late += sum(1 for e in ex if int(e["time_ns"]) > int(r["time_ns"]))
    res["P2_every_completion_executed"] = p2
    res["duplicate_target_executions"] = dup_execs
    res["duplicate_target_bytes"] = dup_bytes
    res["P5_no_exec_after_completion"] = late == 0
    res["late_execs_after_completion"] = late
    res["redispatched_segments"] = sum(1 for r in comp if int(r["redispatches"]) > 0)
    # Segment offsets. The model gives every WRITE/READ segment the WQE base address (it keeps no
    # per-segment offset), but a WQE's segments carry consecutive TaSsns: the offset of a segment is
    # the sum of the sizes of the WQE's segments with smaller TaSsn.
    by_wqe = collections.defaultdict(list)
    for r in comp:
        by_wqe[(r["node"], r["jetty"], r["wqe"])].append((int(r["tassn"]), int(r["bytes"]), r))
    offset = {}   # (initiator, jetty, tassn) -> (wqe key, offset, bytes)
    sizes = collections.Counter()
    p3 = True
    for k, segs in by_wqe.items():
        segs.sort()
        tassns = [x[0] for x in segs]
        if len(set(tassns)) != len(tassns) or tassns != list(range(tassns[0], tassns[0] + len(tassns))):
            p3 = False   # a TaSsn missing or completed twice
        pos = 0
        for ts, b, r in segs:
            offset[(k[0], k[1], str(ts))] = (k, pos, b)
            pos += b
        sizes[pos] += 1
    expected = collections.Counter(int(t["dataSize(Byte)"]) for t in tasks)
    res["P3_coverage_exact"] = p3 and sizes == expected
    # P4 last writer for dependency-ordered overlapping writes between the same pair: all WQEs
    # between a pair write [0, size); region = offset within the WQE.
    deps = {int(t["phaseId"]): [int(x) for x in t["dependOnPhases"].split() if x] for t in tasks}
    pairs = collections.defaultdict(set)
    for t in tasks:
        if t["opType"] == "URMA_WRITE":
            pairs[(t["sourceNode"], t["destNode"])].add(int(t["phaseId"]))
    wqe_first = {k: min(int(x[2]["time_ns"]) for x in segs) for k, segs in by_wqe.items()}
    p4 = None
    violations = 0
    for (src, dst), phases in pairs.items():
        chain = sorted(phases)
        if len(chain) < 2 or not all(chain[i] in deps.get(chain[i + 1], [])
                                     for i in range(len(chain) - 1)):
            continue  # single or concurrent writers: no defined winner
        wqes = sorted((t, k) for k, t in wqe_first.items()
                      if k[0] == src and by_wqe[k][0][2]["peer"] == dst and
                      int(by_wqe[k][0][2]["opcode"]) == OP_WRITE)
        rank = {k: i for i, (t, k) in enumerate(wqes)}
        final_rank = len(wqes) - 1
        last = {}
        for e in exe:
            if e["initiator"] != src or e["target"] != dst or int(e["opcode"]) != OP_WRITE:
                continue
            o = offset.get((e["initiator"], e["jetty"], e["tassn"]))
            if o is None or o[0] not in rank:
                continue
            region = o[1]
            if region not in last or int(e["time_ns"]) >= last[region][0]:
                last[region] = (int(e["time_ns"]), rank[o[0]])
        p4 = True if p4 is None else p4
        bad = sum(1 for _, (t, r) in last.items() if r != final_rank)
        violations += bad
        if bad:
            p4 = False
    # P6 credit conservation: at the last sample (all tasks done), every port that is not an end
    # of a DOWN link has all its downstream credits back.
    gp = os.path.join(case, "runlog", "GoodputSample.csv")
    if os.path.exists(gp):
        down = set()
        lh = os.path.join(case, "link_health.csv")
        if os.path.exists(lh):
            topo = list(csv.DictReader(open(os.path.join(case, "topology.csv"))))
            for r in csv.DictReader(open(lh)):
                if r["action"] in ("DOWN", "DOWN_NODE"):
                    n, p = r["nodeId"], r["portId"]
                    for l in topo:
                        ends = [(l["nodeId1"], l["portId1"]), (l["nodeId2"], l["portId2"])]
                        if (n, p) in ends or (r["action"] == "DOWN_NODE" and n in (l["nodeId1"], l["nodeId2"])):
                            down.update(ends)
        last = {}
        for r in csv.DictReader(open(gp)):
            last[(r["node"], r["port"])] = int(r.get("fc_consumed_bytes") or 0)
        live_stuck = {k: v for k, v in last.items() if v and k not in down}
        res["P6_credits_returned_on_live_ports"] = not live_stuck
        res["credits_stuck_live"] = sum(live_stuck.values())
        res["credits_stuck_on_down_ports"] = sum(v for k, v in last.items() if k in down)
    # Failure detector: suspicions, probe-timeout deaths; without any DOWN row every probe-timeout
    # is a false death.
    fe = os.path.join(case, "runlog", "FailureEvent.csv")
    ev = list(csv.DictReader(open(fe))) if os.path.exists(fe) else []
    has_down = False
    lh = os.path.join(case, "link_health.csv")
    if os.path.exists(lh):
        has_down = any(r["action"] in ("DOWN", "DOWN_NODE") for r in csv.DictReader(open(lh)))
    res["suspects"] = sum(1 for e in ev if e["event"] == "SUSPECT")
    res["probe_timeouts"] = sum(1 for e in ev if e["event"] == "probe-timeout")
    res["false_dead_no_down"] = 0 if has_down else res["probe_timeouts"]
    res["P4_last_writer"] = p4  # None = no dependency-ordered overlapping writes in this case
    res["last_writer_violations"] = violations
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case")
    a = ap.parse_args()
    print(json.dumps(check(a.case), indent=1))


if __name__ == "__main__":
    main()
