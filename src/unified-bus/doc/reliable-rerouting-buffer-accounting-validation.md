# Reliable rerouting storage and admission validation

The implementation follows ADR 0029. This record distinguishes completed slices from remaining
work; local commands/logs live under `.scratch/ub21-reliable-rerouting-buffer-accounting/evidence/`.
All builds and tests run sequentially on `ub-2.1-dev`, release profile, tests enabled, MPI/MTP off.

## Ticket 01: original packet retention

Baseline: `f992c11de0710ecbab51caa82e2ab31eecc799bc`.
Implementation commit: `f702c63c`.

Normal armed transmission retains the original `Packet` until complete link delivery. Physical
failure transfers all unreceived originals to retry; local detection closes the send set. Disabled
and host access endpoints skip the bookkeeping. The existing permanent-failure delivery gate
invalidates old events before recovery reuses these objects. Port disposal releases the retention
owner, including incomplete work at an early stop.

Executed:

- `python3.12 ./ns3 build -j 4`: PASS, baseline and implementation (all configured targets).
- `build/utils/ns3.44-test-runner --suite=unified-bus-reliable-rerouting --verbose`: PASS.
- `build/utils/ns3.44-test-runner --suite=unified-bus-link-fault-input --verbose`: PASS.
- Isolated baseline and implementation small-case runs via
  `python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=<isolated-case>'`: PASS.
- `python3.12 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py <isolated-case>`:
  PASS, 382 transfers, both directions, four completed tasks, actual LastAck paths verified.

Red evidence: the new healthy propagation assertion found four retry entries where the contract
requires zero. Green tests additionally compare original object pointers at destination, cover four
simultaneously propagating packets, recovery before the old delivery deadlines, and an already
delivered packet still occupying the sending port's long IFG. Existing sequence-wrap, bilateral,
error/timeout, duplicate LastAck and same-timestamp event fixtures remain active. An early-stop
assertion verifies retained originals lose all owner/event references at simulator destruction.

Evidence: `evidence/01/{baseline-build,baseline-reliable-rerouting,baseline-link-fault-input,
baseline-case,baseline-check,red,green-build,final-build,green-reliable-rerouting,
green-link-fault-input,implemented-case,implemented-check}.log`; isolated inputs/outputs under
`evidence/01/{baseline,implemented}/2nodes_reliable-rerouting/`.

Review: separate read-only standards/spec reviews against the pinned baseline. The spec review
requested positive-delay CBFC exhaustion evidence; the added production fixture passes, observing
retry growth from one to two, uncredited VOQ retention, detection cutoff and late delivery suppression.
Both axes have zero outstanding findings. The final run is `evidence/01/review-tests.log`;
`review-build.log` records its current-source build. No performance claim is inferred from these runs.
Ticket 02 will change ingress accounting; this ticket deliberately uses the existing generous
small-case capacity. Tickets 03/04 cover migration order and occupancy output.

## Knowledge Sources

- `doc/adr/0029-retain-original-packets-for-failure-recovery.md`: approved ownership/accounting contract.
- `doc/adr/0027-use-packet-boundaries-for-routed-lastack.md`: complete-packet receipt and real LastAck.
- `doc/adr/0021-model-switch-allocation-as-wakeup-snapshot.md`: preserve allocation transactions.
- `dev-knowledge/specification/`: UB specification baseline; this change remains packet-level and
  does not model flit retry memory, finite SRAM, or normal on-wire link ACKs.
