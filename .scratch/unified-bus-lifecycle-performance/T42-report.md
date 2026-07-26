# T42: Typed tracked LDST scheduling

## Result

T42 promotes the measured T34 prototype as a constrained production optimization. The eight LDST
hot-path callers again schedule their original typed member functions, while the lifecycle wrapper
still performs `Track -> ScheduleNow -> Attach` and `Begin -> action -> Resolve`. Forced-stop
cancellation, the generic callback test seam, packet identity, and all protocol algorithms remain.

The final targeted build and both `unified-bus-ldst-lifecycle` and `unified-bus-lifecycle` suites
pass. Expected isolated death-test diagnostics are collected by the passing lifecycle parent runner.

## Correctness

The corrected six-block comparison used exact `next@cf8ee683`, frozen pre-T42 `d376fff`, and the T42
typed implementation. All three roles used one warm-up, two approximately 16 MiB LDST tasks,
strictly serial counterbalanced execution, and trace output OFF. Every emitted task, throughput,
PacketTrace, and TaskTrace artifact was byte-identical in all six blocks.

After the final member-function constraint changed the dylib hash, one additional byte-only run bound
the final library to the same four exact hashes. It is not counted as a performance sample.

The first attempt is invalid and excluded: the detached pre-T42 worktree lacked the
`ns-3-ub-tools` submodule, so its simulation completed without producing parsed task/throughput
outputs and omitted parser time. The submodule was initialized before the accepted v2 run.

## Performance

| In-block comparison | Wall | User CPU | RSS |
|---|---:|---:|---:|
| pre-T42 vs next | +15.00% | +18.35% | +1.70% |
| T42 typed vs next | +4.41% | +11.80% | +1.19% |
| T42 typed vs pre-T42 | -6.90% | -8.44% | -0.33% |

T42 recovers 10.58 percentage points of the current-block next-relative wall gap. It passes the
10% next-relative wall and RSS gates and materially improves both wall and CPU over exact pre-T42.
The remaining `+11.80%` next-relative user CPU is recorded as a residual, not hidden by the wall
result. Per-block host timing was noisy; the counterbalanced paired median is the accepted statistic.

The older T28 p01 `+7.25% wall / +8.42% RSS` remains an immutable result for revision `85c61e8` and
its five-round frozen promotion contract. T42 does not rewrite that historical number. The stronger
current conclusion is that the typed optimization itself improves exact pre-T42, and current T42 is
within the wall/RSS gates against next.

## Review

Review against `main@ccf41b58` confirms the scheduled member functions, arguments, and same-time
event order are restored directly from the old active path. Review against pre-T42 confirms the
lifecycle token, `EventId`, begin, resolve, disposal, and cancellation blocks are retained. The
public overload is constrained to member-function pointers; no parallel builder, parser, state
machine, ACK, queue, routing, packet-lifecycle, or tracked-event storage implementation was added.

The arguments intentionally remain by value, matching the measured prototype and ns-3 event copy
semantics. Perfect forwarding was not added because it would create a new, unmeasured reference and
decay contract for scheduled events.
