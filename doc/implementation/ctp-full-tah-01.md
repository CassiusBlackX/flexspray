# CTP full TA ticket 01: shared codec and RTP characterization

Implementation: `85667a83cf823fb4bda0fc306913f7d937985906`. Pre-refactor baseline:
`a4e7025844dd400cf2c0644699de1b6950489377`, branch `feat/ub-2.1-ddie`.
Initial user CONTEXT/ADR changes and historical repro evidence were preserved.

The existing full RTP transaction extension builder, header-size calculation and two-stage
parser now use `ub-ta-codec`. Carriers retain payload, tags, outer headers and request lookup.
The decoder bounds basic headers, full MAETAH and variable BEETAH before deserialization,
and exposes the complete ATAH to the RTP caller. Valid RTP identity/completion behavior
and CTP/LDST paths are unchanged. No requester/progress repair is claimed by this ticket.

## Validation

Current-session release build: unified-bus,iodie,ddie; tests ON; examples/MPI/MTP OFF.
One build/test process at a time, build `-j 2`. Full configured build passed:
`python3.12 ./ns3 build -j 2`.

Each following suite passed through
`python3.12 ./ns3 run --no-build 'test-runner --test-name=NAME --verbose'`:

- unified-bus-transaction-header-codecs
- unified-bus-transaction-rules
- unified-bus-wire-format
- unified-bus-transaction-urma
- unified-bus-atomic
- unified-bus-transaction-extensions
- unified-bus-ldst-cfg

New coverage preserves status/RSPINFO/SV/version/poison/RCID, checks truncated basic,
memory and variable BE headers, and exercises the production RTP decoder with truncated
planned headers. Eight pre-extraction RTP TA/payload byte witnesses are frozen in the
existing wire-format fixture. Existing real request/response and completion tests remain.

Original input copies of `scratch/2nodes_single-tp` and `scratch/2nodes_atomic_ctp` ran with
`--test=1`: runner PASSED, task IDs 0–3 and 0–24 exactly once, phase dependencies checked.
Parser rows and first-send/last-response timestamps were verified. Each case's 35 raw trace
files are byte-identical before/after extraction. Original inputs and old outputs were not overwritten.

Evidence root: `.scratch/ctp-full-tah/execution/01/`; exact commands/exits in
`m0/commands.jsonl` and `m1/commands.jsonl`, byte witness in `m0/rtp-wire-bytes.json`,
case comparison in `m1/comparison.json`, independent review in `review.md`.
The initial new-API test compile failed as expected; a subsequent link error at the
migrated caller was fixed by retaining the original enum opcode overload before acceptance.

The current-session `ddie` baseline reproduces DdieStarsWaitTest's shared Entity/TASSN
stall (`m0/ddie.log`); ticket 04 must repair it. MPI/MTP were not compiled or run.
Repository-wide test.py is reserved for final integration; this record claims the seven suites above.

## Review

A separate root-agent standards/specification pass against the pre-refactor code found no
blocking defect. It checked branches/errors, mutation/header/tag order, callback/event timing,
ACK/queue/CC behavior, reuse and lifecycle balance. No new modeled resource or event exists.
This closeout completes ticket 01; ticket 02 may proceed.

## Knowledge Sources

- `doc/adr/0024-unify-target-transaction-execution.md`: shared executor and rule-owned layout.
- `doc/adr/0026-extend-transaction-rules-with-response-length-policy.md`: request-dependent response layout.
- `doc/adr/0031-use-full-transaction-headers-for-cfg6-ctp.md`: approved shared full codec boundary.
- `.scratch/ctp-full-tah/design.md`: A01 and M0/M1 contracts.
- `dev-knowledge/specification/` is absent in this worktree; no claim of reading that source.
  This extraction uses the existing production Header codecs and approved design.
