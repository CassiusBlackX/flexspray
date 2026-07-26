# T29 Allocator Reclamation Report

## Result

T29 rejected both allocator event-reclamation variants and retained the exact pre-T29 allocator
implementation from `db335a0`. Both variants preserved the measured packet and task semantics, but
neither produced a repeatable end-to-end gain worth its added lifecycle state and callback wrappers.

## Baseline And Method

- Pre-T29 revision: `db335a0068b27b46c4cddd6b04365ac21d3edc0b`.
- Frozen executable SHA-256: `7756e587027029b4d551f70d490659bcea4af0f31cc5fbd47ddc47a46bb45a78`.
- Frozen Unified Bus library SHA-256:
  `0968fced00fb34f13331e73e603c91382601d99a1f588a37517a79aa93a32878`.
- Workload: p01 minimal-observability case with 16 MiB per flow.
- Execution: one warm-up per role followed by five strictly serial counterbalanced pairs.
- Acceptance: exact task completion artifacts and at least 5 percent paired-median wall reduction.

The motivating profile contained 52 samples attributed to
`UbSwitchAllocator::PruneResolvedEvents`, including 19 direct leaf samples. These are profiler
samples, not milliseconds that can be recovered independently; end-to-end paired measurement was
the deciding evidence.

## Attempt 1: Bounded Batch Pruning

The first variant retained the event vector and compacted it once per 64 event completions. It
preserved exact semantics, but still queried `EventId::IsPending()` once for every completed event.

- Paired-median wall: `+0.77%`.
- User CPU: flat.
- Paired-median RSS: `+0.17%`.
- Semantic comparison: exact in all five pairs.

This variant was rejected before commit.

## Attempt 2: Generation Slots

The second variant reserved an owner-private generation slot before scheduling, resolved the slot at
callback entry, and reused it in O(1). Pending-event queries remained only at destroy, query, and
dispose boundaries. Existing allocator callback bodies, timestamps, RR/DWRR selection, queue order,
flow control, notification order, and deadlock cadence remained unchanged.

- Targeted build passed: `unified-bus`, `unified-bus-test`, and `test-runner`.
- `unified-bus-switch-voq` passed.
- `unified-bus-lifecycle` passed, including its expected fail-fast subprocess cases.
- Paired-median wall: `-0.23%`.
- User CPU: flat.
- Paired-median RSS: `+0.08%`.
- Semantic comparison: exact in all five pairs.

This variant was also rejected. Roughly 200 lines of token, wrapper, and slot-management code for a
sub-percent, noisy result would increase correctness and maintenance risk without materially
improving the packet-heavy workload.

## Review And Decision

The final source contains no T29 allocator or test changes. The rejected variants did not change an
ACK/RTO path, packet builder/parser, queue or scheduler algorithm, congestion accounting, lifecycle
terminal semantics, callback timestamp, or trace ordering. The existing tracked-event ownership,
VOQ destroy gate, forced-stop residue, cancellation, and pending-event behavior remain intact.

The allocator prune profile signal was real but not an effective optimization boundary. Further work
should move to lifecycle owner resolution and observation, where packet-tag lookup, owner checks,
node/object resolution, and transfer observation form a higher-frequency chain. That work must begin
with focused call counts and must retain existing identity, generation, terminal, and trace behavior.
