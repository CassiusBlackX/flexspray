# CTP Fast-Path Three-Role Scaling Experiment

## Planning Mode

- `experiment-group`

## Initial Claim

The complete-inbound-unit fast path reduces CTP receiver CPU cost relative to the lazy-context
implementation at `a95f8bc` without changing protocol-observable output relative to
`next@cf8ee683`, while the remaining lifecycle RSS cost is attributable to payload-scaled retained
generation history rather than CTP receive-unit storage.

The first performance pass supported the storage attribution but falsified the assumption that the
remaining RSS cost was acceptable: current reached about 57.6 MB at 2 GiB and about 184.5 MB at
8 GiB. The user therefore approved a lifecycle-storage treatment within T36 before final acceptance.

## Simulator Boundary

- This experiment measures deterministic ns-3 model output plus host wall time, CPU time, and peak
  RSS on one macOS host. It does not make hardware-performance claims.
- Protocol correctness is checked with trace-on canaries and production output artifacts.
- Performance runs disable legacy and lifecycle output and do not use missing traces as a
  correctness oracle.

## Control And Treatments

- Control: GitCode `next@cf8ee683e00e5b6210661013e93cec7217d662e5`.
- Treatment 1: lazy CTP context at `a95f8bcdbf2eb32e171edde7aae970cfedf85c76`.
- Treatment 2: the current complete-inbound-unit fast-path candidate based on `a95f8bc`.
- Main changed variable: CTP lifecycle implementation revision. The fast-path versus lazy
  comparison isolates the uncommitted receive-unit storage change.

## Approved Scope Extension

- Preserve the pre-refactor CTP queue object and allocator slot after the queue drains. This is a
  correctness repair discovered by the multi-Entity canary, not a performance treatment.
- With lifecycle trace OFF, replace one retired-resource map shell per local ID with ordered runs of
  adjacent IDs that have the same exact generation and terminal-outcome history.
- Reopening one ID splits it from the run and enters the existing generation-increment path.
  SUMMARY, EVENT, and DETAIL retain the per-resource storage and emission path.
- The final treatment is compared directly with `next@cf8ee683`; the lazy role remains diagnostic
  history and is not required for the final 2 GiB or 8 GiB gate.

## Fixed Controls

- Same compiler family, release build, host, local-single runtime, `rng-run=1`, source case,
  topology, routing, flow control, Jetty window, traffic bytes, and trace settings within a block.
- CTP uses the existing three-path two-device fixture and Entity/VL transport semantics.
- Every simulation is serial. No build, test, or second simulation overlaps a measured run.
- The same materialized input bytes are copied for all three roles.

## Blocks

### Four-flow scale curve

- `scale-512m`: four same-Entity CTP WRITEs, 128 MiB each, 512 MiB total.
- `scale-2g`: four same-Entity CTP WRITEs, 512 MiB each, 2 GiB total.
- `scale-8g`: sixteen same-Entity CTP WRITEs, 512 MiB each, 8 GiB total. This keeps each
  `dataSize(Byte)` below the parser's signed intermediate boundary while preserving total segment
  count and memory pressure.
- Prediction: fast path uses less CPU than lazy at the same scale; lazy and fast path retain a
  similar RSS excess over next, and that excess grows with the number of retired segment IDs.
- Falsification: fast path has no repeatable CPU reduction, or RSS is flat with payload despite the
  retained-generation attribution.

### Multi-Entity CTP

- `multi-entity-2g`: sixteen simultaneous 128 MiB WRITEs, each using a distinct source and
  destination Entity identity, 2 GiB total.
- Prediction: all roles complete the same tasks and the fast path remains effective when CTP state
  is distributed across Entity/Jetty owners.
- Falsification: output differs, a role stalls, or the gain exists only for one shared Entity.

### Mixed Bidirectional CTP

- `mixed-bidir-2g`: sixteen simultaneous 128 MiB operations: eight WRITEs and eight READs split
  evenly across both directions and distinct Entity identities, 2 GiB total.
- Prediction: request and response paths remain byte-equivalent, and RSS is driven primarily by
  total retired segment identity count rather than one opcode or direction.
- Falsification: READ/WRITE completion or artifact bytes differ, or memory behavior changes in a
  way inconsistent with segment count.

### Diagnostic: stable CTP queue registration

