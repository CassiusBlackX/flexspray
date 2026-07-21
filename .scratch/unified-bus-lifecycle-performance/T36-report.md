# T36: CTP send-window and lifecycle storage fast paths

## Outcome

T36 passes its correctness, wall-time, and memory gates against `next@cf8ee683`. The final candidate
preserves exact CTP protocol artifacts while removing the payload-scaled retired-resource high-water
that previously reached about 57.6 MB at 2 GiB and about 184.5 MB at 8 GiB.

The accepted implementation consists of three narrow changes discovered through the T36 matrix:

1. Receiver-only CTP packets no longer create Transaction state or a send-side ACK bitmap.
2. Each CTP TP Packet uses the existing segment construction block and is submitted directly as one
   complete transaction operation, as required by UB Base Specification 2.0 section 6.7.2.
3. Empty CTP queues keep their pre-refactor allocator identity, and trace-OFF retired generation
   history stores adjacent equal records in compact runs.

## Behavior preservation

- The pre-refactor CTP builder, parser, header/tag construction, Jetty admission, send/ACK window,
  TAACK, response, queue priority, callback, timer, trace, and completion algorithms remain active.
- Higher-level Write/Read segmentation remains active, but every resulting transaction segment has
  its own TASSN and CTP TP Packet; the receiver keeps no cross-packet segment assembly state.
- Queue shells remain registered after draining because their allocator slot affects RR/DWRR order.
  Service disposal still unregisters all retained queues before clearing ownership.
- Compact history stores the exact generation and terminal outcome. Reopening removes only one ID,
  increments through the existing open path, and preserves unknown, stale, and retired rejection.
- SUMMARY, EVENT, and DETAIL retain per-resource summaries and emitted bytes; compact runs are used
  only when lifecycle trace is OFF.

## Correctness

| Comparison against next | Result | Exception |
|---|---|---|
| frozen `ctp-win1` | all shared artifacts and task rows byte-identical | none |
| frozen `ctp-win4` | all shared artifacts and task rows byte-identical | none |
| frozen `ctp-win128` | all shared artifacts and task rows byte-identical | none |
| 16-Entity CTP canary | all shared output/runlog artifacts and task rows byte-identical | none |
| mixed READ/WRITE bidirectional canary | all shared output/runlog artifacts and task rows byte-identical | none |

The three frozen cases also reproduce the same completion timing under Jetty inflight windows 1, 4,
and 128. Input hashes are identical for every comparison.

## Host performance

All runs are local-single release builds on the same macOS host, strictly serial, with legacy and
lifecycle output disabled. Percentages are medians of paired current-minus-next deltas.

| Total CTP payload | Measured pairs | Wall | User CPU | Peak RSS | Current median RSS | Next median RSS |
|---:|---:|---:|---:|---:|---:|---:|
| 512 MiB | 1 | -0.67% | -0.58% | -0.74% | 15,433,728 B | 15,548,416 B |
| 2 GiB | 5 | +4.20% | +4.05% | -0.74% | 15,335,424 B | 15,499,264 B |
| 8 GiB | 5 | -0.01% | +0.26% | -0.42% | 15,384,576 B | 15,417,344 B |

The 2 GiB wall gap falls from T35's `+15.04%` to `+4.20%`, recovering 10.84 percentage points and
passing the required eight-point recovery and five-percent final-gap gates. RSS is 0.74% below next,
well inside the ten-percent gate.

An earlier three-pair 8 GiB block was host-noisy at `+8.08%` wall. The independent five-pair replay
used the same source, binary, library, and input hashes and measured `-0.01%`; the stable RSS result is
consistent across both blocks. No large-scale performance exception is needed.

## Validation and review

- Targeted `unified-bus`, `unified-bus-test`, and `test-runner` build passed with `-j2`.
- `unified-bus-lifecycle-storage`, `unified-bus-lifecycle`,
  `unified-bus-lifecycle-trace`, `unified-bus-ctp-transaction`, and
  `unified-bus-ctp-lifecycle` passed serially.
- A post-review UB Base Specification 2.0 audit removed the unreachable CTP partial-assembly state.
  The targeted build and affected focused suites passed again. Fresh `ctp-win1`, `ctp-win4`, and
  `ctp-win128` runs against `next@cf8ee683` kept every shared artifact and task row byte-identical.
  The portable aggregate then resolved an explicit next checkout, verified exact revision
  `cf8ee683`, recorded both roles' source/binary/library hashes, and exited zero.
- Final implementation revision `d8f344f` has binary SHA-256 `7756e587...78` and Unified Bus library
  SHA-256 `c975a74c...03`; the aggregate records the T36 production scope as clean. The exact next
  binary and library hashes are `51129796...87` and `5d090eef...92`.
- Review against `main@ccf41b58`, T25 storage behavior, and `next@cf8ee683` found no omitted old CTP
  branch, reordered protocol state, duplicate builder/parser/state machine, active/history overlap,
  generation classification error, or enabled-trace change.
- A separate read-only cross-module audit found one pre-existing low-risk TP TPN-reuse
  cross-generation test gap, but no current production failure. It is outside T36.

## Evidence

- `scratch/20260720-ctp-fastpath-memory-scaling/results/frozen-ctp-compact-history-v1/summary.json`
- `scratch/20260720-ctp-fastpath-memory-scaling/results/frozen-ctp-compact-history-v1/ctp-win4/summary.json`
- `scratch/20260720-ctp-fastpath-memory-scaling/results/diagnostics/compact-history-production-v1-multi-entity-2g/summary.json`
- `scratch/20260720-ctp-fastpath-memory-scaling/results/diagnostics/compact-history-production-v1-mixed-bidir-2g/summary.json`
- `scratch/20260720-ctp-fastpath-memory-scaling/results/performance-compact-history-v1/summary.json`
- `scratch/20260720-ctp-fastpath-memory-scaling/results/performance-compact-history-8g-v2/summary.json`
