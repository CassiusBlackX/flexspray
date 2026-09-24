# Run Ledger

## Environment and authorization

- Package: `scratch/20260921-2nodes-roi-write-counter/`; repository branch `ub-2.1`.
- User explicitly authorized all three tickets, necessary environment/build/run/review/local commits. No additional approval checkpoint.
- Existing uncommitted `CONTEXT.md` preserved. No production modules or interfaces changed.
- Python 3.12; release Ninja configuration, MPI/MTP/examples/tests disabled as planned.
- README/QUICK_START checked: launcher and smoke case exist. Tool submodule remains empty; approved direct-API driver and standard-library comparison do not consume CSV tooling.
- Configure and required builds succeeded. [Build logs](output/build-logs/) preserve actual commands/output.
- Runtime catalog: [type IDs](output/runtime-catalog/typeids.txt), [globals](output/runtime-catalog/globals.txt), [41 component query commands/return codes](output/runtime-catalog/queries.json). All queries exited 0. Actual scalar parameters are also queried in each driver process.
- Driver/source and binary hashes are in final resolved-parameters and every final run-record. Recorder adds provenance after capture, without changing simulator inputs.
- [Machine-readable complete run history](output/run-history.jsonl). Earlier ticket validation is retained, not treated as extra network matrix rows.

## Checkpoint policy

`continue_full_matrix`; no manual pause. Each control run completed before its independent treatment process. No matrix row was skipped, paused or failed. Timing mismatches would not stop the other row.
Output collisions were handled by timestamped archives before every rerun. Ticket03 repeated its fixed pair once to validate the packaged recorder; this changed only evidence capture, not workload or parameters.

## Simulation runs

| # | Stage | Case | Status | Return | UTC start | UTC end | Evidence |
| --- | --- | --- | --- | ---: | --- | --- | --- |
| 1 | ticket-01 | control | success | 0 | 2026-09-21T09:33:43.830629+00:00 | 2026-09-21T09:33:43.912293+00:00 | [record](cases/control/archives/20260921T093503303415Z/run-record.json) |
| 2 | ticket-01-wire-order | control | success | 0 | 2026-09-21T09:35:03.338677+00:00 | 2026-09-21T09:35:03.415781+00:00 | [record](cases/control/archives/20260921T093629481482Z/run-record.json) |
| 3 | ticket-01-final | control | success | 0 | 2026-09-21T09:36:29.515609+00:00 | 2026-09-21T09:36:29.594698+00:00 | [record](cases/control/archives/20260921T093822609231Z/run-record.json) |
| 4 | ticket-02 | control | success | 0 | 2026-09-21T09:38:22.645179+00:00 | 2026-09-21T09:38:22.732741+00:00 | [record](cases/control/archives/20260921T094626316645Z/run-record.json) |
| 5 | ticket-02 | treatment | success | 0 | 2026-09-21T09:38:22.813474+00:00 | 2026-09-21T09:38:22.892443+00:00 | [record](cases/treatment/archives/20260921T094626478781Z/run-record.json) |
| 6 | ticket-03-matrix | control | success | 0 | 2026-09-21T09:46:26.354368+00:00 | 2026-09-21T09:46:26.435048+00:00 | [record](cases/control/archives/20260921T094914377030Z/run-record.json) |
| 7 | ticket-03-matrix | treatment | success | 0 | 2026-09-21T09:46:26.514755+00:00 | 2026-09-21T09:46:26.598669+00:00 | [record](cases/treatment/archives/20260921T094914543625Z/run-record.json) |
| 8 | ticket-03-recorder-validation | control | success | 0 | 2026-09-21T09:49:14.411669+00:00 | 2026-09-21T09:49:14.489454+00:00 | [record](cases/control/output/run-record.json) |
| 9 | ticket-03-recorder-validation | treatment | success | 0 | 2026-09-21T09:49:14.579312+00:00 | 2026-09-21T09:49:14.655515+00:00 | [record](cases/treatment/output/run-record.json) |

## Exact run provenance

### Run 1: control

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=control --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/control/output'
```

- HEAD: `a9ebda9b106c850db862e466a3a788c92d13b78f`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/control/archives/20260921T093503303415Z/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `None`; reason: None.
- Source SHA-256: `not captured in this early acceptance attempt`.
- Binary SHA-256: `not captured in this early acceptance attempt`.
- Dirty state at execution:

```text
 M CONTEXT.md
?? scratch/20260921-2nodes-roi-write-counter/
```

### Run 2: control

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=control --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/control/output'
```

- HEAD: `a9ebda9b106c850db862e466a3a788c92d13b78f`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/control/archives/20260921T093629481482Z/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `scratch/20260921-2nodes-roi-write-counter/cases/control/archives/20260921T093503303415Z`; reason: preserve earlier ticket acceptance before next planned run.
- Source SHA-256: `not captured in this early acceptance attempt`.
- Binary SHA-256: `not captured in this early acceptance attempt`.
- Dirty state at execution:

