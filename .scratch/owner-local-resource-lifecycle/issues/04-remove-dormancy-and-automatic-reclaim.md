# 04 - Remove TP dormancy and automatic backing reclaim

**What to build:** Delete simulator-only TP backing suspension before removing the packet-idle gate
that currently authorizes it. TP endpoints remain materialized until explicit owner-local close.

**Blocked by:** 02 - Complete explicit TP connection close across execution owners; 03 - Migrate
Jetty, RTP TPG, CTP, and LDST lifecycle dependencies.

**Status:** completed

- [x] TP endpoints are either active, closing, or retired; no endpoint backing is suspended and later
      rehydrated solely for host-memory policy.
- [x] Automatic backing-reclaim controls, reclaim scans, dormant snapshots, rehydrate entry points,
      and compact-backing diagnostics are removed.
- [x] Allocator, transaction, switch, congestion-control, and controller paths retain live endpoint
      backing and do not attempt automatic reclaim.
- [x] Stable connection and endpoint identities remain available for explicit close correctness.
- [x] No idle, timeout, RSS, memory-pressure, reference-count, or empty-container reclamation policy
      remains.
- [x] Packet lifecycle bookkeeping remains temporarily connected until Ticket 05.
- [x] Focused transport, allocator, transaction, and storage tests pass.
- [x] Local and MTP canonical outputs remain raw-byte-identical to baseline; the existing MPI-only
      and hybrid explicit-close harnesses pass on both ranks.

## Completion evidence

- Implementation commit: `db817e6 refactor(unified-bus): remove TP backing dormancy`.
- `unified-bus-lifecycle`, `unified-bus-tp-reservation`, and main `unified-bus` QUICK suites pass.
- Local canonical output matches the frozen baseline byte-for-byte, SHA-256
  `10f54f22c7b3f8e935b08c87eb9ed1e8d8e6384fc8ece55c8b77b5a6e0b9dd64`.
- MTP-only canonical output matches the frozen baseline byte-for-byte, SHA-256
  `72a03b332b4fb144ad1a3ccc03d454f1c7e506ceafcb6274360b5ca96b7579f6`.
- MPI-only and MPI+MTP hybrid explicit connection-close harnesses pass on both ranks.
- Final MPI/hybrid logs: `/tmp/ticket04-mpi.MqIbAZ`.
- Final local/MTP canonical artifacts: `/tmp/ticket04-canonical.NfbcsL`.
- Frozen implementation diff SHA-256:
  `6967a189f86e9792c8b79eb17be0985288d7e5033c9baa1ee509ff29e7cfd0a6`.
