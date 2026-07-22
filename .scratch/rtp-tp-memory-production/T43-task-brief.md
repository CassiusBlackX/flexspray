# T43 Task Brief: Freeze RTP/TP Compaction Acceptance Baseline

Status: complete

## Authority And Scope

- Ticket: `tickets.md`, Task 43.
- Feature contract: `.scratch/rtp-tp-memory-production/PRD.md`.
- Source baseline: `aa2b26a` on `codex/unified-bus-lifecycle-implementation`.
- This ticket freezes evidence only. It does not change Unified Bus production code, promote the
  prototype policy, or run a checkpoint larger than an approved guard frontier.
- Baseline build and runs must use an isolated clean worktree at exactly `aa2b26a`. The current
  feature worktree contains user/prototype changes and is not an eligible build source.

## Highest Test Seam

The confirmed seam is the public `ub-quick-example` executable plus its complete case outputs,
task timestamps, command manifest, process-tree RSS samples, and versioned runtime storage snapshot.
RSS is performance evidence; canonical artifacts and task timing are correctness evidence.

## Existing Evidence Inventory

### Reusable as inputs or harness prior art

- `scratch/20260721-rtp-tp-memory-probe/scripts/run_guarded.py`: process-tree RSS and wall guard,
  fresh-output enforcement, command capture, input hashes, stdout/stderr, and `/usr/bin/time -l`.
- `scratch/20260721-rtp-tp-memory-probe/command-manifest.yaml`: release MTP build recipe, MTP16
  controls, 10 GiB RSS guard, 900-second wall guard, serial checkpoint order, and manual gates.
- `scratch/20260721-rtp-tp-memory-probe/cases/p001`, `p004`, and `p016`: bounded fixed-topology
  inputs. Inputs may be admitted only after their hashes are re-recorded in the T43 baseline
  manifest; existing mutable paths are not authority by name alone.
- Existing p001/p004/p016 result directory schemas are reusable as the artifact contract:
  `manifest.json`, `command.json`, `stdout.txt`, `stderr.txt`, `time-l.txt`, `rss-samples.csv`,
  `exit-code.txt`, output/runlog inventory, and `runtime-storage-snapshot.csv` when enabled.

### Diagnostic-only evidence

- Post-`e3682e5` MTP16 p001/p004/p016 results prove the guarded runner and workload complete, but
  their source and binary do not match `aa2b26a`.
- Owner-attribution p001/p004/p016 results use binary
  `d947779b243377827fd09de8509436ff00e4c3f562488680a64596e484f0e0e5` from `e3682e5` plus
  uncommitted instrumentation. Their manifests omit `source_head`; they cannot be acceptance
  baseline artifacts.
- Prototype p001/p004/p016 results prove memory leverage only. They intentionally change eager
  reservation, connection lifetime, and completion visibility, so their canonical outputs cannot
  define production equivalence.
- The prototype p016 comparison is trace-confounded: baseline trace-on versus prototype trace-off.
  It is not eligible for a same-configuration memory delta.

## Runtime Variants To Freeze

1. `local`: the exact non-MTP execution mode supported by the `aa2b26a` public runner.
2. `mtp16`: MTP enabled at build time and invoked with 16 workers, RNG run 1,
   `dependency-visibility-delay=10ns`, `initial-task-start-offset-window=32ps`, and timing-offset
   seed 1.

Before execution, resolve whether `local` and `mtp16` require separate build caches or one
MTP-enabled executable with a runtime worker setting. Do not infer this from old binaries. Record
the resolved configure command, CMake cache hash, executable hash, Unified Bus library hash, and
dynamic library environment for each incompatible variant.

## Artifact Eligibility

- `raw-byte`: repeated baseline runs are byte-identical; final milestone requires exact bytes.
- `canonical-only`: raw order is proven nondeterministic but a predeclared canonicalizer preserves
  every semantic record and value. No artifact enters this class without two-run evidence.
- `diagnostic-only`: RSS samples, wall/CPU timing, host process details, and prototype/old-revision
  outputs. These inform performance but cannot waive correctness.
- `excluded`: temporary files with no user or semantic contract. Exclusion must be explicit before
  the optimized run and cannot be added after observing a difference.

Every generated file must appear in exactly one class. Missing, extra, or unclassified optimized
artifacts fail the milestone comparison.

## Batch Reuse Map

Pre-refactor commit: `aa2b26a`

Semantic batch: freeze an immutable local/MTP acceptance corpus and comparison manifest.

Old active symbols: public `ub-quick-example` entry point and existing guarded probe scripts; no
production symbol changes.

Old call paths: case directory -> `ub-quick-example` -> canonical output/runlog plus runtime storage
snapshot.

Continuous code blocks reused: guarded-runner process-tree sampling, wall/RSS termination, input and
binary hashing, fresh output directory enforcement, and evidence capture.

Reuse mode: reuse existing scripts where their recorded behavior matches T43; minimally extend only
the evidence manifest or comparison harness if a required hash/classification is absent.

Required lifecycle adaptations: none.

Preserved ordering and observable behavior: case inputs, task scheduling, reservation order,
network results, task `START`/`COMPLETE_VISIBLE` timestamps, output schemas, and normal commands.

Existing characterization: post-fix MTP16 p001/p004/p016 and owner-attribution runs are diagnostic
prior art only.

Explicit behavior changes: none.

Excluded paths: prototype reclamation flag and policy, production UB source, p064/full workload,
trace compression, event aggregation, and unrelated lifecycle-performance evidence.

Focused validation: manifest schema/self-check, input and binary hash verification, fresh-directory
rejection, complete artifact classification, and repeat-run byte/canonical stability.

