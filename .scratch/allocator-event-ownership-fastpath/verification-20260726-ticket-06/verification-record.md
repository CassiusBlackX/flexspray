# Ticket 06 Serial Verification

- Working directory: `/Users/ytxing/workspace/ns-3-ub-architecture-integration`
- Runner: `./build/utils/ns3.44-test-runner`
- Environment: `DYLD_LIBRARY_PATH=build/lib`
- Per-suite command shape: `./build/utils/ns3.44-test-runner --test-name=<suite> --stop-on-failure --xml --out=<suite>.xml`
- Rebuild: not needed; the built runner was executable and `--help` succeeded.
- Execution: serial, in the requested order; all 15 suites exited `0` and XML `<Result>` was `PASS`.
- Total measured suite wall time: `3.716 s`.
- Residual process check after execution: no `ninja`, test-runner, ns3, simulator, or simulation process found.

| Suite | Exit | Elapsed (s) | XML |
|---|---:|---:|---|
| `unified-bus-tp-reservation` | 0 | 0.108 | PASS |
| `unified-bus-switch-voq` | 0 | 0.032 | PASS |
| `unified-bus-lifecycle-storage` | 0 | 0.035 | PASS |
| `unified-bus-lifecycle` | 0 | 0.174 | PASS |
| `unified-bus-ctp-lifecycle` | 0 | 0.041 | PASS |
| `unified-bus-ctp-transaction` | 0 | 0.030 | PASS |
| `unified-bus-rtp-resolution` | 0 | 0.029 | PASS |
| `unified-bus-parallel-lifecycle` | 0 | 0.029 | PASS |
| `unified-bus-transport-retrans` | 0 | 0.035 | PASS |
| `unified-bus-retrans-timer-scan` | 0 | 0.033 | PASS |
| `unified-bus-retrans-cc-regression` | 0 | 0.029 | PASS |
| `unified-bus-examples-smoke` | 0 | 0.593 | PASS |
| `unified-bus-examples-compat` | 0 | 2.200 | PASS |
| `unified-bus-structured-routing` | 0 | 0.206 | PASS |
| `unified-bus-runtime-storage-snapshot` | 0 | 0.142 | PASS |

Per-suite stdout, stderr, and XML files are stored in this directory. `run-status.tsv` records
the exact absolute paths and measured elapsed time for each invocation.
