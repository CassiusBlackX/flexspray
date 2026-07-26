# Unified Bus architecture integration

## Purpose

Maintain one `next`-based validation branch for the structured topology/routing,
allocator direct completion, Unified Bus lifecycle, and TP backing compaction changes.
The branch preserves component history and remains a Draft until combined large-scale
correctness and performance evidence is complete.

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

## Validation boundary

Focused builds, unit suites, and a legacy smoke establish integration readiness only. The
branch still requires structured/compressed runtime smokes, allocator on/off controls,
local/MTP lifecycle controls, fault/retransmission coverage, and 1024-scale route/RSS/event
and runtime measurements before it can leave Draft.

## Allocator performance closure

The allocator event-ownership regression and the required typed TP reclaim assessment are planned
under `.scratch/allocator-event-ownership-fastpath/`. This subplan is part of the current Draft PR,
not a post-merge follow-up. Its allocator correctness and performance gates must pass before the
reclaim-assessment refactor begins, and both must complete before final compatibility review.

Other liveness ledgers are profiled in that subplan, but production changes outside allocator
ownership and TP reclaim assessment require separate evidence-backed tasks.
