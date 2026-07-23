# T27 Report: Selective Retransmission Timer Fast Path

## Outcome

T27 passes its timer-state, SELECTIVE/GBN protocol, lifecycle, fault-scenario, and review
acceptance. A running retransmission timer now returns before querying outstanding descriptor state.
The first expired-timer call still requires live descriptor state before it schedules.

The focused fast path removes 1,000,000 redundant SELECTIVE outstanding queries in the exercised
loop. Five counterbalanced runs reduce median loop time from 5,262 us to 4,305 us (`-18.19%`). This
is a seam-level microbenchmark. The bounded fault scenario is byte-identical but too short and noisy
for a scenario-level performance claim.

## Reuse And Implementation

`main@ccf41b58` checked `EventId::IsExpired` before scheduling. T27 restores that old timer-first
guard and retains the descriptor-era empty-state protection added by `6ec09e6` immediately after it:

1. retransmission must be enabled;
2. the current timer must be expired;
3. GBN or SELECTIVE outstanding state must exist;
4. `m_rto` resets to `m_baseRto` and the existing timeout path schedules once.

Default-disabled test counters observe start, selective scan/entry, schedule, cancel, and ACK-restart
counts. No production builder, parser, ACK/TPSACK, timeout strategy, retry queue, descriptor
retirement, queue/scheduler, congestion-control, tracked-event, callback, trace, or completion block
changed.

## Focused Evidence

The fixture retains 4,096 SELECTIVE descriptors and performs 1,000,000 calls while the first timer
remains running.

| Metric | Pre-T27 | T27 |
|---|---:|---:|
| `StartTimerIfNeeded` calls | 1,000,001 | 1,000,001 |
| SELECTIVE outstanding queries | 1,000,001 | 1 |
| Entries visited | 1,000,001 | 1 |
| Timeout schedules | 1 | 1 |
| Cancels | 0 | 0 |
| ACK restarts | 0 | 0 |
| Five-run median loop time | 5,262 us | 4,305 us |

The live map normally begins at an outstanding PSN, so the old query usually visited one entry. T27
does not claim that every send previously traversed the complete 4,096-entry window.

## Validation

- Targeted `unified-bus-test`, `test-runner`, and quick-example build: PASS.
- `unified-bus-retrans-timer-scan`: PASS, including SELECTIVE and GBN controls.
- `unified-bus-transport-retrans`: PASS.
- `unified-bus-lifecycle`: PASS; expected fail-fast child aborts were collected by the passing suite.
- Existing selective-loss fast-retry MarkPSN scenario: PASS.
- Complete pre/post scenario input and runlog trees: byte-identical.
- `git diff --check`: PASS.
- Review against `main@ccf41b58`, `6ec09e6`, and pre-T27 HEAD: PASS.

The scenario's one-shot internal host total varied from 5.466 ms pre to 8.483 ms final. That variation
is retained as a negative/noisy observation rather than presented as a gain. T28 remains responsible
for final repeated main/current/next host-performance acceptance.