Re-approval triggers: baseline cannot be built at exact `aa2b26a`; public runner needs source
changes; local/MTP runtime variants cannot be made configuration-equivalent; a canonical artifact
is unstable without a semantics-preserving canonicalizer; a guard fires; or p064/full workload is
proposed.

## Executed Commands And Expected Artifacts

The following commands were run serially in the detached reference worktree. Each run used a fresh
case copy with no inherited `output/` or `runlog/` directory.

```text
python3.12 ./ns3 configure --enable-modules=unified-bus --disable-examples --disable-tests \
  --disable-mpi --enable-mtp --disable-werror -d release -G Ninja
python3.12 ./ns3 build -j 2 ub-quick-example

python3.12 scratch/20260721-rtp-tp-memory-probe/scripts/run_guarded.py p001 \
  --run-id t43-aa2b26a-local-a \
  --case-dir scratch/20260721-rtp-tp-memory-probe/runtime-cases/t43-p001-local-a \
  --single-thread
python3.12 scratch/20260721-rtp-tp-memory-probe/scripts/run_guarded.py p001 \
  --run-id t43-aa2b26a-local-b \
  --case-dir scratch/20260721-rtp-tp-memory-probe/runtime-cases/t43-p001-local-b \
  --single-thread
python3.12 scratch/20260721-rtp-tp-memory-probe/scripts/run_guarded.py p001 \
  --run-id t43-aa2b26a-mtp16-a \
  --case-dir scratch/20260721-rtp-tp-memory-probe/runtime-cases/t43-p001-mtp-a \
  --mtp-threads 16
python3.12 scratch/20260721-rtp-tp-memory-probe/scripts/run_guarded.py p001 \
  --run-id t43-aa2b26a-mtp16-b \
  --case-dir scratch/20260721-rtp-tp-memory-probe/runtime-cases/t43-p001-mtp-b \
  --mtp-threads 16
```

Each run must contain `manifest.json`, `command.json`, `stdout.txt`, `stderr.txt`, `time-l.txt`,
`rss-samples.csv`, `exit-code.txt`, `runtime-storage-snapshot.csv`, and exactly 1,024 task trace
files. Expected source/binary/input hashes, completion status, artifact classes, stable digests, RSS,
and wall measurements are recorded in `t43-baseline-manifest.json`.

The concrete ticket acceptance commands are:

```text
jq empty .scratch/rtp-tp-memory-production/t43-baseline-manifest.json
bash -n .scratch/rtp-tp-memory-production/verify-t43-baseline.sh
.scratch/rtp-tp-memory-production/verify-t43-baseline.sh
git diff --check -- tickets.md .scratch/rtp-tp-memory-production
```

The verifier must print `T43 baseline verified: aa2b26a6050fb37b9136f9dd87d669afc90627f3`.
No p004, p016, p064, or full-workload command is part of T43 acceptance.

## Dirty Worktree Exclusions

Do not build from, clean, stage, or overwrite the existing prototype edits in the application and
traffic-generator sources, `.scratch/rtp-tp-reclamation-prototype/`, or any unrelated files under
`.scratch/unified-bus-lifecycle-performance/`. Do not include existing `results*.txt` or unrelated
scratch experiment trees in T43 commits.

## Completion Evidence

- Immutable manifest for each accepted runtime variant and case.
- Two-run stability report and complete artifact classification.
- Hash-verification and comparison commands that fail on drift, missing/extra artifacts, or a wrong
  executable.
- Serial guarded run records for only the approved frontier.
- `git diff --check` and a worktree-status audit proving the ticket did not stage unrelated data.

## Execution Record

- A detached permanent reference worktree was created at `.worktrees/t43-reference-aa2b26a` and
  built once from exact source `aa2b26a` with release MTP enabled, MPI/tests/examples disabled, and
  build parallelism limited to two jobs.
- The frozen executable SHA-256 is
  `f7ff27a7dca03cf8a6975e4469db551869803c53b32235ae660289946a0eb1c6`; the Unified Bus library
  SHA-256 is `5f5db1ebc37dfdee6ec0d22bb15e4aad9d2b5863d49391b7d4406c11e2566606`.
- Two single-thread and two MTP16 p001 runs completed serially below both guards. All four produced
  exactly 1,024 task trace files with the same byte-level tree digest
  `0a94a0f5a15af54fad159cd839e904170f4a7672a548214ab616ae77827df270`.
- Both MTP16 storage snapshots are byte-identical. The two local snapshots differ only in
  trace-writer buffered-byte and write/flush counters; all non-trace-writer rows have canonical
  digest `c3533ccc11dbc343283cf92b9daa07d24dfe3744de9d5510759b5eed75e0f662` across all four runs.
- No p004, p016, p064, or full-workload run was started. The next scale checkpoint remains behind a
  fresh manual gate.
- Machine-readable facts and classifications are in `t43-baseline-manifest.json`; the frozen
  reference is checked by `verify-t43-baseline.sh` without rebuilding or rerunning it.
- The committed `baseline/` evidence package contains the exact p001 inputs, all four guarded run
  manifests/commands/stdout/stderr/timing/RSS/exit/snapshot files, the complete 1,024-path task-trace
  checksum inventory for each individual run, and a compressed exact executable plus linked ns-3
  libraries and CMake cache. This keeps metadata and runtime bytes verifiable even if the optional
  nested reference worktree is later removed.
- Standards review and specification review both passed after the verifier was hardened to unpack
  and validate the runtime archive, all four per-run task-trace checksum lists, build flags,
  commands, runtime modes, guards, input hashes, artifact inventory, and MTP raw snapshot.
- Implementation commit: `48edc12`.
