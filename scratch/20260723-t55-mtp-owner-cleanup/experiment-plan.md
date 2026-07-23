# T55 MTP16 Owner Cleanup Acceptance

Status: approved

## Claim

Under a real 16-partition MTP run, every phase-0 task completion becomes globally visible before any
dependent phase-1 task starts. Phase 1 reuses the same RTP endpoint identity keys, and the treatment
reports aggregate compact backing restorations without changing canonical workload results.

## Simulator Boundary

This experiment observes the public `ub-quick-example` case path. It does not directly call manager
internals. Owner-gated resolution and fine-grained `COMPLETE_VISIBLE -> cleanup` ordering are
therefore focused-test-derived, not run-derived.

## Control And Treatment

- Control: revision `e3682e593c144ab441dd7045aff5f6312efab86d`, before TP backing compaction.
- Treatment: HEAD `5965931f378430f3d4195ec7fab5487a4e01ce1d` plus the recorded working-tree patch that implements
  T51-T54.
- Changed variable: TP backing compaction and its owner-local completion/restore implementation.
- Interpretation: this is an implementation A/B, not a performance comparison.

## Fixed Controls

- 32 hosts in a two-layer Clos with 4 leaves and 8 spines, 400 Gbps, 20 ns links.
- 16 MTP partitions, MPI disabled, Release build, Ninja generator, RNG run 1.
- RTP, task-scoped Jetty, URMA_WRITE, priority 7, on-demand transport channels.
- Phase 0 contains one directed ring edge per host. Phase 1 repeats the exact same source, destination,
  priority, routing, and reserved connection identity after phase 0 is visible and a 1 us task delay.
- `--dependency-visibility-delay=10ns`, `--initial-task-start-offset-window=32ps`, seed 1.
- Same byte-identical case inputs and canonical-output definition on both sides.
- Runs are serial, one formal repetition per row.

## Prediction And Falsification

- Prediction: both rows exit zero and their normalized canonical output is byte-identical.
- Prediction: treatment snapshot has exactly 64 traffic tasks completed, zero dependency references,
  64 compact endpoint records, 64 aggregate TP backing restorations across 64 resident endpoints,
  zero final live backing, and peak live backing 64. Per-identity restoration count is unavailable.
- Reason: phase 1 reuses phase 0 connection reservations only after visible dependency release and
  after owner-delivered task cleanup has had 1 us of simulated delay to compact idle backing.
- Falsification: nonzero exit, guard termination, canonical mismatch, incomplete traffic state,
  nonzero dependency references, zero compact records, zero restoration count, or invalid storage
  accounting.

## Evidence Plan

- Measured: process-group RSS samples, `time -l`, exit status, wall time, stdout, and stderr.
- Simulator-emitted: canonical events and treatment runtime-storage snapshot.
- Case-derived: case hashes and the repeated `(source,destination,priority)` set across phases.
- Build-derived: source revision/status/diff hash, source-tree hash, cache hash, binary/library hashes,
  build shape, and artifact mtimes.
- Canonical-derived: every phase-1 `START` is strictly later than all phase-0 `COMPLETE_VISIBLE`
  events.
- Test-derived: owner/public/missing-owner resolution gating and cleanup-after-visible ordering.

## Artifact Contract

Each row must contain `command.json`, `manifest.json`, `exit-code.txt`, `time-l.txt`,
`rss-samples.csv`, `stdout.txt`, `stderr.txt`, raw canonical rank files, and
`canonical-output.csv`. The treatment additionally contains `runtime-storage-snapshot.csv`.
The package contains `build-provenance.json`, `input-sha256.json`, `comparison-summary.json`, and
`analysis.md`.

## Checkpoint Policy

`checkpoint_policy=continue_full_matrix`; `manual_approval_gate=false`. Safety guards always stop a
run. A failed control prevents treatment execution because canonical comparison would be undefined.
Formal evidence directories are write-once.
