# 05 - Remove packet bookkeeping and obsolete global lifecycle machinery

**What to build:** Remove simulator-global lifecycle work from every packet family and every normal,
drop, fault, malformed, consume, and response path, then delete coordinator machinery that has no
remaining resource-correctness user.

**Blocked by:** 04 - Remove TP dormancy and automatic backing reclaim.

**Status:** completed

## Batch reuse map

Pre-refactor commit: `82b2295`

### Batch 1 - packet path disconnection

- Old active symbols: `RegisterPacketAttempt`, `ObservePacketTransfer`, `TerminatePacket`, and
  per-family `RegisterPacketBirth` wrappers.
- Old call paths: RTP data/ACK/CNP/response construction; CTP and LDST construction; datalink
  control construction; queue, link, port, switch, parser, fault, route-failure, and consume paths.
- Reuse mode: retain each existing packet builder, parser, queue, link, ACK/RTO, fault, and logging
  block unchanged; delete only lifecycle calls and packet-only owner wrappers.
- Preserved behavior: packet bytes, tags unrelated to lifecycle, scheduling, queue decisions,
  protocol state transitions, drop counters/logs, ACK/RTO, routing, and trace callbacks.
- Excluded paths: entity identities, leases, tracked events, explicit close, connection owner routing,
  generation validation, transaction correlation, flow tags, and packet trace tags.
- Focused validation: targeted build, RTP/CTP/LDST/allocator suites, and main QUICK suite.

### Batch 2 - packet ledger and cross-owner control removal

- Old active symbols: `UbPacketLifecycleTag`, `UbPacketAttemptIdentity`, attempt bitmaps, packet
  counters, packet terminal/transfer control records, and packet lifecycle trace schema.
- Reuse mode: retain coordinator resource identity, lease, tracked-event, work-terminal, and
  connection-control machinery; delete packet-only state and branches.
- Preserved behavior: resource open/retire validation, generation history, lease balance, state
  callbacks, MTP/MPI connection control ordering, and resource/work lifecycle traces.
- Focused validation: lifecycle storage/control tests rewritten around remaining entity semantics,
  then targeted build and focused suites.

### Batch 3 - obsolete tests, examples, and diagnostics

- Remove packet-ledger-only tests/examples and packet-only runtime/trace assertions.
- Preserve local packet logging, flow/trace/correlation tag tests, explicit close tests, and all
  canonical network-output fixtures.
- Focused validation: local and MTP canonical byte comparison plus MPI-only and hybrid close harness.

- [x] RTP data, ACK, CNP, retransmission, and response packets are not registered with a global
      lifecycle owner and carry no lifecycle packet tag.
- [x] CTP and LDST data and response packets carry no global lifecycle ownership record.
- [x] Links and ports do not report packet transfers to a lifecycle coordinator.
- [x] Switch, port, transport, fault, malformed, route-failure, consume, and drop paths do not perform
      packet-terminal lifecycle resolution.
- [x] Packet-attempt bitmaps and global in-flight packet counts are absent from product behavior.
- [x] MTP and MPI send no packet-terminal control record back to a packet origin.
- [x] Passive non-lifecycle packet logs remain local observations and do not gate packet handling or
      destroy; removed lifecycle trace schemas are not compatibility contracts.
- [x] Obsolete lifecycle coordinator packet state, packet control operations, callbacks, storage
      accounting, and trace schema are removed once no product caller remains.
- [x] Stable connection, endpoint, Jetty, and TPG identities remain owner-local where required for
      modeled generation and explicit close correctness.
- [x] Local, MTP, MPI, and hybrid canonical outputs are raw-byte-identical to the frozen baseline.
- [x] The approximately 30-second smoke measurement confirms the full removal improves retired
      instructions after correctness acceptance passes.

## Completion evidence

- Implementation commit: `17a669d refactor(unified-bus): remove packet lifecycle bookkeeping`.
- `unified-bus-lifecycle`, `unified-bus-tp-reservation`, and main `unified-bus` QUICK suites pass.
- The registered MPI system suite runs the explicit connection-close harness in both MPI-only and
  MPI+MTP hybrid modes; both ranks report `PASSED` in both cases.
- Local canonical output matches the frozen baseline byte-for-byte, SHA-256
  `10f54f22c7b3f8e935b08c87eb9ed1e8d8e6384fc8ece55c8b77b5a6e0b9dd64`.
- MTP-only canonical output matches the frozen baseline byte-for-byte, SHA-256
  `72a03b332b4fb144ad1a3ccc03d454f1c7e506ceafcb6274360b5ca96b7579f6`.
- The reversed-order 16-flow, 768 MiB-per-flow comparison produced byte-identical canonical output
  in all four runs, SHA-256
  `c6c81f0df395ad903fefea44172d73370ac0af08f41d830eeeaa8a33d64f8acb`.
- Treatment retired instructions improved by `3.890%` and `3.869%` in the two ordered pairs;
  combined mean improvement was `3.879%`. Combined mean cycles improved by `5.067%`, wall time by
  `6.860%`, and user time by `5.989%`.
- Performance logs and canonical artifacts: `/tmp/ticket05-perf.sRxfv5`.
- Review follow-up removed stale references to deleted packet-control examples, registered the live
  connection-close harness for MPI-only and hybrid execution, and restored a non-lifecycle oracle
  for detached channel failures.
