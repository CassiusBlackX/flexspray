#!/bin/bash
# FlexSpray - the one reproduction entry for the current version (v5.3, round 5).
#
#   scratch/flexspray/reproduce.sh OUT_DIR [JOBS]
#
# All outputs (matrices, logs, analyzer results) go below OUT_DIR. The build is this checkout's
# build/ directory. For v5.2 (tag flexspray-v5.2) check out that tag and use its
# reproduce_v5.2.sh; the v5.2 behaviour is also available here as scheme option `rim0`.
#
# Steps (each can be skipped with SKIP_<NAME>=1):
#   BUILD   ub-quick-example + test-runner (release build of this checkout)
#   UNIT    all unified-bus test suites (incl. unified-bus-flexspray: hard link-down re-send,
#           SprayCC/CAQM rollback, tail-segmentation reservation)
#   REGRESS flexspray_regress.py: replay fence on (22 recovery cases pass P1-P6, incl. the fence
#           liveness and 1/64 false-death cases) / off (negative control must fail P5) /
#           4 KB tail segmentation completes
#   GATES   gate set on tuning seeds 1,2 (v5.3 vs ablation rim0 = v5.2 behaviour) + sensitivity
#   FINAL   validation on seeds 7,8 and traffic map 3 (unseen when v5.3 was frozen), hard
#           link-down, comparators on hard link-down
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
V=flex+spraycc; A5=flex+spraycc+rim0; W=ns3::UbTransaction::ReadIngressMarkWeight

[ -n "$SKIP_BUILD" ] || ninja -C $ROOT/cmake-cache -j $J scratch_ub-quick-example ns3.44-test-runner
if [ -z "$SKIP_UNIT" ]; then
  for s in $($ROOT/build/utils/ns3.44-test-runner --list | tr -s ' ' '\n' | grep '^unified-bus' | sort); do
    (cd $ROOT && ./build/utils/ns3.44-test-runner --test-name=$s > /dev/null 2>&1) && echo "PASS $s" || echo "FAIL $s"
  done | tee unit.txt
fi
[ -n "$SKIP_REGRESS" ] || $PY $FS/flexspray_regress.py --out regress --bin $BIN | tee regress.txt
if [ -z "$SKIP_GATES" ]; then
  run gates_sens --scenarios mixedread --faults F0 --schemes flex+spraycc+$W=0.15,$V,flex+spraycc+$W=0.5 --seeds 1,2
  run gates_core --scenarios $SC --faults $F7 --schemes $V,$A5 --seeds 1,2
  run gates_cov --scenarios mixed,mixedread --faults F1ssd,F3ssd,F1nic,F3nic,F4p4 --schemes $V,$A5 --seeds 1,2
  run gates_cov --scenarios stagger,staggermixed,incast --faults F0 --schemes $V,$A5 --seeds 1,2
  run gates_osub --scenarios a2a,mixed,mixedread --faults F0,F2up,F4 --schemes $V,$A5 --seeds 1,2 --extra "--links-per-pair 2"
  run gates_buf --scenarios incast,mixed,a2a,mixedread,d2dread --faults F0 --schemes $V+buf128,$A5+buf128 --seeds 1,2
  run gates_hard --scenarios $SC --faults H1near,H1far,H2up,H4 --schemes $V,$A5 --seeds 1,2
  run gates_hard --scenarios mixed,mixedread --faults H1ssd,H4p4 --schemes $V,$A5 --seeds 1,2
fi
if [ -z "$SKIP_FINAL" ]; then
  run final_core --scenarios $SC --faults $F7 --schemes $V --seeds 7,8
  run final_core --scenarios mixed,mixedread,a2aread --faults $F7 --schemes $A5 --seeds 7,8
  run final_map --scenarios $SC --faults $F7 --schemes $V --seeds 7 --extra "--map 3"
  run final_cov --scenarios mixed,mixedread --faults F1ssd,F3ssd,F1nic,F3nic,F4p4 --schemes $V,$A5 --seeds 7,8
  run final_cov --scenarios stagger,staggermixed,incast --faults F0 --schemes $V,$A5 --seeds 7,8
  run final_osub --scenarios a2a,mixed,mixedread --faults F0,F2up,F4 --schemes $V,$A5 --seeds 7,8 --extra "--links-per-pair 2"
  run final_buf --scenarios incast,mixed,a2a,mixedread,d2dread --faults F0 --schemes $V+buf128,$A5+buf128 --seeds 7,8
  run final_hard --scenarios $SC --faults H1near,H1far,H2up,H4 --schemes $V --seeds 7,8
  run final_hard --scenarios mixed,mixedread --faults H1ssd,H4p4 --schemes $V,$A5 --seeds 7,8
  run final_hardabl --scenarios $SC --faults H1far,H2up,H4 --schemes $V+nofd,$V+nortx --seeds 7
  run final_cmp --scenarios $SC --faults $F7 --schemes spray,adaptive+caqm,ecmp --seeds 7
  S="adaptive+caqm+rtx,adaptive+caqm+rtx+fd,spray+rtx+fd"
  run final_cmphard --scenarios $SC --faults H1near,H1far,H2up,H4 --schemes $S --seeds 1,5
  run final_cmphard --scenarios mixed,mixedread --faults H1ssd,H4p4 --schemes $S --seeds 1,5
fi
if [ -z "$SKIP_REPORT" ]; then
  for d in gates_* final_*; do
    [ -f $d/results.jsonl ] || continue
    $PY $FS/flexspray_portutil.py $(ls -d $d/mixed*/ $d/staggermixed*/ 2>/dev/null) --jobs $J --out $d/pu.jsonl 2>/dev/null || true
  done
  dirs() { for d in "$@"; do if [ -f "$d/results.jsonl" ]; then echo "$d"; fi; done; return 0; }
  H=$(dirs gates_hard final_hard final_hardabl final_cmphard)
  [ -n "$H" ] && $PY $FS/flexspray_hardreport.py $H > hard.json
  [ -n "$H" ] && $PY $FS/flexspray_fdreport.py $H --md > fdreport.md
  I=$(dirs gates_cov gates_buf final_cov final_buf)
  [ -n "$I" ] && $PY $FS/flexspray_incastreport.py $I > incast.jsonl
  L=$(dirs final_core final_cov final_hard)
  [ -n "$L" ] && $PY $FS/flexspray_lossreport.py $L > loss.json
  true
fi
echo "done: $OUT"
