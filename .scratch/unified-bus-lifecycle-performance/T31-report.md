# T31 Fused Owner Resolution Report

## Result

T31 reused the node resolved by packet execution-owner classification for local lifecycle
coordinator mutation. The implementation preserved exact behavior and passed focused tests, but it
regressed every measured p01 wall pair and user CPU. It was reverted.

## Implementation Tested

The semantic batch first extracted the exact current bodies of
`UbParallelLifecycleAdapter::IsExecutionOwner` and `UbLifecycleCoordinator::Resolve` into internal
helpers. Old callers were migrated and validated before packet transfer/terminal used the helpers.

For local owners, the treatment performed one node-range check and one `NodeList::GetNode`, then
resolved the same node aggregate and ran the same transfer/terminal mutation. For remote owners, the
existing MPI system-ID check, MTP logical-process check, source-context validation, causal-control
record, and route timing remained unchanged. No registry, cache, packet pointer, public API, or owner
state was added.

## Validation

Both the extraction-only stage and final packet-route stage passed the targeted Unified Bus build
and `unified-bus-lifecycle`. The final stage also passed `unified-bus-switch-voq`. Expected abort
output came only from the lifecycle suite's existing fail-fast subprocess cases; the parent suite
reported PASS.

All five p01 pairs produced exact task, throughput, and trace artifact sets.

One warm-up and five strictly serial counterbalanced pairs produced:

- paired-median wall: `+0.50%`;
- non-regressing wall pairs: `0/5`;
- paired-median user CPU: `+1.54%`;
- paired-median RSS: flat.

The predefined gate required `-1.50%` wall, at least four non-regressing pairs, and no CPU/RSS
regression above 1 percent. T31 failed all wall and CPU conditions.

## Review And Decision

Source review found no behavior divergence, but the helper/output-pointer shape added enough branch
and call structure to offset the duplicated lookup it removed. More importantly, T30 and T31 now
provide two independent paired results showing that coordinator lookup is not a useful end-to-end
optimization boundary for p01.

The final source is identical to pre-T31. Further owner-resolution variants are stopped. New work
requires fresh evidence on a different hot path, such as the required packet-tag decode plus transfer
observation update itself, rather than another coordinator lookup rewrite.
