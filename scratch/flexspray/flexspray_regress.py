#!/usr/bin/env python3
"""FlexSpray scripted regressions (system level; the unit tests are in unified-bus-flexspray).

Each check states what it guards and what a failure means. Exit status 1 if any check fails.

  fence_on     recovery suite with the replay fence (default): every case passes P1-P6.
  fence_off    negative control: near_stale_slow WITHOUT the fence must FAIL P5 (an old-path copy
               executed after the initiator completed the segment). If it passes, the case no
               longer exercises the stale-delivery hazard and fence_on proves nothing.
  tail4k       mixed seed 1 with 4 KB tail segments (TailBytes=128 KB): every task completes.
               Before the fix the WQE TaSsn reservation ignored the extra tail segments, the WQE
               was declared complete early, its unsent segments were canceled, and a TAACK queued
               behind them on the TP never left (one a2a WRITE hung).

Usage: flexspray_regress.py --out DIR [--bin BIN] [--only fence_on,fence_off,tail4k]
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
P_KEYS = ["P1_segments_completed_once", "P1_tasks_completed_once", "P2_every_completion_executed",
          "P3_coverage_exact", "P5_no_exec_after_completion", "P6_credits_returned_on_live_ports"]


def suite(out, binary, scheme, only=None):
    cmd = [PY, os.path.join(HERE, "flexspray_recovery_suite.py"), "--out", out, "--bin", binary,
           "--scheme", scheme]
    if only:
        cmd += ["--only", only]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    return json.load(open(os.path.join(out, "oracle.json")))


def check_fence_on(root, binary):
    res = suite(os.path.join(root, "fence_on"), binary, "flex+spraycc")
    bad = {n: [k for k in P_KEYS if r.get(k) is False] + (["P4"] if r.get("P4_last_writer") is False else [])
           for n, r in res.items()}
    bad = {n: v for n, v in bad.items() if v or res[n].get("rc") != 0 or res[n].get("tasks_missing")}
    return not bad, f"{len(res)} cases, failing: {bad or 'none'}"


def check_fence_off(root, binary):
    res = suite(os.path.join(root, "fence_off"), binary,
                "flex+spraycc+ns3::UbTransaction::ReplayFence=false", only="near_stale_slow")
    r = res["near_stale_slow"]
    ok = r.get("P5_no_exec_after_completion") is False
    return ok, f"P5={r.get('P5_no_exec_after_completion')} late_execs={r.get('late_execs_after_completion')}"


def check_tail4k(root, binary):
    name = "tail4k"
    scheme = ("flex+spraycc+ns3::UbJetty::TailBytes=131072+ns3::UbJetty::TailSegmentBytes=4096")
    subprocess.run([PY, os.path.join(HERE, "flexspray_matrix.py"), "--name", os.path.join(root, name),
                    "--scenarios", "mixed", "--faults", "F0", "--schemes", scheme, "--seeds", "1",
                    "--jobs", "1", "--stop-ms", "3", "--bin", binary], check=True,
                   stdout=subprocess.DEVNULL)
    rows = [json.loads(l) for l in open(os.path.join(root, name, "results.jsonl"))]
    r = rows[-1]
    ok = r.get("missing_tasks") == 0
    return ok, f"missing_tasks={r.get('missing_tasks')} eff={r.get('eff')}"


CHECKS = {"fence_on": check_fence_on, "fence_off": check_fence_off, "tail4k": check_tail4k}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--bin", default=os.path.join(HERE, "..", "..", "build", "scratch",
                                                  "ns3.44-ub-quick-example"))
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    names = a.only.split(",") if a.only else list(CHECKS)
    failed = 0
    for n in names:
        ok, msg = CHECKS[n](os.path.abspath(a.out), os.path.abspath(a.bin))
        print(f"{'PASS' if ok else 'FAIL'} {n}: {msg}")
        failed += not ok
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
