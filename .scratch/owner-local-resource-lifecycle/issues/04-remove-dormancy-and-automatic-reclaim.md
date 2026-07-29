# 04 - Remove TP dormancy and automatic backing reclaim

**What to build:** Delete simulator-only TP backing suspension before removing the packet-idle gate
that currently authorizes it. TP endpoints remain materialized until explicit owner-local close.

**Blocked by:** 02 - Complete explicit TP connection close across execution owners; 03 - Migrate
Jetty, RTP TPG, CTP, and LDST lifecycle dependencies.

**Status:** ready-for-agent

- [ ] TP endpoints are either active, closing, or retired; no endpoint backing is suspended and later
      rehydrated solely for host-memory policy.
- [ ] Automatic backing-reclaim controls, reclaim scans, dormant snapshots, rehydrate entry points,
      and compact-backing diagnostics are removed.
- [ ] Allocator, transaction, switch, congestion-control, and controller paths retain live endpoint
      backing and do not attempt automatic reclaim.
- [ ] Stable connection and endpoint identities remain available for explicit close correctness.
- [ ] No idle, timeout, RSS, memory-pressure, reference-count, or empty-container reclamation policy
      remains.
- [ ] Packet lifecycle bookkeeping remains temporarily connected until Ticket 05.
- [ ] Focused transport, allocator, transaction, and storage tests pass.
- [ ] Local, MTP, MPI, and hybrid canonical outputs remain raw-byte-identical to baseline.
