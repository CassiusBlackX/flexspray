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
