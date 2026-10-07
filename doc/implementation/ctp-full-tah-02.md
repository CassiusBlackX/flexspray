# CTP full TA ticket 02: requester preparation and admission

Implementation: `e8eb31e6`. Baseline: `2ca1645bd86dc88d6f21ef26ebd412bb52bef481`.
UbTransaction now owns a monotonic node-local 20-bit RCID namespace, independent of caller
Jetty IDs. Exact Jetty generation associations survive idle unprepare/reprepare and retire on
final destruction without reuse. Keyed preparation returns structured failure reasons and checks
lifecycle, binding and actual ACK capacity before allocating/publishing. Unprepared direct WQE
submission rejects before lifecycle/sequence/events. UbApp prepares before submission; Iodie
maps exhaustion to IdSpaceExhausted and cleans only a newly created failed Jetty.

## Validation

Release, unified-bus/iodie/ddie, tests ON, MPI/MTP OFF. Full configured build passed:
`python3.12 ./ns3 build -j 2`.
Each following suite passed using
`python3.12 ./ns3 run --no-build 'test-runner --test-name=NAME --verbose'`:

- unified-bus-ctp-transaction
- unified-bus-ctp-lifecycle
- unified-bus-ctp-input
- unified-bus-ctp-endpoint
- iodie-urma
- unified-bus-examples (including original scratch entry)
- unified-bus-atomic
- unified-bus-transaction-extensions

The requester test verifies distinct 24-bit caller IDs sharing low20 bits; usable RCID0; maximum
RCID and exhaustion without wrap; preparation failures without ID/binding/WQE/event mutation;
idempotence, unprepare/reprepare, destruction/recreation and existing association after exhaustion.
The Iodie roundtrip verifies a rejected new requester leaves an accepted requester able to complete
and submit again, exactly once per WQE. Existing lifecycle and preparation tests retain assertions.

Fresh original single-TP and atomic CTP input copies passed with `--test=1`: task IDs 0–3 and
0–24 exactly once, phase dependencies and first-send/last-response parser timestamps checked.
No original workload/CLI changes. Evidence root `.scratch/ctp-full-tah/execution/02/`, exact
commands/exits/revision in focused/, acceptance/, atomic-rerun/ and case-acceptance/commands.jsonl;
case-evidence.json contains task/parser checks. Expected API-red compile and subsequent compile
correction are retained. Extra atomic suite initially exposed a fixture migration selecting the CTP
Jetty for its final RTP-positive block; fixed the fixture and reran the whole atomic suite successfully.
No historical run is counted as this ticket's PASS. Generated outputs are excluded from commits.

## Review and limits

Independent root-agent standards/spec review against the baseline: no blocking production finding.
Checks covered ordering, existing helper/owner reuse, rejection balance, finalization-before-callback,
and unchanged RTP/LDST/header/ACK behavior. Review evidence: execution/02/review.md and review.diff.
CTP wire format and shared Entity progress are intentionally still pending tickets 03/04.
MPI/MTP are SKIP; repository-wide test.py remains final-integration work.

## Knowledge Sources

- `doc/adr/0020-use-owner-local-explicit-resource-lifecycles.md`: exact owner retirement.
- `doc/adr/0028-own-ctp-requester-context-identifiers-in-transaction.md`: RCID allocation contract.
- `.scratch/ctp-full-tah/design.md`: D01/D02 and A02/A03.
- `dev-knowledge/specification/` is absent locally; approved design and production lifecycle APIs
  are the implementation sources, with no claim of reading absent specification files.
