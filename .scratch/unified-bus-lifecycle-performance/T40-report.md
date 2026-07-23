# T40: Promotion tree and final acceptance

## Outcome

T40 passes. It changes no Unified Bus source code. It freezes the final production and exact next
roles, reruns six complex CTP/RTP cases serially, curates only compact promotion artifacts, and
publishes the final acceptance boundary.

## Frozen differential

- Current source: `61abfafe1c69a985a14d42c5bbb616227f5bbf52`.
- Next source: `cf8ee683e00e5b6210661013e93cec7217d662e5`.
- All three CTP Jetty-window cases are byte-identical, including task rows.
- CTP completion vectors match next and remain distinct for windows 1, 4, and 128.
- RTP shared-window, GBN double-loss, and TPG loss/pinning each reproduce both archived role trees
  exactly and retain identical task ID sequences.
- RTP differences remain limited to T05 same-timestamp trace ordering, T06 cumulative-ACK descriptor
  retirement, and T14 generation attribution/pinning. No exception was widened.

The first aggregate incorrectly required full RTP task-row equality, which conflicts with those
already approved timing/ordering boundaries. The runner was split so analysis can consume saved
results, and the RTP gate was restored to the T28 contract: exact archived replay for both roles plus
matching task IDs. No simulation was rerun after that analysis-only correction.

After the promotion commit advanced repository HEAD, the verifier was narrowed to distinguish a
documentation-only descendant from a production-source change. A descendant is accepted only when
its complete `src/unified-bus` tree matches the frozen source revision; executable, Unified Bus
library, input, and comparison hashes remain exact gates.

## Promotion inventory

The repository contained 17,880 untracked files: 8,685 raw/run artifacts and 9,195 other local
evidence or case files. None was deleted or staged wholesale. The promotion commit contains only the
T40 brief, inventory, reusable runner, frozen manifest, and compact summary. The user-modified
`run_p01_detailed_diagnosis.py` remains unstaged.

## Final review

The final source review used both `main@ccf41b58` and `next@cf8ee683` as fixed points and the lifecycle
architecture plus ticket tracker as the specification. No new hard standards or specification defect
was found. T38 and T39 leave existing algorithms and statement ordering in place while contracting
ownership seams. T40 contains no source change.

The following residuals are explicit and are not reported as PASS:

- the macOS umbrella test runner exits 139 after focused suites have passed;
- automatic TP cleanup is excluded and not implemented;
- the LDST/p01 performance residual remains, and the measured typed-tracked prototype is not in the
  production branch;
- the complete MTP/MPI/hybrid matrix was frozen at T20 and was not rerun after T38/T39.

## Evidence

- `T40-task-brief.md`
- `T40-promotion-inventory.md`
- `run_t40_acceptance.py`
- `evidence/t40-final-v1/manifest.json`
- `evidence/t40-final-v1/summary.json`
- `docs/superpowers/specs/2026-07-16-unified-bus-lifecycle-acceptance.md`
