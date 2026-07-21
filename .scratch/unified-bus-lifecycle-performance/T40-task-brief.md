# T40 Task Brief: Curate the promotion tree and publish final acceptance

## Batch 1 Reuse Map: Promotion inventory and final runner

- Pre-refactor commit: `main@ccf41b58`; protocol control: `next@cf8ee683`.
- Old active assets: T28 frozen runner, manifest, complex CTP/RTP inputs and comparison summaries;
  T34-T39 reports and summaries; T36 portable CTP aggregate.
- Old call path: frozen role binary -> immutable case input -> production parser/comparator -> compact
  manifest and aggregate summary.
- Continuous code reused: T28 SHA-256, input copy, serial timeout/run, `/usr/bin/time -l`, artifact
  hashing, TaskTrace reorder classification, task CSV reader, and production lifecycle parser.
- Reuse mode: direct import plus a thin final-milestone orchestrator.
- Required adaptations: pin current production revision `61abfaf` and exact next revision
  `cf8ee683`; write into a new T40 evidence root; validate six complex cases without overwriting the
  archived T28/T36 trees.
- Preserved behavior: release local-single, `rng-run=1`, identical paired input bytes, strictly serial
  runs, and the existing approved RTP difference boundaries.
- Explicit behavior changes: none.
- Excluded paths: source changes, new cases, performance reruns, automatic TP cleanup, MTP/MPI, and
  deletion of untracked user evidence.
- Validation: freeze/verify role and input hashes, run 12 simulations serially, compare all shared
  artifacts, validate current lifecycle traces with the production parser, and verify frozen hashes
  again after the matrix.
- Stop conditions: moved revision/binary/library, changed input, failed run, new path/hash difference,
  task-set mismatch, parser failure, or a broadened exception.

## Batch 2 Reuse Map: Final report and closeout

- Reused evidence: T28 main/current/next semantic and p01/p02 matrix; T34 LDST calibration; T35/T36
  CTP 2 GiB and 8 GiB performance; T36 complex CTP bytes; T37 DCQCN differential; T38/T39 focused
  owner-seam validation.
- Reuse mode: summarize existing frozen evidence and append the new T40 complex aggregate.
- Required adaptations: bind the report to final production and evidence revisions, keep module-level
  performance results separate, document the macOS umbrella-runner residual accurately, and retain
  automatic TP cleanup as excluded/not implemented.
- Preserved behavior: no protocol, packet, queue, ACK/RTO, callback, timer, scheduler, or lifecycle
  state change.
- Validation: final source-vs-next/main independent review, tracker audit, `git diff --check`, and
  clean staging limited to promotion artifacts followed by closeout artifacts.

## Ticket-Defined Acceptance Suite

1. Existing targeted build and T38/T39 focused suites remain valid because no later source changes
   occur.
2. Re-run CTP windows 1/4/128 and RTP shared-window, GBN double-drop, and TPG pinning against exact
   next with frozen role/input hashes.
3. Require exact CTP shared artifacts and task rows. Require each RTP role to reproduce its archived
   T28 shared tree exactly, thereby preserving the already reviewed difference paths and hashes.
4. Require non-degenerate, next-matched CTP window completion vectors and valid current lifecycle
   trace parsing.
5. Preserve earlier main exact/controlled-difference classification and all module-specific
   performance results; do not rerun or average them into one number.
6. Run final review and `git diff --check`. Do not run the macOS umbrella suite or claim it passes.

## Ledger

- 2026-07-21: T40 opened after T39 implementation `cbc0567` and closeout `61abfaf`.
- 2026-07-21: inventory found 17,880 untracked files: 8,685 raw/run artifacts and 9,195 other local
  evidence or case files. None will be deleted or staged wholesale.
- 2026-07-21: all 12 frozen simulations completed serially. The first aggregate incorrectly required
  full RTP task-row equality; analysis was corrected to the existing T28 archived-replay plus task-ID
  gate and rerun from saved results without rerunning any simulation. Final aggregate status: PASS.
- 2026-07-21: post-promotion analysis permits documentation/evidence-only descendant commits only
  when the complete `src/unified-bus` tree still matches the frozen revision. Binary, library, input,
  and accepted-comparison hashes remain exact gates.
