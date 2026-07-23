# T55 Validation Record

All commands ran from the treatment worktree. Build and tests were serial.

| command | exit | result |
| --- | ---: | --- |
| `python3.12 ./ns3 build -j8 ub-quick-example` | 0 | treatment executable refreshed |
| `python3.12 ./ns3 build -j8 ub-quick-example` in control worktree | 0 | no work; artifacts current |
| `python3.12 scripts/run_t55.py --validate-only ...` | 0 | package, inputs, worktrees, builds validated |
| `python3.12 -W error scripts/test_run_t55.py` | 0 | 8 cleanup/guard/manifest tests passed, including two real process-group integrations |
| `python3.12 scripts/test_analyze_t55.py` | 0 | 13 fail-closed analyzer tests passed |
| `python3.12 -c '... run_t55.validate_package() ...'` | 0 | matrix, case specs, run order, and single-source commands agree |
| `python3.12 scripts/run_t55.py ...` | 0 | both formal rows completed serially |
| `python3.12 scripts/analyze_t55.py --verify-only` | 0 | formal gate true; both rows matched; no writes |
| `python3.12 scripts/analyze_t55.py --regenerate-derived` | 0 | regenerated derived files with old hashes recorded |
| `ninja -C cmake-cache unified-bus-test` | 0 | focused test library rebuilt |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-tp-reservation --fullness=QUICK` | 0 | PASS |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle --fullness=QUICK` | 0 | PASS |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-parallel-lifecycle --fullness=QUICK --verbose` | 0 | PASS |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus-ctp-lifecycle --fullness=QUICK` | 0 | PASS |
| `./build/utils/ns3.44-test-runner --test-name=unified-bus --fullness=QUICK` | 0 | PASS |
| `git diff --check` | 0 | clean |

The focused suites intentionally execute child-process fail-fast cases; their expected abort text is
not a suite failure. Every listed suite returned 0 and printed `PASS`.

After the final Python/manifest/documentation-only corrections, C++ was unchanged. Therefore only
`unified-bus-tp-reservation` (the mapped-owner seam) and aggregate `unified-bus` were rerun; both
returned 0 and printed `PASS`. The earlier same-build runs of lifecycle, parallel lifecycle, and CTP
lifecycle remain recorded above.

The live treatment source-tree hash differs from the frozen formal-run hash because the mapped-owner
focused test was added after the run. This test-only drift is reported, not rewritten. Live hashes and
mtimes for the frozen treatment binary, UnifiedBus library, MTP library, and CMake cache still match
the formal provenance exactly. Raw formal evidence remains write-once.
