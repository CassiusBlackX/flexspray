# T30 Coordinator RTTI Lookup Report

## Result

T30 tested the existing TypeId-based ns-3 aggregate lookup in
`UbLifecycleCoordinator::Resolve`. The one-line variant preserved exact behavior but did not meet
its low-complexity performance gate, so it was reverted.

## Evidence

The current p01 profile contained 913 sampled backtraces. Packet transfer observation appeared in
47, route transfer in 34, coordinator resolution in 10, `GetNNodes` in 11, and `GetNode` in 7.
RTTI/dynamic-cast frames appeared in 55 inclusive process samples, including a direct coordinator
resolution stack. This established a real hot seam but did not predict its independent end-to-end
weight.

The variant replaced `node->GetObject<UbLifecycleCoordinator>()` with the existing
`node->GetObject<UbLifecycleCoordinator>(UbLifecycleCoordinator::GetTypeId())` overload. No owner,
identity, generation, counter, trace, queue/link, terminal, MTP/MPI, or error behavior changed.

Validation passed:

- targeted `unified-bus`, `unified-bus-test`, and `test-runner` build;
- `unified-bus-lifecycle`, including expected fail-fast subprocess cases;
- `unified-bus-switch-voq`;
- exact task, throughput, and trace artifacts in all five performance pairs.

One warm-up and five strictly serial counterbalanced p01 pairs produced:

- paired-median wall: `-0.82%`;
- non-regressing wall pairs: `3/5`;
- paired-median user CPU: flat;
- paired-median RSS: `+0.33%`.

The predefined gate required at least `-1.00%` wall, at least four non-regressing pairs, and no CPU
or RSS regression above 1 percent. The variant failed the first two conditions.

## Decision

The final T30 source is identical to pre-T30. RTTI lookup is not retained as an isolated
optimization. The next narrow batch may combine execution-owner validation with coordinator node
resolution so the packet path performs one owner-node lookup rather than separate owner-check and
resolve lookups. That batch must preserve the same remote-owner branch and error ordering and must be
measured independently.
