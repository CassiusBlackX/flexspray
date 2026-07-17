# T24 Report: Steady-State Packet Lifecycle Bookkeeping

## Outcome

T24 preserves its semantic and lifecycle correctness contract and reduces p01 wall regression, but
does not meet the 10 percent performance threshold. It closes through the ticket's explicit
measured-blocker branch rather than widening into speculative redesign.

The pre-change p01 paired-median wall regression was `+20.19%`; the final result is `+16.65%`.
Peak RSS changed from `+15.63%` to `+15.29%`. All five final current/main wall deltas are positive,
so the remaining regression is not treated as timing noise.

## Implementation

- Pack exact packet-attempt membership into one 64-bit word per attempt range while preserving
  arbitrary out-of-order terminal and exact inactive-attempt rejection.
- Reuse the last canonical `ResourceState` lookup within each coordinator. Every mutation still
  checks owner, generation, and retirement; canonical map ownership does not change.
- Move the existing TP lifecycle state-change callback from ACTIVE creation to the first DRAINING
  transition. It is installed before freeze and the immediate drain check.

No packet builder/parser, protocol state machine, ACK/RTO policy, queue/scheduler, routing,
congestion-control, or packet-byte logic changed.

## Profile And Reuse Review

The bounded profile selected the current transfer path:

`PeekPacketTag -> RoutePacketTransfer -> Resolve -> ObservePacketTransfer`

The implementation retains the current lifecycle mutation blocks and minimally changes only the
membership container operations and canonical lookup. The destroy notification reuses and moves the
existing callback registration statement. Review against `main@ccf41b58` confirms that the old TP
creation, transaction initialization, allocator registration, tracing, and destroy-side effects
retain their relative order outside the required lifecycle seams.

## Validation

- `git diff --check`: PASS.
- Targeted `unified-bus-test`, `test-runner`, and quick-example build: PASS.
- `unified-bus-lifecycle --verbose`: PASS, including exact membership, generation reuse, stale and
  retired mutation, TP idle/busy/reentrant destroy, and forced-stop cases.
- Final p01: one warm-up plus five counterbalanced pairs, strictly serial.
- Semantic comparison: no current-only, main-only, hash, or unexplained mismatch.

| Revision | Median wall | Median user | Median sys | Median RSS |
|----------|-------------|-------------|------------|------------|
| Main `ccf41b58` | 1.784 s | 1.68 s | 0.07 s | 19,824,640 B |
| T24 `83080d6` | 2.075 s | 1.98 s | 0.07 s | 22,855,680 B |

- Paired-median wall: `+16.65%`.
- Median wall: `+16.29%`.
- Median RSS: `+15.29%`.

## Residual

The remaining p01 overhead is in always-on packet tag/owner routing and lifecycle mutation work.
The accepted T24 batches recovered only part of that cost. Because further changes would require a
wider ownership or packet-seam redesign, the ticket stops at the measured blocker as required.
T25-T27 remain independent optimizations; T28 must keep this residual visible in final main/current/
next reporting rather than averaging it away.
