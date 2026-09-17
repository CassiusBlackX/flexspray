# T37 Report: DCQCN idle reset alignment

T37 restores `main@ccf41b58` and `next@cf8ee683` sender-idle semantics without changing lifecycle
event ownership. `OnSenderTransportIdle` still releases the host active-flow slot and cancels the
rate-increase, alpha, and pacing events through `CancelTrackedEvent`; it then reuses the old scalar
reset block before setting the next-send time to `Simulator::Now()`.

The change removes one unintended protocol-state difference introduced during lifecycle event
tracking. Idle no longer preserves a prior CNP-cut rate, alpha, recovery counters, or byte progress.
The next sender start still uses the existing active-flow rule to choose line rate or initial rate.

Validation passed:

- targeted Unified Bus/test build with `-j2`;
- `unified-bus-congestion-control`;
- `unified-bus-dcqcn-marking`;
- `unified-bus-lifecycle`;
- one serial 16 MiB PFC/DCQCN next/current replay through production trace boundaries.

The differential produced 16 shared artifacts. Fifteen PFC, Port, congestion, and task artifacts are
byte-identical and parsed task rows match. `TaskTrace_node_2.tr` contains the same 2,048 records,
timestamps, and TASSNs, but lifecycle current writes same-timestamp Send lines before Complete lines
where next writes Complete first. The sorted record multiset hash is identical
(`039f9bb4fcdd13978e1eb4c03196965d757bf5f9cf57ef22714b0ab3859b46e4`). This is an accepted narrow
trace-order exception: the T37 reset executes only after sender idle and cannot cause per-segment
same-timestamp trace ordering during the active flow.

No CNP, ECN, rate-increase, pacing, packet, queue, parser, builder, or TP cleanup code changed.