```text
 M CONTEXT.md
A  scratch/20260921-2nodes-roi-write-counter/README.md
A  scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter.cc
?? scratch/20260921-2nodes-roi-write-counter/cases/
```

### Run 3: control

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=control --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/control/output'
```

- HEAD: `a9ebda9b106c850db862e466a3a788c92d13b78f`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/control/archives/20260921T093822609231Z/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `scratch/20260921-2nodes-roi-write-counter/cases/control/archives/20260921T093629481482Z`; reason: preserve earlier ticket acceptance before next planned run.
- Source SHA-256: `f26c7c3391907341ed4fdcef81ced0cb3af98614a250097e3bff55fb71688dfe`.
- Binary SHA-256: `b467d8b18704d5e82e1f587a7d3e85b61780c9194405e03a3fc1b13074992a3e`.
- Dirty state at execution:

```text
 M CONTEXT.md
A  scratch/20260921-2nodes-roi-write-counter/README.md
AM scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter.cc
?? scratch/20260921-2nodes-roi-write-counter/.gitignore
```

### Run 4: control

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=control --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/control/output'
```

- HEAD: `79f45f001d287eeeec47f0aa6b4ac25f0af928cb`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/control/archives/20260921T094626316645Z/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `scratch/20260921-2nodes-roi-write-counter/cases/control/archives/20260921T093822609231Z`; reason: preserve earlier ticket acceptance before next planned run.
- Source SHA-256: `8e6e58becf4ec5a1db571b86861836e31d3a465ab4e9b9623fd28b69e2b15806`.
- Binary SHA-256: `03b052cf31057b4990c0d80b87ce4683b0b57112d920fa439daf533010ff8d20`.
- Dirty state at execution:

```text
 M CONTEXT.md
 M scratch/20260921-2nodes-roi-write-counter/README.md
 M scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter.cc
```

### Run 5: treatment

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=treatment --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/treatment/output'
```

- HEAD: `79f45f001d287eeeec47f0aa6b4ac25f0af928cb`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/treatment/archives/20260921T094626478781Z/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `None`; reason: None.
- Source SHA-256: `8e6e58becf4ec5a1db571b86861836e31d3a465ab4e9b9623fd28b69e2b15806`.
- Binary SHA-256: `03b052cf31057b4990c0d80b87ce4683b0b57112d920fa439daf533010ff8d20`.
- Dirty state at execution:

```text
 M CONTEXT.md
 M scratch/20260921-2nodes-roi-write-counter/README.md
 M scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter.cc
```

### Run 6: control

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=control --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/control/output'
```

- HEAD: `2a2fd1470256b6c5c2059e53b1dcbfd3a173991e`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/control/archives/20260921T094914377030Z/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `scratch/20260921-2nodes-roi-write-counter/cases/control/archives/20260921T094626316645Z`; reason: preserve earlier ticket acceptance before next planned run.
- Source SHA-256: `8e6e58becf4ec5a1db571b86861836e31d3a465ab4e9b9623fd28b69e2b15806`.
- Binary SHA-256: `03b052cf31057b4990c0d80b87ce4683b0b57112d920fa439daf533010ff8d20`.
- Dirty state at execution:

```text
 M CONTEXT.md
?? scratch/20260921-2nodes-roi-write-counter/cases/
?? scratch/20260921-2nodes-roi-write-counter/command-manifest.yaml
?? scratch/20260921-2nodes-roi-write-counter/compare-results.py
?? scratch/20260921-2nodes-roi-write-counter/experiment-plan.md
?? scratch/20260921-2nodes-roi-write-counter/matrix.yaml
?? scratch/20260921-2nodes-roi-write-counter/run-ledger.md
```

### Run 7: treatment

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=treatment --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/treatment/output'
```

- HEAD: `2a2fd1470256b6c5c2059e53b1dcbfd3a173991e`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/treatment/archives/20260921T094914543625Z/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `scratch/20260921-2nodes-roi-write-counter/cases/treatment/archives/20260921T094626478781Z`; reason: preserve earlier ticket acceptance before next planned run.
- Source SHA-256: `8e6e58becf4ec5a1db571b86861836e31d3a465ab4e9b9623fd28b69e2b15806`.
- Binary SHA-256: `03b052cf31057b4990c0d80b87ce4683b0b57112d920fa439daf533010ff8d20`.
- Dirty state at execution:

```text
 M CONTEXT.md
?? scratch/20260921-2nodes-roi-write-counter/cases/
?? scratch/20260921-2nodes-roi-write-counter/command-manifest.yaml
?? scratch/20260921-2nodes-roi-write-counter/compare-results.py
?? scratch/20260921-2nodes-roi-write-counter/experiment-plan.md
?? scratch/20260921-2nodes-roi-write-counter/matrix.yaml
?? scratch/20260921-2nodes-roi-write-counter/run-ledger.md
```

### Run 8: control

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=control --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/control/output'
```

