#!/bin/bash
# FlexSpray v5.2 (tag flexspray-v5.2) - one reproduction entry for round 4.
#
#   scratch/flexspray/reproduce_v5.2.sh OUT_DIR [JOBS]
#
# Steps (each can be skipped with SKIP_<NAME>=1):
#   BUILD   ub-quick-example + test-runner (release build of this checkout)
#   UNIT    all unified-bus test suites (incl. unified-bus-flexspray: hard link-down re-send,
#           SprayCC/CAQM rollback, tail-segmentation reservation)
#   REGRESS flexspray_regress.py: replay fence on (14 cases pass P1-P6) / off (negative control
#           must fail P5) / 4 KB tail segmentation completes
#   GATES   round-11 gate set on tuning seeds 1,2 (v5.1 = +ai5 vs v5.2)
#   FINAL   round-13 validation on unseen seeds 5,6 and traffic map 2, ablations, comparators
#   REPORT  analyzers: summarize, portutil (+util_biz), hardreport, fdreport, incastreport
# Python: a venv with numpy/scipy/highspy (OMP_NUM_THREADS=1 is set below).
set -e
OUT=${1:?usage: reproduce_v5.2.sh OUT_DIR [JOBS]}; J=${2:-$(nproc)}
ROOT=$(cd "$(dirname "$0")/../.." && pwd); FS=$ROOT/scratch/flexspray
PY=${PY:-python3}; BIN=$ROOT/build/scratch/ns3.44-ub-quick-example
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
mkdir -p "$OUT"; cd "$OUT"
run() { name=$1; shift; echo "=== $name $(date +%T)"; $PY $FS/flexspray_matrix.py --name $name --root "$OUT" --jobs $J --stop-ms 3 --bin $BIN "$@" | grep -E "rc=|Traceback|Error" || true; }
F7=F0,F1near,F1far,F2up,F3near,F3far,F4; SC=d2d,d2dread,h2d,a2a,mixed,a2aread,mixedread
V=flex+spraycc; A5=flex+spraycc+ai5

[ -n "$SKIP_BUILD" ] || ninja -C $ROOT/cmake-cache -j $J scratch_ub-quick-example ns3.44-test-runner
if [ -z "$SKIP_UNIT" ]; then
  for s in $($ROOT/build/utils/ns3.44-test-runner --list | tr -s ' ' '\n' | grep '^unified-bus' | sort); do
    (cd $ROOT && ./build/utils/ns3.44-test-runner --test-name=$s > /dev/null 2>&1) && echo "PASS $s" || echo "FAIL $s"
  done | tee unit.txt
fi
[ -n "$SKIP_REGRESS" ] || $PY $FS/flexspray_regress.py --out regress --bin $BIN | tee regress.txt
if [ -z "$SKIP_GATES" ]; then
  run gates_core --scenarios $SC --faults $F7 --schemes $V,$A5 --seeds 1,2
  run gates_cov --scenarios mixed,mixedread --faults F1ssd,F3ssd,F1nic,F3nic,F4p4 --schemes $V,$A5 --seeds 1,2
  run gates_cov --scenarios stagger,staggermixed,incast --faults F0 --schemes $V,$A5 --seeds 1,2
  run gates_osub --scenarios a2a,mixed --faults F0,F2up,F4 --schemes $V,$A5 --seeds 1,2 --extra "--links-per-pair 2"
  run gates_buf --scenarios incast,mixed,a2a,mixedread,d2dread --faults F0 --schemes $V+buf128,$A5+buf128 --seeds 1,2
  run gates_hard --scenarios $SC --faults H1near,H1far,H2up,H4 --schemes $V,$A5 --seeds 1,2
  run gates_hard --scenarios mixed,mixedread --faults H1ssd,H4p4 --schemes $V,$A5 --seeds 1,2
fi
if [ -z "$SKIP_FINAL" ]; then
  run final_core --scenarios $SC --faults $F7 --schemes $V,$A5 --seeds 5,6
  run final_map --scenarios $SC --faults $F7 --schemes $V --seeds 5 --extra "--map 2"
  run final_cov --scenarios mixed,mixedread --faults F1ssd,F3ssd,F1nic,F3nic,F4p4 --schemes $V,$A5 --seeds 5,6
  run final_cov --scenarios stagger,staggermixed,incast --faults F0 --schemes $V,$A5 --seeds 5,6
  run final_osub --scenarios a2a,mixed --faults F0,F2up,F4 --schemes $V,$A5 --seeds 5,6 --extra "--links-per-pair 2"
  run final_buf --scenarios incast,mixed,a2a,mixedread,d2dread --faults F0 --schemes $V+buf128,$A5+buf128 --seeds 5,6
  run final_hard --scenarios $SC --faults H1near,H1far,H2up,H4 --schemes $V --seeds 5,6
  run final_hard --scenarios mixed,mixedread --faults H1ssd,H4p4 --schemes $V,$A5 --seeds 5,6
  run final_hardabl --scenarios $SC --faults H1far,H2up,H4 --schemes $V+nofd,$V+nortx --seeds 5
  run final_cmp --scenarios $SC --faults $F7 --schemes spray,adaptive+caqm,ecmp --seeds 5
  S="adaptive+caqm+rtx,adaptive+caqm+rtx+fd,spray+rtx+fd"
  run final_cmphard --scenarios $SC --faults H1near,H1far,H2up,H4 --schemes $S --seeds 1,5
  run final_cmphard --scenarios mixed,mixedread --faults H1ssd,H4p4 --schemes $S --seeds 1,5
fi
if [ -z "$SKIP_REPORT" ]; then
  for d in gates_* final_*; do
    [ -f $d/results.jsonl ] || continue
    $PY $FS/flexspray_portutil.py $(ls -d $d/mixed*/ $d/staggermixed*/ 2>/dev/null) --jobs $J --out $d/pu.jsonl 2>/dev/null || true
  done
  $PY $FS/flexspray_hardreport.py $(ls -d *hard* 2>/dev/null) > hard.json || true
  $PY $FS/flexspray_fdreport.py $(ls -d *hard* 2>/dev/null) --md > fdreport.md || true
  $PY $FS/flexspray_incastreport.py $(ls -d *cov* *buf* 2>/dev/null) > incast.jsonl || true
fi
echo "done: $OUT"