- `queue-retirement-disabled`: reuse the trace-on `multi-entity-2g` canary and current fast-path
  candidate, but temporarily omit only the empty-queue callback binding.
- Changed variable: whether an empty CTP transmit queue is unregistered/erased or retains its
  original allocator slot, matching the pre-refactor and next behavior.
- Prediction: if empty-queue retirement is the cause of the complex CTP divergence, every shared
  output/runlog artifact and parsed task row returns to exact equality with `next@cf8ee683`.
- Falsification: any shared artifact or parsed task row remains different with identical input
  hashes. No performance conclusion is drawn from this diagnostic.
- Rollback: restore the callback binding and rebuild the original fast-path binary immediately
  after the diagnostic artifact is captured.

## Evidence Plan

- Correctness canaries: 1 MiB per operation for the two complex blocks, trace-on, one run per role;
  compare all shared output and runlog files by SHA-256 and compare parsed task rows.
- Existing correctness anchor: frozen `ctp-win1`, `ctp-win4`, and `ctp-win128` comparisons already
  show exact shared bytes and identical Jetty-window timing against next.
- `scale-2g`: two warm-ups per role and nine position-balanced three-role blocks.
- Other performance blocks: one warm-up per role; three blocks for 512 MiB and 8 GiB, six blocks
  for each complex 2 GiB workload.
- Primary CPU metric: paired user CPU time. Wall time is secondary because the host showed large
  scheduling noise. Memory metric: peak RSS from `/usr/bin/time -l`.
- Report role medians, within-block deltas, all raw blocks, absolute RSS excess, RSS ratio, and RSS
  slope versus total payload.

## Checkpoint Policy

- `checkpoint_policy: continue_full_matrix`
- Stop on an unexplained correctness difference, repeated execution failure, output collision,
  timeout, or unsafe host resource pressure.
- A noisy wall-time block does not stop execution when user CPU and correctness remain available.

## Artifact Contract

- Package records: this plan, `matrix.yaml`, `command-manifest.yaml`, `run-ledger.md`, per-case
  specs, runner source, and analysis output.
- Materialized inputs: one correctness and one performance template per block with SHA-256
  manifests.
- Per-run evidence: exact command, stdout, stderr, exit code, `/usr/bin/time -l`, result JSON, and
  copied case input including generated simulator output.
- Analysis: exact role revisions and binary/library hashes, correctness comparisons, every timing
  block, role medians, paired deltas, and RSS scale fit.

## Analysis Notes

- `queue-retirement-disabled`: `matched`. The treatment used identical materialized inputs and
  matched `next@cf8ee683` exactly across all 16 shared output/runlog artifacts and parsed task rows.
- Supported conclusion: empty CTP queue retirement, specifically unregister/erase followed by
  later re-registration, causes the observed multi-Entity ordering divergence by changing stable
  allocator slots. The complete-inbound-unit fast path is not the cause.
- Unsupported conclusion: this diagnostic does not select the production repair. Permanently
  retaining every empty queue would recover next behavior but would weaken finite lifecycle cleanup;
  a production design must preserve scheduler identity/order without retaining unnecessary state.
- The diagnostic source change was restored after evidence capture, and the original source,
  executable, and Unified Bus library hashes were recovered before further work.
- Production resolution: an empty CTP queue shell is scheduler state, so the approved repair retains
  its service-owned identity until service disposal while finite Transaction, packet, and event work
  still retires. Final `multi-entity-2g` and `mixed-bidir-2g` results are byte-identical to next.
- Frozen `ctp-win1`, `ctp-win4`, and `ctp-win128` results are byte-identical to next and reproduce
  the same Jetty-window completion effects. No correctness exception is present.
- Final 2 GiB five-pair result: current versus next is `+4.20%` wall, `+4.05%` user CPU, and
  `-0.74%` peak RSS. The wall gap recovers 10.84 percentage points from T35 and passes both gates.
- Independent final 8 GiB five-pair result: `-0.01%` wall, `+0.26%` user CPU, and `-0.42%` peak RSS.
  Current median RSS is about 15.4 MB, so payload-scaled retired-map growth is no longer present.

## Approval

- Approved by the user on 2026-07-20, including the three-role, larger-scale, and complex-scenario
  extension and the T36 RSS-gate split when the memory source is explicitly attributed.
- The single-variable stable-queue diagnostic was approved by the user on 2026-07-20 after the
  unexplained complex correctness difference paused the performance matrix.
