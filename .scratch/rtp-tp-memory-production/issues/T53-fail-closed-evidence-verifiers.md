# T53: Make TP Compaction Evidence Verification Fail Closed

Status: complete

Blocked by: none

## Objective

Harden the fixed p001/p004 A/B and p016 checkpoint analyzers so malformed, incomplete, stale, or
prediction-falsifying evidence cannot be reported as verified or matched.

## Allowed Artifacts

- `scratch/20260723-tp-backing-compaction-ab/`
- `scratch/20260723-tp-backing-compaction-p016-checkpoint/`
- This ticket and its closeout notes

Do not modify simulator code or rerun simulations in this ticket.

## Required Behavior

- RSS treatment greater than or equal to control is classified as `mismatched`, never hard-coded
  `matched`.
- Trace verification checks the exact runlog TaskTrace file set, unique relative names, expected
  count, checksum-list equality, and each file hash.
- Runtime snapshot verification checks exact schema/version, required unique metrics, numeric values,
  and count invariants including logical endpoint balance and peak-live bounds.
- A/B verification compares `command.json` with the manifest command, cross-checks binary paths,
  re-hashes existing binary/library/cache artifacts, and verifies a present worktree HEAD. Missing
  artifacts are reported as provenance unavailable rather than verified.
- Completed ledger/case status text must not remain `planned` or claim that no simulation ran.
- Existing valid artifacts continue to produce their independently verified measured numbers; the
  formal preregistered gate remains false where the plan says it is false.

## TDD Acceptance

1. Add isolated analyzer self-tests or fixture-copy tests for each known fail-open case before the
   implementation change: non-improving RSS, duplicate trace name, missing/duplicate snapshot metric,
   command mismatch, and artifact hash mismatch.
2. Run tests only against temporary copies; never alter frozen raw evidence in place.
3. Run each analyzer on the existing package and record its final classification plus any explicit
   provenance-unavailable qualification.
4. Run Python syntax checks and a package diff/status audit.

## Closeout

Raw admitted result artifacts were held immutable. Their aggregate SHA-256 before and after this
ticket was `0be4cfb92e4ac5a71f56900dcceaf9f27f9ae6f42d71ff6afa44783fdd7cb009`.

RED fixture checks demonstrated that the old A/B verifier accepted 1,024 duplicate checksum rows,
silently overwrote a duplicate snapshot metric, and that the p016 verifier accepted a command when
`command.json` and the run manifest drifted together. The existing p016 RSS classifier and artifact
hash comparison were already fail closed and were retained as regression behavior.

GREEN verification now covers:

- non-improving RSS classification;
- exact and unique TaskTrace file sets plus every individual file hash;
- snapshot schema version, unique numeric metric identities, required metrics, and count invariants;
- independently reconstructed commands and cross-artifact binary identity;
- present worktree HEADs and hashes of present build artifacts;
- explicit `unavailable` provenance when a recorded worktree or artifact no longer exists.

Both existing packages pass. The A/B package reports `p001` memory as `mismatched`, `p004` memory
as `matched`, and preserves the false formal preregistered memory gate. The p016 package reports
`matched`. Both currently report provenance as `verified`.

Reviewer follow-up added full per-run artifact-contract enforcement, exit-code/manifest and sampled
RSS/manifest cross-checks, exact snapshot row widths, and worktree-relative A/B build paths. Each
analyzer now has complete package-fixture tests for missing artifacts, an extra snapshot data column,
joint command drift, malformed trace and snapshot data, and sampled-RSS drift. The original run
manifests did not hash stdout/stderr, so analyzers verify their semantic completeness but report
byte-level integrity as `unavailable`; no post-run hash was invented.
