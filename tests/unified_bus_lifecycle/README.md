# Unified Bus lifecycle reference baseline

This directory freezes the T00 acceptance contract. It contains only tracked,
path-independent metadata, comparison rules, case inputs, tests, and the
comparison harness. The large executables, dylibs, parser environment, and run
outputs remain under the gitignored artifact root:

```text
.openusim/unified-bus-lifecycle/reference/
```

`reference-manifest.json` is authoritative for the source commit, exact build
commands, toolchain, linked libraries, runtime variants, hashes, accepted run
paths, and complete output classification. T00 freezes exactly one executable
for each incompatible accepted build configuration. Every later ticket must
reuse the exact matching variant; do not rebuild a missing binary or substitute
a different compile-time superset. Restore the frozen artifact root or return
to T00 explicitly.

## Verify the frozen baseline

Run this before every old-versus-new comparison:

```bash
python3 utils/unified_bus_baseline.py verify \
  --manifest tests/unified_bus_lifecycle/reference-manifest.json \
  --repo-root . \
  --artifact-root .openusim/unified-bus-lifecycle/reference
```

The T00 reference verifies 369 files. A changed or unregistered case input,
comparison rule, binary, dylib, or generated output is a hard failure.

## Runtime variants

Three immutable binaries cover four runtime modes. They represent three
incompatible build configurations and were each built exactly once:

| Runtime mode | Binary variant | Launch shape |
|---|---|---|
| local single-thread | `local-single` | direct |
| local MTP | `local-mtp` | direct, `--mtp-threads=2` |
| MPI single-thread | `parallel-all` | `mpirun -np 2`, `--mtp-threads=0` |
| hybrid MPI/MTP | `parallel-all` | `mpirun -np 2`, `--mtp-threads=2` |

The standard single-thread binary cannot run MTP. Its preserved runtime probe
returns `MTP requested but not compiled`. The `parallel-all` build selects MPI
or hybrid at runtime, so those two modes reuse one frozen executable.

A review probe showed that `parallel-all` can run the cross-owner case directly
in local single-thread mode and its selected outputs match `local-single`.
However, directly running that same binary with `--mtp-threads=2` terminates in
`ns3::UbRemoteLink::TransmitStart()` with `SIGSEGV`. Therefore one superset
binary cannot cover all four accepted modes, and the three frozen variants are
retained as exact configuration references.

On macOS, set `DYLD_LIBRARY_PATH` to the selected variant's `lib/` directory.
Prepend the artifact-root `.venv-parser/bin` directory to `PATH` when producing
`task_statistics.csv` and `throughput.csv`.

## Compare outputs

Candidate outputs must contain the exact managed file set and use the same
relative layout as a reference `case/` directory. For example:

```bash
python3 utils/unified_bus_baseline.py compare \
  --rules tests/unified_bus_lifecycle/comparison-rules.json \
  --case minimal-single-rtp-write \
  --reference .openusim/unified-bus-lifecycle/reference/runs/local-single/minimal-single-rtp-write/run-1/case \
  --candidate .openusim/unified-bus-lifecycle/candidate/minimal-single-rtp-write/case \
  --diagnostics .openusim/unified-bus-lifecycle/diagnostics/minimal-single-rtp-write
```

The managed set includes the copied case inputs and every generated file in the
frozen `case/` tree. A missing managed file or an unclassified new file is a
hard mismatch. The command reports the lexicographically first differing
artifact and its first byte offset, then copies the complete reference and
candidate trees into the new diagnostics directory.

All generated artifacts were raw byte-stable across two accepted reference
runs and are explicitly listed for raw byte equality. Every
`canonical_only_artifacts` list is therefore explicitly empty.
`stdout.txt` and `stderr.txt` are retained as diagnostic originals but excluded
from semantic comparison because stdout contains wall-clock timing, timestamps,
and absolute run paths. Canonicalization is not used and removes no events or
fields.

The cross-owner case has one characterized runtime difference: local single
thread records `COMPLETE_VISIBLE` at 138 ns, while MTP, MPI, and hybrid record
158 ns because the parallel paths apply the positive 20 ns link delay as the
dependency-visibility bound. Each runtime compares against its own frozen
canonical hash; task statistics, throughput, and selected traces are identical
across the four runtimes.
