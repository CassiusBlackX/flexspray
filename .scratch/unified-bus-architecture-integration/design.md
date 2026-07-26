# Unified Bus architecture integration

## Purpose

Maintain one `next`-based validation branch for the structured topology/routing,
allocator direct completion, Unified Bus lifecycle, and TP backing compaction changes.
The branch preserves component history and enters `next` after representative correctness,
compatibility, performance, and independent-review gates pass. Exhaustive performance research
continues on `next` and is not a prerequisite for entering the validation branch.

## Included lines

- `codex/structured-topology-routing-closure`, including tools-v2 at `4e7847f`.
- `codex/allocator-direct-completion`.
- `codex/rtp-tp-memory-compaction`, which contains
  `codex/unified-bus-lifecycle-implementation`.

Dependency-visibility case compatibility, landing pages, and theory-bound validation are
outside this integration.

## Integration decisions

- `UbRoutingTable` and `UbRoutingPolicy` own legacy route storage and path selection.
- Candidate lookup order is structured rule, compressed exact index, then legacy table.
- Structured compatibility wrappers remain available to existing callers.
- Allocator direct completion retains the scheduled compatibility mode and lifecycle event
  tracking.
- TP reclaim and resolution remain owner-gated and fail closed.
- The tools gitlink remains pinned to the verified tools-v2 commit.

## Invariants

- Preserve selectors, ingress filtering, primary fallback, and one-detour routing behavior.
- Preserve `AllocationTime`, port/SerDes/link, flow control, congestion control, and
  retransmission behavior.
- Keep `LOCAL_COMPLETE -> COMPLETE_VISIBLE -> dependency release -> owner cleanup`.
- Keep `PeekLiveTp()` read-only; only owner-gated resolution may restore backing.
- Reject corrupt structured artifacts and invalid owner operations without semantic fallback.
- Preserve ordinary CSV case, CLI, and trace compatibility.

## Merge gates for `next`

- Focused Unified Bus, lifecycle, structured-routing, TP-reservation, allocator, and compatibility
  suites pass.
- Representative compatibility evidence covers the legacy CSV example, portable compressed
  next-hop artifact, and prepared structured-rule protocol.
- Allocator on/off, direct/scheduled completion, local/MTP owner cleanup, forced stop, dependency
  release, fault, congestion-control, and retransmission controls pass.
- One representative 1024-device/64-switch PR-vs-main comparison records canonical output,
  retired instructions, runtime, cycles, events, and RSS. The gate requires measured and explained
  differences, not performance equality with `main`.
- Independent review finds no blocking correctness or compatibility issue.

The full routing/workload cross-product, repeated 1024-scale sweeps, complete attribution of the
remaining PR/main CPU gap, and optimization of other liveness ledgers are non-blocking follow-up
work on `next`.

## Allocator performance closure

The allocator event-ownership regression and the required typed TP reclaim assessment are planned
under `.scratch/allocator-event-ownership-fastpath/`. This subplan is part of the current Draft PR,
not a post-merge follow-up. Its allocator correctness and performance gates must pass before the
reclaim-assessment refactor begins, and both must complete before final compatibility review.

Other liveness ledgers remain separate evidence-backed follow-up tasks. They do not block this PR
because no additional ledger optimization is part of its production diff.
