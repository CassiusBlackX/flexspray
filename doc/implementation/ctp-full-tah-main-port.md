# CFG6 CTP header migration onto GitCode main

Base: `c10ba71f` (GitCode main, fetched 2026-10-08).
Destination: `fix/ctp-full-tah`.
Source: `a4e70258..7fa4dc79` on `feat/ub-2.1-ddie`.

## Scope and adaptations

All twelve implementation, test and closeout commits are cherry-picked in their original order,
with `-x` trailers identifying the source commits. GitCode main does not contain Iodie or DDIE.
Their caller adaptations, tests and scenario configuration edits are therefore omitted when
resolving modify/delete conflicts. The UnifiedBus implementation and tests are retained.
Reference edits follow main's `.agents/skills/` directory rename.

The migrated `UbJetty::CanReserveCtpWqe` uses
`std::min(m_taSegmentBytes, maxSegmentBytes)`, the same calculation already used in main's
`PrepareNextWqeSegment` and `PushWqe`. The source branch's `GetEffectiveSegmentBytes` helper
predated the selected commits and is not present in main. This adaptation preserves segment
limits and sequence reservation behavior without importing unrelated device modules.

Main's `TargetExecutionDelay` attribute and regression tests, scenario completion fixes and
trace-tools submodule revision are preserved. The source branch and existing untracked cases
are retained.

## Commit mapping

| Source | Destination | Purpose |
| --- | --- | --- |
| `85667a83` | `6ba51828` | Shared full transaction header codec |
| `2ca1645b` | `ada0985f` | Codec closeout |
| `e8eb31e6` | `8f8a917b` | Independent requester RCIDs |
| `8ca8f1c8` | `4504425b` | Requester preparation closeout |
| `eda5d678` | `55dfed30` | Full CFG6 wire headers and response matching |
| `40df13d8` | `2f84f2fe` | Wire migration closeout |
| `8d99d105` | `c5eafff5` | Independent requester progress |
| `7db1b868` | `f08c2118` | Progress closeout |
| `0eb4248f` | `d7dc57a7` | Safe requester retirement |
| `56b369e7` | `e66c8c62` | Retirement closeout |
| `7d62c68a` | `73cceaca` | Integration tests and documentation |
| `7fa4dc79` | `58f99f4a` | Integration closeout |

## Validation

The six original ticket records describe historical validation on the source branch, including
Iodie/DDIE and its 190-suite result. They are not evidence of validation on this destination.
This port validates the release UnifiedBus configuration with tests enabled and MPI/MTP
disabled. No new test oracle or protocol behavior is introduced.

Commands executed sequentially on the destination tree:

```bash
python3.12 ./ns3 configure --enable-modules=unified-bus --disable-examples --enable-tests --disable-mpi --disable-mtp --disable-werror -d release -G Ninja
python3.12 ./ns3 build -j 2
python3.12 ./test.py --no-build --jobs 1 --retain --text=.scratch/ctp-main-port-20261008/all-tests
python3.12 -m unittest discover -s tests/openusim_skills -p test_skill_docs.py
git diff --check gitcode/main
```

Results: configured build PASS; all 182 QUICK test suites PASS, with zero skipped, failed or
crashed suites; all 20 skill-documentation tests PASS; diff whitespace check PASS. The 49
UnifiedBus suites include CTP transactions/lifecycle/input/endpoints, wire format, transaction
header codecs, Atomic/extensions, target execution, RTP, rerouting and original example entry
points. Iodie/DDIE and MPI/MTP are outside this configuration; no coverage is claimed for them.

Fresh evidence: `.scratch/ctp-main-port-20261008/all-tests.txt` and retained XML reports in
`testpy-output/2026-10-09-06-55-03-CUT/`. Existing single-TP `test`, `runlog` and `output`
directories were moved aside before system tests and restored afterward; newly generated
artifacts are retained under `.scratch/ctp-main-port-20261008/validation-*`.

Review against the source tip and main confirms that the remaining UnifiedBus differences
are the pre-existing branch differences plus the equivalent segment-limit adaptation above.
No Iodie/DDIE module files or old tools revision were introduced. The original twelve source
commits remain reachable through the unchanged `feat/ub-2.1-ddie` branch.
