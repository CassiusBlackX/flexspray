# 03 — Migrate Jetty, RTP TPG, CTP, and LDST lifecycle dependencies

**What to build:** Move every remaining non-packet correctness decision needed by explicit destroy
or finite protocol completion onto state already owned by the relevant Jetty, transaction, TPG,
CTP, or LDST module, without introducing a replacement global ledger or generic participant model.

**Blocked by:** 01 — Resolve live, closing, retired, and invalid TP ingress owner-locally.

**Status:** ready-for-agent

- [ ] Jetty destroy closes new WQE admission and derives readiness from accepted WQEs, transaction
      operations, owner-tracked actions, and bindings.
- [ ] RTP TPG destroy and last-member removal freeze new selection and derive readiness from actual
      transaction operations, admitted segments, selection activity, admission bindings, and member
      records.
- [ ] RTP TPG use and binding leases from the global lifecycle coordinator are removed without
      changing group membership, selection, or member TP lifetime behavior.
- [ ] CTP retains its existing operation, pending/outstanding segment, delayed completion, queue,
      and congestion behavior without global packet ownership state.
- [ ] LDST retains its task map, segment map, thread queue, ACK, scheduled-action, and completion
      behavior without using a global in-flight packet count.
- [ ] WQEs, transaction segments, allocator work, timers, packets, and queue entries remain finite
      owner state and do not acquire customer-facing destroy interfaces.
- [ ] No universal lifecycle participant, root-operation identity, lineage ledger, or manual generic
      acquire/release framework is introduced.
- [ ] Focused Jetty, TPG, CTP, and LDST external-behavior tests pass with byte-identical frozen
      canonical outputs.
