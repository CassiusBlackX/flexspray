# Combined runtime matrix

状态: 已完成
阻塞于: 02

## Required matrix

- legacy CSV, compressed-table lookup, and structured-rule compute;
- allocator direct and scheduled completion;
- local and MTP16 lifecycle/owner cleanup;
- forced stop, dependency release, fault, congestion control, and retransmission controls.

Record predictions, falsification signals, commands, simulator commit, exit status, and all
failed, skipped, paused, or wall-limited cases before execution.

## Result

- Legacy `ub-quick-example` compatibility PASS.
- Portable compressed next-hop artifact load PASS.
- Prepared structured protocol install/reject controls PASS.
- Allocator direct/scheduled completion, automatic reclaim on/off, local/MTP lifecycle cleanup,
  forced stop, dependency release, fault, congestion-control, retransmission, and timer controls
  PASS.
- No safety stop or residual simulator process was recorded in the final focused matrix.

Evidence is archived under `.scratch/allocator-event-ownership-fastpath/verification-20260726-ticket-06/`.
