# 05 - Remove packet bookkeeping and obsolete global lifecycle machinery

**What to build:** Remove simulator-global lifecycle work from every packet family and every normal,
drop, fault, malformed, consume, and response path, then delete coordinator machinery that has no
remaining resource-correctness user.

**Blocked by:** 04 - Remove TP dormancy and automatic backing reclaim.

**Status:** ready-for-agent

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

- [ ] RTP data, ACK, CNP, retransmission, and response packets are not registered with a global
      lifecycle owner and carry no lifecycle packet tag.
- [ ] CTP and LDST data and response packets carry no global lifecycle ownership record.
- [ ] Links and ports do not report packet transfers to a lifecycle coordinator.
- [ ] Switch, port, transport, fault, malformed, route-failure, consume, and drop paths do not perform
      packet-terminal lifecycle resolution.
- [ ] Packet-attempt bitmaps and global in-flight packet counts are absent from product behavior.
- [ ] MTP and MPI send no packet-terminal control record back to a packet origin.
- [ ] Passive non-lifecycle packet logs remain local observations and do not gate packet handling or
      destroy; removed lifecycle trace schemas are not compatibility contracts.
- [ ] Obsolete lifecycle coordinator packet state, packet control operations, callbacks, storage
      accounting, and trace schema are removed once no product caller remains.
- [ ] Stable connection, endpoint, Jetty, and TPG identities remain owner-local where required for
      modeled generation and explicit close correctness.
- [ ] Local, MTP, MPI, and hybrid canonical outputs are raw-byte-identical to the frozen baseline.
- [ ] The approximately 30-second smoke measurement confirms the full removal improves retired
      instructions after correctness acceptance passes.
