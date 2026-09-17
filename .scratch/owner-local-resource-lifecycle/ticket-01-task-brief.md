# Ticket 01 implementation ledger

Status: complete

## Acceptance suite

- Targeted `ub-quick-example` build with at most eight build jobs.
- Focused UnifiedBus tests for inbound reservation resolution, explicit connection destroy, closing
  ingress, retired-key classification, mismatched-key rejection, and TPN quarantine.
- Frozen local and MTP canonical output comparison against
  `68bbce1310dad539a0c053cac6cd14ac69dda5cb` for identical inputs and runtime configuration.
- MPI and hybrid byte acceptance remains a feature milestone after the cross-owner close ticket; no
  unrun mode is reported as passing in Ticket 01.

## Batch Reuse Map

Pre-refactor implementation commit: `349a3d16a4c4c71e9256eec1cca05482e857fec9`

Frozen canonical baseline commit: `68bbce1310dad539a0c053cac6cd14ac69dda5cb`

Semantic batch: owner-local TP ingress and final-retirement quarantine.

Old active symbols:

- `TpConnectionManager::TryGetLocalConnection`
- `TpConnectionManager::BuildIndexesForNode`
- `TpConnectionManager::EraseConnectionView`
- `TpConnectionManager::ReserveNextTpnLocked`
- `UbTransportChannelManager::ResolveInboundTp`
- `UbSwitch::SinkTpDataPacket`

Old call paths:

- TP packet sink -> inbound TP resolution -> receive or fatal key validation.
- Explicit connection close -> both endpoint destroy -> connection view erase -> callback.
- TPN allocation -> live/reserved set scan -> next identifier.

Continuous code blocks reused: existing directed `Connection` view, exact wire-key comparison, TP
packet parser, receive dispatch, and connection finalization ordering.

Reuse mode: retain active lookup and packet dispatch; add a small owner-local retired-view map and a
typed ingress result around the existing lookup.

Required lifecycle adaptations:

- A closing materialized endpoint remains a valid exact ingress target.
- Final view erasure atomically installs a backing-free full-key quarantine entry.
- Exact quarantine match is stale; unknown or mismatched key remains invalid.
- Retired TPNs are never returned to the allocator during the run.

Preserved ordering and observable behavior:

- Packet parsing, header removal, ACK/data dispatch, routing, queueing, retransmission, congestion
  control, and ordinary terminal accounting are unchanged.
- Existing packet lifecycle bookkeeping remains connected for this ticket.
- Connection callbacks remain after both endpoint retirements and both view replacements.

Existing characterization: normal RTP reservation tests and explicit paired destroy tests; the P2
delayed-destroy prototype exposed early view erase and immediate TPN reuse.

Explicit behavior changes: closing endpoints process exact valid packets; exact retired keys are
classified stale; retired TPNs are quarantined for the rest of the run.

Excluded paths: packet bookkeeping removal, dormancy removal, Jetty/TPG migration, ACK/RTO changes,
wire-visible generation, timeout quarantine, and convenience destroy policy.

Focused validation: targeted build, focused tests above, then frozen local/MTP byte comparison.

Re-approval triggers: any required packet-format, ACK/RTO, routing, queue, congestion-control,
transaction-completion, or public lifetime-interface change.

## Current evidence

- Local configuration (`NS3_MTP=OFF`) built `unified-bus-test` successfully with `ninja -j8`.
- `unified-bus` QUICK suite passed.
- `unified-bus-tp-reservation` QUICK suite passed, including production mismatch aborts, closing
  ingress, exact retired-key stale handling, and TPN non-reuse.
- The worktree was then configured release with MPI off, MTP on, and tests on. Targeted
  `ub-quick-example`, `unified-bus-test`, and `test-runner` builds passed.
- The MTP-enabled `unified-bus-tp-reservation` QUICK suite passed, including
  `TP resolution treats MTP ownership readiness fail closed`.
- Frozen local and MTP canaries are raw-byte-identical to their respective baseline outputs
  (`cmp=0`). Candidate SHA-256 values are
  `10f54f22...b9dd64` for local and `72a03b33...579f6` for MTP.
- Canonical candidate artifacts and copied inputs are retained under
  `/tmp/ns3ub-ticket01.pSZFM9`.
- One suite invocation launched immediately after a build exited with `SIGSEGV`. The same focused
  suite subsequently passed in both normal and MTP-enabled configurations, and the failure has not
  reproduced. This transient remains visible rather than being classified as a pass.
- A dormant endpoint cannot remain observably draining through the ordinary destroy path:
  `DestroyTp()` synchronously finalizes an already-dormant record. The ingress state that must stay
  usable is therefore the materialized `DRAINING` endpoint. Ticket 04 remains responsible for
  deleting dormant snapshots and rehydration.
- A true delayed-destroy run spanning multiple MTP execution owners remains Ticket 02 acceptance;
  Ticket 01 covers the owner-readiness boundary under an MTP build and production stale ingress
  after retirement without changing the cross-owner close protocol.
- Review follow-up showed that injecting a synthetic WRITE or ACK without an accepted protocol
  lineage into the drain-only endpoint and then running scheduled close work can terminate with
  `SIGSEGV`. Such packets are not a valid oracle for already-accepted work. Ticket 02 must build its
  delayed-destroy fixture from a real admitted operation and validate packet completion through the
  production switch path; Ticket 01 is limited to exact closing-key resolution.
- Review P1 closed: `ResolveInboundTp()` now validates the current execution owner before reading
  `m_endpoints`. The ordinary LIVE/DORMANT path reuses an owner-validated internal resolver, so the
  hot path does not repeat the MTP owner gate.
- Review P2 closed: the TPN boundary fixture covers `0xFFFFFE`, free `0xFFFFFF`, occupied
  `0xFFFFFF`, and allocation after the final value; both failure paths require `SIGABRT`.
- Final MTP-enabled targeted builds passed for `unified-bus-test` and `test-runner`.
- Final `unified-bus-tp-reservation --fullness=QUICK` and `unified-bus --fullness=QUICK` suites
  passed. Expected child-process fatal validation output remains part of both suites.
- Final local and MTP canary replays remain raw-byte-identical (`cmp=0`) to the frozen ticket
  outputs. SHA-256 values remain `10f54f22c7b3f8e935b08c87eb9ed1e8d8e6384fc8ece55c8b77b5a6e0b9dd64`
  and `72a03b332b4fb144ad1a3ccc03d454f1c7e506ceafcb6274360b5ca96b7579f6`.
- Final `git diff --check` passed.
