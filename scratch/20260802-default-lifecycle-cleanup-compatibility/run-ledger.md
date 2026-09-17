# Run Ledger

- Package status: `completed`.
- Planned simulator runs: 18, serial.
- Simulation execution approved: `true`.
- Candidate source revision and binary SHA are captured by preflight.
- All 18 runs completed with return code 0. All nine explicit/default pairs passed canonical
  byte comparison and trace-derived task/FCT comparison.
- Raw runlog directories are archived at `/tmp/ns3ub-default-lifecycle-runlogs-a6e1303b`; the
  package retains canonical output, snapshots, stdout/stderr, records and parsed analysis.

| Pair | Modes | Status |
| --- | --- | --- |
| S01 explicit/default | local | matched |
| S03 explicit/default | local, MTP2 | matched |
| S08 explicit/default | local | matched |
| S09 explicit/default | local, MTP2 | matched |
| S10 explicit/default | MTP8 | matched |
| S11 explicit/default | local | matched |
| S12 explicit/default | local | matched |

Machine-readable per-run state is written to `run-ledger.json` only after simulation execution.
