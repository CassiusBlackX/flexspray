# Ticket 01 validation — 2026-09-24

Implementation: `3e5a9c2a`; tools baseline: `0fec3ae`.

- Fresh release build with unified-bus tests enabled, `python3.12 ./ns3 build -j 4 test-runner ub-quick-example`: PASS.
- `build/utils/ns3.44-test-runner --suite=unified-bus-reliable-rerouting --verbose`: PASS, 34 tests.
- `build/utils/ns3.44-test-runner --suite=unified-bus-link-fault-input --verbose`: PASS, 3 tests.
- Tools `python3 -m unittest discover -s trace_analysis -p 'test_*.py'`: PASS, 10 tests.
- `ub_tools.py case check scratch/reliable-rerouting/2nodes_reliable-rerouting --json`: PASS.
- Run and checker commands in README: PASS, four completed 2 MiB tasks, 382 transfers, both failed endpoints covered.
- Checker `--self-test`: PASS, 12 rejecting mutations.
- Independent Standards and Spec review: no remaining blocking findings.

Earlier failures remain in local tracker logs, including old tools silently ignoring CSV traces,
a malformed route input fixed before successful execution, and a default-capacity 65,537-packet
run exposing the already scheduled ticket 02 retry wakeup defect. The large boundary test uses
16 MiB reserve to test sequence anchoring separately. Capacity/drain closure and Clos detours
are not claimed complete by this ticket. No full repository test run was claimed.

Knowledge sources: ADR 0027 defines the accepted packet-level scope;
`dev-knowledge/specification/` supplies the protocol baseline. UB 2.1 section 5.3.9
and its packet-boundary interpretation are recorded in the approved local feature design.

# Ticket 02 validation — 2026-09-24

The same serial build and execution commands above passed after the capacity/drain fix:
35 reliable-rerouting tests, 3 link-fault tests, four-task small case and 13 checker mutations.
The 65,537-unconfirmed-attempt regression now uses default capacity and completes all packets.
Explicit retry-only and cross-ingress shared-pool tests observe a real block, exactly one
route resolution and transfer at capacity release, with bounded event count. Delayed processing
checks the first DRAINED timestamp. Existing CBFC/PFC credit/disposal cases also pass.
Independent Spec and Standards review found no blocking defects.
The CSV evidence now includes `processing_packets`; nonzero processing rejects a drain claim.

# Ticket 03 validation — 2026-09-24

Implementation `67602a43`. Reliable-rerouting suite PASS37. Production three-leaf/two-spine
fixture records 18 LastAck serializations (six copies over three hops) and bidirectional delivery
of retained and new data. Table, index and rule routes preserve endpoint/allow-list/ingress
restrictions; the ordinary shortest API still rejects non-shortest fallback.
Routing, routing-modes and structured-routing suites PASS; final routing-mode and structured
runs were serial. A prior orchestration error briefly overlapped suites; it was recorded locally
and corrected before final evidence. Original small case, 13 checker mutations and CSV route
preflight PASS. Preparation validator explicitly rejects unsupported routing/workload formats.
Independent Standards/Spec findings were resolved. See ADR0028 for the API and topology boundary.