- HEAD: `2a2fd1470256b6c5c2059e53b1dcbfd3a173991e`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/control/output/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `scratch/20260921-2nodes-roi-write-counter/cases/control/archives/20260921T094914377030Z`; reason: preserve previous evidence before explicit rerun.
- Source SHA-256: `8e6e58becf4ec5a1db571b86861836e31d3a465ab4e9b9623fd28b69e2b15806`.
- Binary SHA-256: `03b052cf31057b4990c0d80b87ce4683b0b57112d920fa439daf533010ff8d20`.
- Dirty state at execution:

```text
 M CONTEXT.md
AM scratch/20260921-2nodes-roi-write-counter/compare-results.py
?? scratch/20260921-2nodes-roi-write-counter/cases/
?? scratch/20260921-2nodes-roi-write-counter/command-manifest.yaml
?? scratch/20260921-2nodes-roi-write-counter/experiment-plan.md
?? scratch/20260921-2nodes-roi-write-counter/matrix.yaml
?? scratch/20260921-2nodes-roi-write-counter/record-run.py
?? scratch/20260921-2nodes-roi-write-counter/run-ledger.md
```

### Run 9: treatment

```sh
python3.12 ./ns3 run --no-build 'scratch/20260921-2nodes-roi-write-counter/ub-roi-write-counter --variant=treatment --output-dir=scratch/20260921-2nodes-roi-write-counter/cases/treatment/output'
```

- HEAD: `2a2fd1470256b6c5c2059e53b1dcbfd3a173991e`; failure category: `None`; retryable by recorded archive policy.
- Actual artifacts: `cases/treatment/output/` — events.csv, summary.json, resolved-parameters.json, console.log, run-record.json.
- Prior evidence archive: `scratch/20260921-2nodes-roi-write-counter/cases/treatment/archives/20260921T094914543625Z`; reason: preserve previous evidence before explicit rerun.
- Source SHA-256: `8e6e58becf4ec5a1db571b86861836e31d3a465ab4e9b9623fd28b69e2b15806`.
- Binary SHA-256: `03b052cf31057b4990c0d80b87ce4683b0b57112d920fa439daf533010ff8d20`.
- Dirty state at execution:

```text
 M CONTEXT.md
AM scratch/20260921-2nodes-roi-write-counter/compare-results.py
?? scratch/20260921-2nodes-roi-write-counter/cases/
?? scratch/20260921-2nodes-roi-write-counter/command-manifest.yaml
?? scratch/20260921-2nodes-roi-write-counter/experiment-plan.md
?? scratch/20260921-2nodes-roi-write-counter/matrix.yaml
?? scratch/20260921-2nodes-roi-write-counter/record-run.py
?? scratch/20260921-2nodes-roi-write-counter/run-ledger.md
```

## Comparison and evidence checks

```sh
python3.12 scratch/20260921-2nodes-roi-write-counter/compare-results.py --control-dir=scratch/20260921-2nodes-roi-write-counter/cases/control/output --treatment-dir=scratch/20260921-2nodes-roi-write-counter/cases/treatment/output --output-dir=scratch/20260921-2nodes-roi-write-counter/output
```

UTC: 2026-09-21T09:54:08.821759+00:00 → 2026-09-21T09:54:08.912428+00:00; return code 0; HEAD `2a2fd1470256b6c5c2059e53b1dcbfd3a173991e`.
Prior report archived at `scratch/20260921-2nodes-roi-write-counter/output/comparison-archives/20260921T095408814988Z`: final report includes measured payload/nonpayload split after review.
[Full comparison run record](output/comparison-run-record.json), [console](output/comparison-console.log), [report](output/comparison.md), [JSON](output/comparison.json).

Comparator development initially rejected both cases because ns-3 rendered 1 us as `+1e+06ps`; a first correction also classified `bps` as a time suffix. Both deterministic formatting bugs were fixed before final comparison. No simulator inputs changed. Final parameter comparison is numeric for time attributes.

Final `compare-results.py --self-test`: all 8 checks passed, including valid/no-speedup→mismatched. [Results](output/self-test.json). Bytecode compilation passed. Negative samples only mutate copies of evidence; they are not network runs.

## Measured result

Both cases structurally valid. Control B/A = 3,030,800 / 4,042,040 ps; treatment = 1,008,320 / 2,019,560 ps. ΔB=2,022,480 ps (66.730896%). Propagation RTT=2,000,000 ps; residual=22,480 ps, fully accounted at observed boundaries. Hypothesis matched within this single-packet model; see report for limits.

## Final review follow-up

Recorder review found that truncated parameter JSON could leave a completed process marked running.
The process outcome is now persisted before parameter enrichment; enrichment errors and recorder
exit code are separate from the actual child return code. `test-recording.py` passed both child
exit -9 and child exit 0 with truncated JSON; archive and history evidence remained intact.
[Focused test log](output/recording-test.log). Standards reviewer confirmed the fix.
Spec reviewer found no unresolved defects, independently checked failed-process rejection and a
valid negative-benefit sample. C++ source was unchanged during ticket03, so ticket02's targeted
build remains the final relevant C++ build. No production or build-system tests were needed.
