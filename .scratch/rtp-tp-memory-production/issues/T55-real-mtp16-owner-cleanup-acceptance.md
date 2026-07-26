# T55: Real MTP16 Owner Cleanup Acceptance

Status: 完成

Blocked by: T51, T52, T53, T54

## Objective

Use one bounded public `ub-quick-example` A/B experiment to verify canonical equivalence and observe
compact TP backing followed by owner restoration under 16 MTP partitions.

## Invariants

- Do not change completion order, causal delivery, owner gating, reclaim eligibility, CC state, or
  public CLI behavior.
- Run baseline/current once each as formal evidence, serially, with 2 GiB RSS and 120 s wall guards.
- Mark wrong-owner rejection as test-derived unless a public case can express it without new
  behavior.

## Acceptance

- Complete plan/matrix/manifest/ledger and two case specs.
- Byte-identical canonical output.
- Treatment snapshot proves compact records and successful restoration with valid storage and
  traffic invariants.
- Fail-closed analysis and the requested focused suites pass.

## Implementation Record

- Formal control and treatment ran once each, serially, with the declared 2 GiB/120 s guards.
- Both returned 0. Control wall/peak RSS: 0.74 s / 16,924,672 B. Treatment wall/peak RSS:
  1.20 s / 16,515,072 B.
- Both normalized canonical outputs contain 128 events and have SHA-256
  `480a58051753e964ea369e87aa4984c7850e3b65e6c7d663d679f1cd1d75668a`.
- Treatment snapshot: 32 canonical connections, 64 resident endpoints, 0 final live backing, 64
  compact records, 64 successful backing restorations, 64 peak live backing, 64/64 completed tasks,
  and 0 dependency references.
- Analysis classified both rows `matched`; formal gate passed. Wrong-owner/public-owner rejection is
  explicitly test-derived, not run-derived.
- Both canonical outputs prove all phase-1 starts at 2799 occur after the latest phase-0 visible
  completion at 1799. The treatment reports 64 aggregate restorations across 64 resident endpoints;
  per-identity restoration counts are unavailable. Fine-grained `COMPLETE_VISIBLE -> cleanup`
  ordering remains focused-test evidence.
- The runner renders formal commands from the command manifest, revalidates source/build provenance
  immediately before `Popen`, and always terminates and reaps the unique process group after any
  post-spawn sampling, recording, or interrupt exception.
- The analyzer directly rehashes all five copied case inputs, validates the exact 64-task phase pair
  identities and dependencies, requires the exact storage witness, rehashes raw artifacts, and offers
  a write-free `--verify-only` mode.
- Post-run mapped-owner test changes are reported as live source-tree drift. Frozen raw provenance is
  unchanged, and the live formal binary/library/cache hashes and mtimes still match.
- Validation passed with the non-default runner:
  - `unified-bus-tp-reservation`
  - `unified-bus-lifecycle`
  - `unified-bus-parallel-lifecycle`
  - `unified-bus-ctp-lifecycle`
  - aggregate `unified-bus`
