# T40 Promotion Inventory

Inventory date: 2026-07-21

## Keep In Promotion Tree

- 103 tracked files under `.scratch/unified-bus-lifecycle-performance`: PRD, task briefs, reports,
  compact summaries/manifests, and reproducible runners.
- 171 tracked files under `scratch/20260717-next-lifecycle-ctp-rtp-complex-differential`: six frozen
  case inputs, experiment contract, accepted comparisons, analysis, and runner.
- 16 tracked files under `scratch/20260720-ctp-fastpath-memory-scaling`: T36 plan/inputs/scripts and
  compact frozen aggregate summary.
- 3 tracked lifecycle architecture/acceptance documents under `docs/superpowers/specs`.
- T40 task brief, this inventory, reusable final runner, frozen manifest, aggregate summary, final
  acceptance report, and tracker closeout.

## Summarize Only

- T34 Instruments/profile trees: retain committed profile/timing summaries and runners, not raw
  `.trace`, `.xml`, `.plist`, bulkstore, or index data.
- T28/T36/T37/T40 executions: retain compact manifests, role/input hashes, comparison summaries,
  controlled-difference hashes/causes, and reports; leave repeated run trees untracked.
- Performance matrices: retain the already committed p01/p02, LDST, CTP 2 GiB/8 GiB and module
  hotspot summaries. Do not copy raw repetitions into the promotion commit.

## Discard From Promotion Staging

- 8,685 untracked raw/run artifacts matching stdout, stderr, exit code, `/usr/bin/time`, Instruments,
  bulkstore, and index forms.
- 9,195 other untracked local evidence/case files unless an individual file is explicitly named by
  the T40 manifest or summary.
- Invalid, environment-contaminated, exploratory ablation, and duplicate next/current run trees.

`Discard from promotion staging` does not authorize filesystem deletion. The 17,880 untracked files
remain in place because ownership cannot be established safely. The user-modified
`.scratch/unified-bus-lifecycle-performance/run_p01_detailed_diagnosis.py` also remains unstaged.
