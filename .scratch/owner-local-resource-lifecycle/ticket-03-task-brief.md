# Ticket 03 implementation ledger

Status: complete

## Acceptance suite

- Targeted `unified-bus-test` and `test-runner` build in the current release MPI+MTP configuration.
- `unified-bus-tp-reservation --fullness=QUICK` for TP explicit-close interaction.
- `unified-bus --fullness=QUICK` for Jetty, RTP TPG, CTP, LDST, transaction, packet, ACK/RTO,
  queue, allocator, routing, and congestion behavior.
- Frozen local and MTP canonical byte comparison. MPI/hybrid canonical comparison remains blocked
  by the separately recorded quick-example startup path and is not reported as passing.

## Batch Reuse Map

Pre-refactor commit: `38e1430`

Semantic batch: migrate Jetty and RTP TPG destroy gates from coordinator leases to authoritative
owner-local state, then characterize CTP and LDST unchanged behavior.

Old active symbols:

- `UbJettyManager::{ReplaceRtpBinding,ReleaseRtpBindings,RegisterCtpBinding,FinalizeDestroy}`
- `UbController::{CreateRtpTpGroup,RemoveRtpTpGroupMember,DestroyRtpTpGroup}`
- `UbController::{FinalizeRtpTpGroupDestroy,NotifyRtpTpGroupUseLeaseReleased}`
- `UbTransaction::{TryAdmitRtpWqe,ReleaseTransportUseLease}`
- `UbTransportChannelManager::AssessTpQuiescenceLocked`

Old call paths:

- Jetty bind/unbind/destroy -> duplicate coordinator BINDING lease plus local binding reference.
- TPG operation admission/terminal -> coordinator TRANSPORT_USE lease -> controller finalize notify.
- TPG membership add/remove -> coordinator BINDING lease plus group member/reverse-index update.
- TP quiescence -> coordinator activeBindingLeases rather than controller membership state.

Continuous code blocks reused:

- Jetty manager canonical record, accepted-WQE map, transaction operation query, and tracked-event
  set.
- Transaction `m_operations` exact transport reference and segment maps.
- Controller canonical TPG map, exact group identity, member vector, and reverse TPN index.
- Existing TPG freeze, pending last-member removal, typed cleanup, reentrancy suppression, and final
  map erase ordering.

Reuse mode: query the existing local containers directly. Do not add a replacement counter or
participant registry.

Required lifecycle adaptations:

- Jetty RTP/CTP binding state is stored only as local binding identities/flags, not coordinator
  leases.
- TPG destroy and last-member removal wait for exact transaction operations and admitted segments,
  not a global transport-use count.
- TPG membership is represented by the exact group member vector and reverse index; TP quiescence
  queries that owner-local fact instead of activeBindingLeases.
- Operation terminal paths notify the controller only after the exact operation leaves
  `m_operations`.

Preserved ordering and observable behavior:

- New Jetty/TPG admission freezes before destroy; accepted work continues.
- TPG member selection, generation pinning, retransmission, packet/ACK handling, queues, allocator,
  routing, congestion control, and member TP lifetime are unchanged.
- Jetty and TPG resource identities remain coordinator-backed temporarily; Ticket 05 removes
  obsolete coordinator machinery after packet bookkeeping is disconnected.
- CTP and LDST packet bookkeeping remains connected temporarily, but their completion and forced
  cleanup decisions remain based on their local operation/task/segment/queue/event state.

Existing characterization:

- Focused Jetty destroy tests cover idle, pending accepted work, CTP binding, tracked events, force
  cleanup, stale completion, and WQE reuse.
- Focused TPG tests cover active use, last-member removal, add-during-pending, typed cleanup,
  reentrancy, controller disposal, multi-segment generation pinning, and retransmission.
- UnifiedBus QUICK covers CTP and LDST external behavior.

Explicit behavior changes:

- A TPG remains pending until exact accepted operations and their admitted segments terminalize;
  final segment assignment alone is no longer treated as group quiescence.
- Coordinator Jetty/TPG binding and transport-use snapshots cease to be correctness contracts.

Excluded paths:

- Packet birth/transfer/terminal bookkeeping and origin-return controls (Ticket 05).
- TP dormancy, automatic backing reclaim, and rehydrate deletion (Ticket 04).
- WORK, CHILD, TRANSPORT_WORK, and TP/CC tracked-event lease removal unless a focused failure proves
  they are required for this ticket's explicit Jetty/TPG semantics.
- Customer convenience policy (Ticket 06).

Focused validation: targeted build, focused QUICK suites, local/MTP canonical bytes, then separate
review against `38e1430`.

## Closeout

- Jetty RTP binding truth remains solely in the transaction's typed reference map; CTP retains its
  owner-local service flag. Neither path acquires a global `BINDING` lease.
- TPG membership is proven by the controller member vector and reverse TPN index. TP backing
  quiescence queries that canonical state instead of a coordinator binding count.
- TPG destroy and last-member removal query exact transaction operations. `terminalPending`
  tombstones no longer interact with their TPG because every retained segment already pins an exact
  TP generation.
- A TPG remains live after final segment assignment and retires only after the accepted operation
  terminalizes. Multi-segment generation pinning, typed cleanup, reentrant cleanup, and controller
  disposal tests cover this ordering.
- The release MPI+MTP build passed `unified-bus-lifecycle`, `unified-bus-rtp-resolution`,
  `unified-bus-ctp-lifecycle`, `unified-bus-ldst-lifecycle`, `unified-bus-tp-reservation`, and the
  main `unified-bus` QUICK suite.
- Frozen local output remained byte-identical with SHA-256
  `10f54f22c7b3f8e935b08c87eb9ed1e8d8e6384fc8ece55c8b77b5a6e0b9dd64`. A serialized MTP-only
  rebuild and replay remained byte-identical with SHA-256
  `72a03b332b4fb144ad1a3ccc03d454f1c7e506ceafcb6274360b5ca96b7579f6`.
- The current MPI+MTP quick-example still cannot serve as the MTP-only canonical fixture: the
  system-ID case selects `UbRemoteLink` without an MPI launch, while the prebuilt-TP case queries
  MTP ownership before partition installation. These pre-existing fixture/bootstrap limitations are
  recorded for later work and are not lifecycle acceptance evidence.

Re-approval triggers: packet format, ACK/RTO, queue, allocator, routing, congestion-control, member
TP destruction, cross-owner control, or public destroy API changes.
