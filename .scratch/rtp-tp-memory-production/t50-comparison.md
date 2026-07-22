# T50 Production Candidate Comparison

Candidate `36334c0` was built once in a detached clean worktree and executed serially with the frozen
T43 inputs, a 10 GiB process-tree RSS limit, and a 900-second wall limit. The curated evidence under
`t50-evidence/` excludes raw task traces but includes their complete sorted per-file checksum lists.

## Correctness

- `p001` local and MTP16 checksum-list SHA256 values equal the frozen T43 task-trace tree hash.
- `p004` completes 4,096 tasks and 114,688 segments with the same checksum-list SHA256 as the prior
  owner-attribution run.
- The legacy `p001` storage projection changes only at the intended task-scoped Jetty boundary:
  1,024 Jetties move from active/leased state to compact history.

## Memory

- `p004` `time -l` peak RSS decreases from 498,827,264 B to 283,820,032 B, or 43.10 percent.
- The `p001` to `p004` process-peak increment decreases from 93,525 B/pair to 26,069 B/pair, or
  72.12 percent.
- Peak live TP backing stays at 2,048 while logical endpoint records grow from 2,048 to 8,192.

## Runtime And Limits

- MTP16 wall time regresses by 42.30 percent at `p001` and 31.13 percent at `p004` versus the prior
  attribution build.
- `p016` remains event-progressing through 700 us but reaches the unchanged 900-second wall guard.
  It has no completion trace or storage snapshot and is excluded from completed-run slope claims.
- `p064` was not run.

## Classification

- Canonical outcome: `matched` through completed checkpoints.
- Backing working-set prediction: `matched` through `p004`.
- Process-memory prediction: `matched` through `p004`.
- Runtime-improvement expectation: `mismatched`.
- `p016` completed-run scaling and `p064`: `inconclusive`.
