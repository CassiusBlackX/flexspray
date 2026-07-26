# T55 MTP16 Owner Cleanup Analysis

## Prediction vs Actual

| row | role | classification | wall | peak RSS | canonical |
| --- | --- | --- | ---: | ---: | --- |
| t55-before | control | matched | 0.74 s | 16924672 B | 480a58051753e964ea369e87aa4984c7850e3b65e6c7d663d679f1cd1d75668a |
| t55-after | treatment | matched | 1.2 s | 16515072 B | 480a58051753e964ea369e87aa4984c7850e3b65e6c7d663d679f1cd1d75668a |

Canonical byte equality: `True`.

Dependency phase ordering (both rows): latest phase-0 `COMPLETE_VISIBLE` =
`1799`, earliest phase-1
`START` = `2799` time steps.

## Treatment Storage Witness

- Resident endpoints: 64
- Final live backing: 0
- Final compact records: 64
- Peak live backing: 64
- Successful backing restorations: 64
- Completed/stored traffic tasks: 64/64
- Remaining dependency references: 0

## Evidence Boundary

Compact records and restoration counts are simulator-emitted, run-derived aggregate evidence.
Canonical output proves phase 1 starts strictly after every phase-0 completion is visible. The input
proves phase 1 reuses the same endpoint identity keys. The snapshot reports 64 aggregate backing
restorations across 64 resident endpoints; per-identity restoration counts are unavailable. Fine-grained
`COMPLETE_VISIBLE -> cleanup` ordering, wrong-owner rejection, public-LP rejection, and missing-owner
behavior remain focused-test-derived from `unified-bus-lifecycle`,
`unified-bus-parallel-lifecycle`, and `unified-bus-tp-reservation`; they are not promoted to
run-derived evidence.
