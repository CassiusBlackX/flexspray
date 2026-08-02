# Run Ledger

- Package status: `materialized-dry-run-passed`.
- Planned simulator runs: 18, serial.
- Simulation execution approved: `false`.
- Candidate source revision and binary SHA are captured by preflight.
- All 18 case directories passed fixture-hash, runtime-default, pair-input and command dry-run
  checks. No simulation has been started.

| Pair | Modes | Status |
| --- | --- | --- |
| S01 explicit/default | local | pending |
| S03 explicit/default | local, MTP2 | pending |
| S08 explicit/default | local | pending |
| S09 explicit/default | local, MTP2 | pending |
| S10 explicit/default | MTP8 | pending |
| S11 explicit/default | local | pending |
| S12 explicit/default | local | pending |

Machine-readable per-run state is written to `run-ledger.json` only after simulation execution.
