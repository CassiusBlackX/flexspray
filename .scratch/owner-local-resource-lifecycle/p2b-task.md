# P2b implementation ledger

Status: closed as rejected prototype evidence

## Live findings

- The targeted build passes.
- The original delayed-destroy case still reaches node 0 at `136.720 ns`.
- A tombstone alone cannot handle this race because the endpoint is still `DRAINING`; connection
  finalization and view erasure have not completed.
- The experiment that classified a closing key as stale prevented the reservation fatal, but the
  destroy callback remained pending because accepted transaction/transport work could no longer
  make protocol progress. Production `DRAINING` endpoints must instead continue processing valid
  traffic while participants cooperatively withdraw.
- A forced `1 ms` checkpoint reports `initial=PENDING`, zero callbacks, then crashes during forced
  shutdown. It is not passing evidence.

The production direction is defined by `spec.md`: components stop creating new work, accepted work
continues normally, all participants withdraw, and only then does atomic retirement install the
quarantine record. A tombstone and no-reuse rule remain necessary after finalization but cannot
replace drain behavior.

## Batch Reuse Map

Pre-refactor commit: `68bbce1310dad539a0c053cac6cd14ac69dda5cb`

Semantic batch: owner-local retired TP reservation and late-packet classification

Old active symbols:

- `TpConnectionManager::EraseConnectionView`
- `TpConnectionManager::BuildIndexesForNode`
- `TpConnectionManager::ReserveNextTpnLocked`
- `UbTransportChannelManager::ResolveInboundTp`
- `UbSwitch::SinkTpDataPacket`

Old call paths:

- explicit connection destroy -> endpoint drain -> connection view erase
- TP packet sink -> inbound TP resolution -> receive or fatal reservation validation

Continuous code blocks reused: existing reservation lookup and exact wire-key validation

Reuse mode: retain the active lookup and validation; add a minimal retired lookup after active
resolution fails.

Required lifecycle adaptations:

- retain a backing-free owner-local retired reservation after view erase;
- do not return a retired TPN to the allocator;
- classify an exact retired wire key as a stale packet drop;
- keep mismatched and unknown keys fatal.

Preserved ordering and observable behavior:

- endpoint backing is destroyed before connection finalization;
- destroy callbacks and connection-control ordering are unchanged;
- ACK, retransmission, routing, queue, congestion-control, and transaction code are unchanged;
- active packet delivery and canonical task output are unchanged.

Existing characterization: P2 normal local/MTP/MPI/hybrid canaries passed; delayed destroy aborts
after reservation erasure in local and MTP.

Explicit behavior changes: an exact packet for a retired reservation is consumed as a typed stale
drop instead of aborting; retired TPNs are not reused during the simulation.

Excluded paths: packet accounting removal, dormant backing removal, protocol algorithm changes,
wire-visible generation, bounded-time quarantine.

Focused validation:

- targeted `ub-quick-example` build;
- delayed-destroy local, MTP, MPI, and hybrid probes;
- normal canonical comparison in all four execution modes;
- confirm no TPN reuse and no dormant rehydration in the probe.

Re-approval triggers: any required change to ACK/RTO/routing/queue/CC/transaction completion, public
wire format, or cross-owner mutable lookup.
