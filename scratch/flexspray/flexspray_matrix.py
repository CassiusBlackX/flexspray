#!/usr/bin/env python3
"""Run a FlexSpray experiment matrix in parallel and collect analysis results.

Usage:
  flexspray_matrix.py --name NAME --scenarios d2d,a2a --faults F0,F4 --schemes spray,flex+spraycc \
      [--seeds 1,2,3] [--jobs 48] [--bin PATH] [--root DIR]

Each case lives in ROOT/NAME/<scenario>__<fault>__<scheme>__s<seed>/ with its own runlog; results are
appended to ROOT/NAME/results.jsonl (one analysis record per case; existing records are reused
unless --force). Runs are single-threaded processes, so many run concurrently.
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
import argparse
import concurrent.futures as cf
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flexspray_analyze  # noqa: E402
import flexspray_case  # noqa: E402

DEFAULT_BIN = os.path.join(HERE, "..", "..", "build", "scratch", "ns3.44-ub-quick-example")


def case_id(sc, fault, scheme, seed):
    return f"{sc}__{fault}__{scheme.replace('/', '_')}__s{seed}"


def run_one(root, sc, fault, scheme, seed, binary, extra, scale, timeout, stop_ms=3):
    cid = case_id(sc, fault, scheme, seed)
    d = os.path.join(root, cid)
    os.makedirs(d, exist_ok=True)
    flexspray_case.main(["--out", d, "--scenario", sc, "--fault", fault, "--scheme", scheme,
                         "--seed", str(seed), "--scale", str(scale)] + extra)
    t0 = time.time()
    with open(os.path.join(d, "stdout.txt"), "w") as out:
        try:
            rc = subprocess.run([binary, f"--case-path={d}", f"--rng-run={seed}",
                                 f"--stop-ms={stop_ms}"], stdout=out,
                                stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            rc = -999
    wall = time.time() - t0
    try:
        res = flexspray_analyze.analyze(d)
    except Exception as e:  # keep failed cases visible
        res = dict(case=cid, scenario=sc, fault=fault, scheme=scheme, seed=seed, error=str(e))
    res.update(rc=rc, wall_s=wall)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--scenarios", required=True)
    ap.add_argument("--faults", default="F0")
    ap.add_argument("--schemes", required=True)
    ap.add_argument("--seeds", default="1")
    ap.add_argument("--jobs", type=int, default=48)
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("--root", default="/home/cassius/ub/work/exp")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--timeout", type=int, default=3600)
    ap.add_argument("--stop-ms", type=int, default=3, help="simulated-time cap (stalled runs)")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--extra", default="", help="extra flexspray_case.py args, space separated")
    a = ap.parse_args()
    root = os.path.join(a.root, a.name)
    os.makedirs(root, exist_ok=True)
    resf = os.path.join(root, "results.jsonl")
    done = set()
    if os.path.exists(resf) and not a.force:
        for line in open(resf):
            r = json.loads(line)
            if r.get("rc") == 0 and "error" not in r:
                done.add(r["case"])
    jobs = []
    for sc in a.scenarios.split(","):
        for fault in a.faults.split(","):
            for scheme in a.schemes.split(","):
                for seed in map(int, a.seeds.split(",")):
                    if case_id(sc, fault, scheme, seed) not in done:
                        jobs.append((sc, fault, scheme, seed))
    print(f"{len(jobs)} cases to run ({len(done)} cached) with {a.jobs} jobs", flush=True)
    extra = a.extra.split() if a.extra else []
    with cf.ProcessPoolExecutor(max_workers=a.jobs) as ex, open(resf, "a") as out:
        futs = {ex.submit(run_one, root, *j, os.path.abspath(a.bin), extra, a.scale, a.timeout,
                          a.stop_ms): j
                for j in jobs}
        for f in cf.as_completed(futs):
            r = f.result()
            out.write(json.dumps(r) + "\n")
            out.flush()
            print(f"{r['case']:60s} rc={r.get('rc')} eff={r.get('eff', 0):.4f} "
                  f"wall={r.get('wall_s', 0):.0f}s {r.get('error', '')}", flush=True)


if __name__ == "__main__":
    main()
