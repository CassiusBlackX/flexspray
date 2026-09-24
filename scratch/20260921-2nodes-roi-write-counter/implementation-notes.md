# Implementation and acceptance record

## Ticket 01 — complete

Implementation commit: `581be40d` (baseline `a9ebda9b`). Existing `CONTEXT.md`
changes were preserved and excluded. Production modules and interfaces are unchanged.

Validation on 2026-09-21:

- Manifest release configure: exit 0.
- `python3.12 ./ns3 build -j 2 ub-quick-example`: exit 0.
- Runtime `--PrintTypeIds`, `--PrintUbGlobals` and 41 `--ClassName` queries: exit 0;
  local catalog in `output/runtime-catalog/`.
- `python3.12 ./ns3 build -j 2 ub-roi-write-counter`: exit 0.
- README control command, three recorded attempts: all exit 0. Earlier evidence
  is retained in `cases/control/archives/` with the retry association in run records.
- Final control: B ready 3,030,800 ps; A post completion 4,042,040 ps; counter 64;
  two executions/completions; one data packet; one ready. Data TAACK receive
  2,019,080 ps precedes SO post send 2,029,080 ps. Request wire order is RO then SO.
- External CSV checks: consecutive sequence numbers, nondecreasing integer timestamps,
  actual decoded post=64, causal ordering and cleanup marker passed.
- `git diff --cached --check`: passed.

Separate review against baseline checked normal/error paths, callback lifetime,
protocol reuse, response timing and evidence. Standards found one pointer-invalidation
risk on diagnostic append (fixed using stable deque storage). Spec found missing
source identity (fixed with source/binary hashes in recorded runs and parameter
snapshots). No outstanding findings. Treatment was not claimed tested in this ticket.

The 1 ms watchdog exceeds the discovered 25.6 us BaseRTO; retransmission is disabled
by the unchanged runtime default. Normal cleanup completes after simulation stops.
The catalog and raw runs are local ignored evidence; the ticket ledger remains in
`.scratch/roi-write-counter/` as required by the local tracker convention.

## Ticket 02 — complete

Implementation commit: `66e5196d`; baseline `79f45f00`.

- Target build and independent control/treatment processes: exit 0.
- Control remains B=3,030,800 ps and A=4,042,040 ps.
- Treatment B=1,008,320 ps and A=2,019,560 ps, one execution/completion,
  one 166-byte modeled data packet, decoded notify address 0x2000 and value 64.
- Treatment event sequence 6→7→8→9 is data commit → decoded notify → counter
  update → ready, all at the same measured time. Addition uses the received field.
- External checks passed: packet opcode 25, wire RO, input address/value, event
  counts, causal order and complete parameter equality except variant.
- Standards and Spec review passes against the baseline found no defects.
- `git diff --cached --check` passed. Production modules are unchanged.

Difference 2,022,480 ps is an observation here, not the final attribution.
All run attempts retain source/binary SHA-256 and earlier control evidence is archived.

## Ticket 03 — complete

Implementation commit: `80bfb9ce`; baseline `2a2fd147`.

- Approved plan, fixed two-row matrix, manifest, per-case specs and complete run
  ledger are materialized in this package. The bounded recorder captures exactly
  the listed simulator argv and preserves previous evidence before reruns.
- Final independent control/treatment processes: exit 0, all contracted artifacts
  present. Actual UTC, HEAD/dirty, source/binary identity, archives and failure
  categories are recorded in `run-ledger.md` and each `run-record.json`.
- Manifest comparison: exit 0. Both cases valid; hypothesis matched. Control B/A
  3,030,800 / 4,042,040 ps; treatment B/A 1,008,320 / 2,019,560 ps.
- ΔB=2,022,480 ps (66.730896%). Propagation RTT=2,000,000 ps. Residual 22,480 ps
  consists of 2,480 ps serialization difference and 20,000 ps measured waiting
  difference. No unexplained residual at the recorded boundaries.
- Comparator's eight evidence self-tests passed: early-ready, overshoot, missing
  event, parameter mismatch, summary mismatch, noninteger time, missing case,
  and valid/no-speedup classified mismatched. Review also checked negative benefit.
- Recorder test passed both truncated-output subcases (child return -9 and 0),
  preserving actual process status, archives and history. These are mocked failure
  checks, not added network cases.
- Python bytecode compilation, YAML/manifest consistency, artifact SHA-256 checks,
  driver source identity, all package/report Markdown links, and missing-case CLI
  diagnostics (exit 1 while retaining both planned rows) passed.
- Separate Standards/Spec review: recorder process-result persistence P2 fixed and
  confirmed by reviewer; no outstanding findings. Prior comparator formatting
  failures and their fixes remain visible in the run ledger.
- C++ was unchanged in ticket03. Ticket02's successful targeted build is the final
  relevant build; no repository-wide tests were run for these scratch-only changes.
- `git diff --cached --check` passed. The pre-existing `CONTEXT.md` edit is preserved.

Final reports: [comparison.md](output/comparison.md) and
[comparison.json](output/comparison.json). Raw evidence, build logs, runtime catalog,
archives and test logs remain local ignored outputs; commands and closeout records
are versioned. The report limits the claim to this deterministic single-packet model.
