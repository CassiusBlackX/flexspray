# T55 Run Ledger

Status: complete

- Package: `scratch/20260723-t55-mtp-owner-cleanup`
- Repository root: `/Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode`
- Branch / HEAD: `codex/rtp-tp-memory-compaction` /
  `5965931f378430f3d4195ec7fab5487a4e01ce1d`
- Working tree: dirty treatment patch; exact formal patch hash is frozen per run manifest
- Runner entrypoint: `scratch/20260723-t55-mtp-owner-cleanup/scripts/run_t55.py`
- Runtime: MTP 16, RNG run 1, dependency visibility 10 ns, initial offset window 32 ps, seed 1
- Build shape: Release, Ninja, MTP enabled, MPI disabled
- Checkpoint policy: `continue_full_matrix`
- Manual approval gate: false
- Formal repetition count: one per row, serial
- Guards: 2 GiB process-group RSS, 120 s wall, 0.25 s sampling

| case_id | role | revision | status | return code | wall | peak RSS | canonical | snapshot |
| --- | --- | --- | --- | ---: | ---: | ---: | --- | --- |
| t55-before | control | e3682e5 | success | 0 | 0.740s | 16924672 | 480a58051753 | not supported |
| t55-after | treatment | 5965931+patch | success | 0 | 1.200s | 16515072 | 480a58051753 | recorded |

## Execution Records

### t55-before

- Start: `2026-07-23T09:15:10.626711+00:00`
- End: `2026-07-23T09:15:11.756381+00:00` (derived from recorded start plus elapsed)
- Runner elapsed: `1.1296702909749001 s`; `time -l` wall: `0.74 s`
- Return code: `0`; failure category: `none`; retryable: `false`
- Artifact inventory: `canonical-output.csv`, `canonical.rank0.txt`, `command.json`,
  `exit-code.txt`, `rss-samples.csv`, `stderr.txt`, `stdout.txt`, `time-l.txt`
- Exact command:

```text
/usr/bin/time -l -o /Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode/scratch/20260723-t55-mtp-owner-cleanup/results/t55-before/time-l.txt /private/tmp/ns3ub-tp-ab-before-e3682e5/build/scratch/ns3.44-ub-quick-example --case-path=/Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode/scratch/20260723-t55-mtp-owner-cleanup/results/t55-before/case --rng-run=1 --dependency-visibility-delay=10ns --initial-task-start-offset-window=32ps --timing-offset-seed=1 --canonical-output=/Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode/scratch/20260723-t55-mtp-owner-cleanup/results/t55-before/canonical --mtp-threads=16
```

### t55-after

- Start: `2026-07-23T09:15:11.850656+00:00`
- End: `2026-07-23T09:15:13.533676+00:00` (derived from recorded start plus elapsed)
- Runner elapsed: `1.6830196250230074 s`; `time -l` wall: `1.20 s`
- Return code: `0`; failure category: `none`; retryable: `false`
- Artifact inventory: `canonical-output.csv`, `canonical.rank0.txt`, `command.json`,
  `exit-code.txt`, `rss-samples.csv`, `runtime-storage-snapshot.csv`, `stderr.txt`,
  `stdout.txt`, `time-l.txt`
- Exact command:

```text
/usr/bin/time -l -o /Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode/scratch/20260723-t55-mtp-owner-cleanup/results/t55-after/time-l.txt /Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode/build/scratch/ns3.44-ub-quick-example --case-path=/Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode/scratch/20260723-t55-mtp-owner-cleanup/results/t55-after/case --rng-run=1 --dependency-visibility-delay=10ns --initial-task-start-offset-window=32ps --timing-offset-seed=1 --canonical-output=/Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode/scratch/20260723-t55-mtp-owner-cleanup/results/t55-after/canonical --runtime-storage-snapshot=/Users/ytxing/.codex/worktrees/73a0/ns-3-ub-gitcode/scratch/20260723-t55-mtp-owner-cleanup/results/t55-after/runtime-storage-snapshot.csv --mtp-threads=16
```

Both rows prove all phase-1 starts at 2799 occur after the latest phase-0 visible completion at 1799.
The treatment reports 64 aggregate restorations across 64 resident endpoints; per-identity counts
are unavailable. Fine-grained `COMPLETE_VISIBLE -> cleanup` ordering and owner gating remain
focused-test evidence.
