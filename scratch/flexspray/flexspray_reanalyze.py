#!/usr/bin/env python3
"""reanalyze.py MATRIX_DIR [JOBS]: re-run analysis (single-threaded workers) for every case dir;
writes MATRIX_DIR/results2.jsonl, keeping rc/wall from results.jsonl when present."""
import os, sys, json, glob
os.environ["OMP_NUM_THREADS"] = "1"; os.environ["OPENBLAS_NUM_THREADS"] = "1"
import concurrent.futures as cf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flexspray_analyze as A

def one(d):
    old = OLD.get(os.path.basename(d), {})
    try:
        r = A.analyze(d)
    except Exception as e:
        r = dict(case=os.path.basename(d), error=str(e))
    out = open(os.path.join(d, "stdout.txt"), errors="replace").read() if os.path.exists(os.path.join(d, "stdout.txt")) else ""
    r["rc"] = old.get("rc", 0 if "[summary] Program finished." in out else -1)
    r["wall_s"] = old.get("wall_s")
    r["aborted"] = "NS_FATAL" in out or "aborted." in out
    return r

root = sys.argv[1]; jobs = int(sys.argv[2]) if len(sys.argv) > 2 else 48
OLD = {}
if os.path.exists(os.path.join(root, "results.jsonl")):
    for l in open(os.path.join(root, "results.jsonl")):
        x = json.loads(l); OLD[x["case"]] = x
dirs = sorted(d for d in glob.glob(os.path.join(root, "*__*")) if os.path.isdir(d))
with cf.ProcessPoolExecutor(jobs) as ex, open(os.path.join(root, "results2.jsonl"), "w") as f:
    for r in ex.map(one, dirs):
        f.write(json.dumps(r) + "\n")
print("done", len(dirs))
