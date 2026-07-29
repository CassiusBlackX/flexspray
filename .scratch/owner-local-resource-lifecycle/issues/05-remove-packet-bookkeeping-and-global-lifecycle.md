# 05 - Remove packet bookkeeping and obsolete global lifecycle machinery

**What to build:** Remove simulator-global lifecycle work from every packet family and every normal,
drop, fault, malformed, consume, and response path, then delete coordinator machinery that has no
remaining resource-correctness user.

**Blocked by:** 04 - Remove TP dormancy and automatic backing reclaim.

**Status:** ready-for-agent

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
