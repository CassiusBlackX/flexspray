# T55 Treatment Experiment Spec

- Goal: prove real MTP16 compact backing and owner restoration while preserving canonical behavior.
- Topology/workload/runtime/transport: byte-identical to `t55-before`.
- Additional observability: versioned runtime-storage snapshot.
- Expected witness: completed tasks 64, dependency references 0, compact records greater than 0,
  restoration count greater than 0, valid endpoint accounting, and peak live backing above final live
  backing.
- Wrong-owner/public-owner negative evidence remains test-derived from
  `unified-bus-tp-reservation`.
- Source: HEAD `5965931f378430f3d4195ec7fab5487a4e01ce1d` plus recorded T51-T54 patch.
- Status: approved.
