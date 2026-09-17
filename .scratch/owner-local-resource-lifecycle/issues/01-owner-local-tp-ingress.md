# 01 — Resolve live, closing, retired, and invalid TP ingress owner-locally

**What to build:** Make packet ingress resolve an exact TP endpoint entirely from the target node's
owner-local reservation and endpoint state. Active and closing endpoints remain usable, exact
retired keys are recognized as stale through lightweight quarantine, and unknown or mismatched keys
preserve fatal validation. Retired TPNs cannot be allocated again during the simulation.

**Blocked by:** None — can start immediately.

**Status:** complete

- [x] Production ingress returns the semantic outcomes `LIVE`, `RETIRED_EXACT`, and `INVALID`
      without taking the process-global connection-control lock.
- [x] Active and closing endpoint views both resolve exact ingress to the existing transport path.
      Ticket 02 owns production packet completion while the close fence is active.
- [x] Final endpoint retirement atomically replaces the live view with quarantine containing the
      full wire-visible connection key.
- [x] An exact retired key follows the defined stale-packet behavior while an unknown or mismatched
      key remains a fatal configuration or protocol error.
- [x] TPN allocation deterministically refuses live, reserved, or quarantined identifiers and fails
      before wraparound reuse.
- [x] Existing packet lifecycle bookkeeping remains temporarily connected so this ticket isolates
      owner-local ingress and identity behavior.
- [x] Focused local delayed-ingress and MTP owner-readiness characterization passes without changing
      packet parsing, ACK, retransmission, routing, queueing, or congestion-control behavior. Real
      cross-owner delayed-destroy timing remains Ticket 02 acceptance.
- [x] The ticket's frozen canonical outputs remain byte-identical to baseline.
