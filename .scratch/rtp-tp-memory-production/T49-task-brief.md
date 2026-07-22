# T49 Task Brief: Route TP Use Through Owner-Local Reclaim And Resolve

Status: complete

## Authority And Scope

- Ticket: `tickets.md`, Task 49.
- Feature contract: `.scratch/rtp-tp-memory-production/PRD.md`.
- Starting commit: `1f52924`.
- T48 owns dormant record storage and continuity. This ticket routes every active RTP consumer
  through that manager seam and invokes reclaim only after a later owner-local transition.
- This ticket does not add a flag, polling event, remote lifecycle opcode, connection teardown,
  runtime snapshot schema, scale run, or prototype removal.

## Resolution Contract

- Add a manager query that returns the exact current endpoint lifecycle identity for a `LIVE` or
  `DORMANT` record and fails for missing or `DRAINING` records. It must not create or mutate state.
- `CreateReservedTpEndpointLocked` resolves an existing dormant record instead of trying to create
  a duplicate endpoint. Existing live records return directly and draining records reject.
- `ResolveInboundTp` validates the packet channel key against the unchanged reservation, then
  synchronously resolves the exact dormant identity before packet consumption.
- Sender binding resolves every requested TPN before creating a direct reference or TPG. It keeps
  the existing input order, random selection, multipath cursor, and exact identity.
- Direct references resolve through `UbTransportChannelManager::ResolveLiveTp` using their stored
  identity before preflight or work admission.
- TPG materialization resolves each selectable member through the same seam using the member's
  stored identity. Rehydration must not change membership version, order, selection cursor, or
  materialization counters.
- Remote-origin work resolves its direct TP identity before enqueue/admission. Already-admitted
  terminal cleanup remains pinned to its live exact generation and must never rehydrate a TP that
  still has active work.
- A caller outside the endpoint owner continues to use the existing packet/event delivery path. No
  call site may access a remote node's manager to mutate a dormant endpoint.

## Reclaim Trigger Contract

- Extend the existing TP state-change callback so a `LIVE` endpoint is considered for reclaim and a
  `DRAINING` endpoint retains its existing explicit-destroy finalize behavior.
- Inbound sink handling makes one owner-local reclaim attempt after packet processing returns. It
  does not reclaim before consumption.
- The allocator makes one owner-local reclaim attempt after it releases the active TP queue for the
  current allocation round. A busy/non-empty TP simply fails the manager gate.
- Destroying a Jetty binding captures its referenced direct TPNs before erasing the reference, then
  attempts reclaim only after the binding and transaction reference no longer make the TP in use.
- Releasing a TPG membership lease attempts reclaim for that exact member after the member is no
  longer present. A still-live group or another binding keeps the TP ineligible.
- Failed attempts are not retried immediately and schedule no event. Only a later queue, packet,
  transaction, binding, allocator, or lifecycle transition may call the seam again.
- Every trigger executes synchronously on the endpoint owner and uses the existing global TP
  connection-control lock only through the manager public seam. No caller borrows a remote LP.

## TDD Slices

1. A dormant reserved endpoint resolves through ordinary `CreateReservedTpEndpoint` with the same
   identity; wrong generation and draining state still reject.
2. A valid inbound packet rehydrates before consumption and preserves receive/ACK behavior; an
   invalid packet does not rehydrate or mutate the dormant record.
3. Sender direct binding and multipath TPG creation rehydrate dormant candidates in the existing
   TPN order. Direct preflight and TPG member materialization resolve exact stored identities.
4. Completing and destroying a task-scoped direct Jetty triggers reclaim after its mapping is
   erased. A live operation, binding, allocator round, retransmission residue, or CC event blocks
   reclaim until a later transition.
5. Sending the final ACK/CNP from an RTP ingress queue triggers reclaim only after allocator active
   state clears. RR and DWRR phase, canonical task events, and packet outcomes stay unchanged.
6. Owner checks prove that no resolution or trigger mutates a manager on another node/LP and no new
   simulator event or remote control record is created.

## Expected Ownership

- `src/unified-bus/model/ub-transport-channel-manager.{h,cc}`: identity query, ordinary reserved and
  inbound live-or-dormant resolution, live transition reclaim.
- `src/unified-bus/model/protocol/ub-transaction.{h,cc}`: sender/direct/TPG/remote resolution and
  post-Jetty-unbind reclaim.
- `src/unified-bus/model/ub-controller.cc`: TPG member release reclaim.
- `src/unified-bus/model/ub-switch.cc`: post-inbound-processing owner-local reclaim attempt.
- `src/unified-bus/model/ub-switch-allocator.cc` and transport callback seam if required: reclaim
  after active allocator ownership clears.
- `src/unified-bus/test/ub-test.cc`: focused public-seam coverage.

## Validation Contract

- RED must demonstrate at least ordinary dormant reserved resolution and automatic direct-Jetty
  terminal reclaim failing before the routing/trigger implementation.
- Build serially with `ninja -C cmake-cache scratch_ub-quick-example unified-bus-test`.
- Run `unified-bus-tp-reservation`, `unified-bus-lifecycle`,
  `unified-bus-congestion-control`, `unified-bus-runtime-storage-snapshot`, and `unified-bus`
  serially with the matching non-default runner and `DYLD_LIBRARY_PATH=build/lib`.
- Run changed-line clang-format and `git diff --check` from `1f52924`.
- No T50 attribution schema, prototype removal, canonical differential, guarded scale checkpoint, or
  full repository test belongs to T49.

## Validation Results

- RED covered ordinary dormant reserved resolution and direct-Jetty terminal reclaim before the
  routing and trigger implementation was complete.
- GREEN build passed with
  `ninja -C cmake-cache scratch_ub-quick-example unified-bus-test`.
- `unified-bus-tp-reservation`, `unified-bus-lifecycle`,
  `unified-bus-congestion-control`, `unified-bus-runtime-storage-snapshot`, and `unified-bus`
  passed serially with `DYLD_LIBRARY_PATH=build/lib` and the matching non-default runner.
- Changed-line `git clang-format 1f52924` completed for the T49 files and
  `git diff --cached --check` passed.
- Independent correctness and contract reviews passed after fixing live inbound validation,
  logical-endpoint assertions, and the DWRR intermediate-queue reclaim trigger.

## Closeout

- Implementation commit: `1365a5b` (`feat(unified-bus): route TP backing lifecycle`).
- Reserved creation, inbound packets, sender binding, direct references, TPG materialization, and
  remote-origin admission now resolve live or dormant backing through the manager seam before use.
- Reclaim attempts run synchronously after inbound consumption, lifecycle state changes, direct
  binding release, TPG member release, and allocator active-queue release. They add no polling,
  control record, connection teardown, or endpoint generation.
- DWRR attempts reclaim whenever its active queue changes, so every released TP in a multi-queue
  allocation round is considered rather than only the final queue.
- All new call sites operate on their owning controller and manager. Defensive rejection inside the
  public manager seam for an externally supplied remote manager remains a possible hardening task;
  the production path does not borrow or mutate a remote LP.
- T50 remains responsible for runtime attribution rows, prototype removal, canonical differential
  evidence, and guarded RSS checkpoints.
