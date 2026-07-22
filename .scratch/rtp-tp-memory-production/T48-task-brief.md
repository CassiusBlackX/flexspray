# T48 Task Brief: Add Dormant TP Backing Records And Manager Seams

Status: complete

## Authority And Scope

- Ticket: `tickets.md`, Task 48.
- Feature contract: `.scratch/rtp-tp-memory-production/PRD.md`.
- Starting commit: `b691768`.
- This ticket adds explicit owner-local backing reclamation and synchronous same-generation
  rehydration. It does not add automatic triggers or route every TP consumer; T49 owns those paths.
- Existing `DestroyTp` remains logical endpoint teardown: it retires the packet owner and later
  rematerialization opens a new generation. Dormancy is a separate operation and must not weaken or
  reinterpret explicit destroy tests.

## Confirmed Public Test Seams

Use public `UbTransportChannelManager`, `UbTransportChannel`, lifecycle, transaction, allocator,
and congestion-control APIs through existing Unified Bus tests. Tests may use established `ForTest`
continuity setters/getters, but must not inspect manager-private containers.

## State Contract

- Manager records have explicit `LIVE`, `DORMANT`, and `DRAINING` states. A dormant record is still
  logically present; `Contains(tpn)` remains true while `GetTp(tpn)` returns no live backing object.
- `ReclaimBackingState(tpn)` is owner-local and idempotent. Missing/draining/ineligible/busy records
  return false without mutation; an already-dormant record returns true.
- Reclamation retains the exact active `RTP_ENDPOINT` lifecycle identity and generation. It never
  calls `RetirePacketOwner`, `TpDeinit`, reservation erase, or `OpenPacketOwner`.
- The gate covers lifecycle packet and lease counts, TP queues and inbound work, ACK/CNP work,
  retransmission state/timer, transaction use and pending remote work, allocator pending/active work,
  and congestion-control eligibility. Snapshot failure is fail-closed and leaves the live record
  unchanged.
- The dormant snapshot preserves send/ack/receive PSN continuity, TP MSN/PSN allocation, receive
  bitmap/window and inbound ordering references, per-packet load-balancing salt, relevant capacity
  flags/configuration, exact lifecycle identity, and an algorithm-owned congestion-control snapshot.
  Queues, active work, packets, timers, and retransmission residue are not snapshotted because the
  gate requires them to be empty or inactive.
- `ResolveLiveTp(tpn, expectedIdentity)` returns a matching live channel directly, synchronously
  rehydrates a matching dormant record, and returns null for missing, draining, stale-generation, or
  wrong-owner requests without mutation.
- Rehydration creates a new backing object without opening a lifecycle generation, validates the
  preserved identity is still active, attaches and restores congestion control, restores transport
  continuity, and only then publishes the new backing object.
- Transaction storage keeps the existing TPN entry and RR state while swapping live pointer to a
  dormant null/backing reference. Allocator storage replaces the queue at its existing slot with a
  lightweight always-empty placeholder, then replaces that exact slot on rehydration. It never
  erase-appends the dormant endpoint.
- No simulator event, polling loop, cross-owner control record, remote LP access, reservation,
  renumbering, or new generation is introduced.

## TDD Slices

1. A fully drained endpoint reclaims to `DORMANT`; duplicate reclaim is idempotent; resolve returns
   a different backing object with the exact same lifecycle identity/generation and preserved
   PSN/MSN/receive/order continuity.
2. One active lifecycle lease, packet/work residue, transaction binding/pending work, allocator
   activity, retransmission timer/residue, or ineligible congestion control rejects reclamation and
   leaves the original live object unchanged.
3. Wrong owner/generation resolve rejects without mutation; the exact identity rehydrates
   synchronously and remains active in the lifecycle coordinator.
4. DCQCN and CAQM snapshots survive manager reclaim/resolve; unknown or unsupported algorithms stay
   live without aborting.
5. Two endpoints sharing an allocator out-port preserve relative slot count/order and RR/DWRR phase
   across dormant replacement. Explicit `DestroyTp` continues to retire and reopen a new generation.

## Expected Ownership

- `src/unified-bus/model/protocol/ub-transport.{h,cc}`: move-only continuity snapshot and
  preserved-identity restore.
- `src/unified-bus/model/ub-transport-channel-manager.{h,cc}`: public reclaim/resolve contract and
  dormant records.
- `src/unified-bus/model/protocol/ub-transaction.{h,cc}`: identity-preserving backing-pointer swap
  without erasing RR/reference state.
- `src/unified-bus/model/ub-switch-allocator.{h,cc}` and `ub-switch.{h,cc}`: replace-at-slot seam.
- `src/unified-bus/test/ub-test.cc`: focused public-seam tests.

## Validation Contract

- RED must fail for missing reclaim/resolve/state APIs before implementation.
- Build serially with `ninja -C cmake-cache scratch_ub-quick-example unified-bus-test`.
- Run `unified-bus-lifecycle`, `unified-bus-tp-reservation`,
  `unified-bus-congestion-control`, and `unified-bus` serially with the matching non-default runner
  and `DYLD_LIBRARY_PATH=build/lib`.
- Run changed-line clang-format and `git diff --check` from `b691768`.
- No T49 trigger/routing change, scale run, runtime snapshot schema change, prototype removal, or
  full repository test belongs to T48.

## Validation Results

- RED: the focused build failed before `ReclaimBackingState`, `ResolveLiveTp`, and the continuity
  seams existed.
- GREEN build: `ninja -C cmake-cache unified-bus-test` passed with the matching worktree build.
- `unified-bus-tp-reservation`, `unified-bus-lifecycle`,
  `unified-bus-congestion-control`, and `unified-bus` passed serially with
  `DYLD_LIBRARY_PATH=build/lib` and the non-default test runner.
- Changed-line `git clang-format --force b691768` completed and `git diff --check` passed for the
  intended T48 files.
- Independent standards and spec reviews passed after fixing stale-backing admission, the DCQCN
  TP ownership cycle, rehydrated trace attachment, and dormant admission-domain retirement.

## Closeout

- Implementation commit: `40d8d03` (`feat(unified-bus): reclaim dormant TP backing`).
- The manager now distinguishes `LIVE`, `DORMANT`, and `DRAINING`; reclaim and resolve preserve the
  exact lifecycle identity and generation without opening, retiring, or remotely borrowing an
  endpoint.
- Dormant state retains compact PSN/MSN, receive-window, ordering, routing, retransmission, and
  algorithm-owned congestion-control continuity. The receive bitmap stores only its base, size,
  and marked sequence numbers instead of copying every window slot.
- Transaction entries and allocator slots are replaced in place, preserving RR state and queue
  position. Explicit dormant destroy still retires the preserved generation and clears its
  admission domain.
- Backing release freezes stale references and disposes reconstructible object state, including the
  DCQCN TP ownership cycle. Rehydrated objects reconnect the normal TP trace surface before use.
- T49 remains responsible for routing sender, inbound, direct-reference, and TPG-member use through
  the manager seam and for invoking reclaim only after later owner-local state transitions.
