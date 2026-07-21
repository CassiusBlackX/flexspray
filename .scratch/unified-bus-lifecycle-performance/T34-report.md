# T34: Lifecycle OFF performance root-cause calibration

## Scope

This investigation measures host runtime overhead, not simulated network throughput. The control is
`next@cf8ee683`; the treatment revisions are lifecycle implementation checkpoints. Every accepted
comparison uses the same p01 input, two roughly 16 MiB LDST tasks, packet spray disabled,
`rng-run=1`, packet/port/per-hop trace disabled, and lifecycle output disabled when supported.

Each result uses one warm-up per role followed by five counterbalanced pairs. All simulation runs are
strictly serial. The reported percentages are medians of paired deltas, so machine-wide speed changes
between experiment blocks do not become revision deltas.

## Harness correction

The earlier historical/ablation runner exported `DYLD_LIBRARY_PATH` before invoking macOS
`/usr/bin/time`. SIP strips `DYLD_*` variables when a protected system executable starts, so the
measured child could fall back to the binary's embedded build-tree rpath instead of the requested
frozen dylib. This made several cross-library ablations false negatives and caused an ABI abort after
the shared build tree moved to T20.

The corrected command places `/usr/bin/env DYLD_LIBRARY_PATH=<absolute role library directory>`
after `/usr/bin/time` and before the measured binary. A load audit confirmed that both Unified Bus
and core dylibs came from the role-specific frozen directory.

The build-tree control also passed: `14596fc` versus `cf8ee683`, rebuilt in the same tree with the
same compiler and flags, measured `+0.32%` wall, `0.00%` user CPU, and `+0.09%` RSS. The other nine
ns-3 dylibs were byte-identical. All five semantic artifact comparisons were exact.

## Valid checkpoint results

| Treatment vs `next@cf8ee683` | Wall | User CPU | RSS | Semantic artifacts |
|---|---:|---:|---:|---|
| pre-implementation `14596fc` | +0.32% | 0.00% | +0.09% | exact 5/5 |
| T12 `67567ea` | -0.97% | 0.00% | +3.78% | exact 5/5 |
| T13 `90f083d` | +42.24% | +44.62% | +4.18% | exact 5/5 |
| T16 `1571206` | +52.32% | +52.31% | +5.19% | exact 5/5 |
| T20 `47fd697` | +55.47% | +57.14% | +5.54% | exact 5/5 |
| T24 `dfcfcb7` | +41.39% | +42.11% | +5.00% | exact 5/5 |
| HEAD `51c60c5` | +29.27% | +31.48% | +5.26% | exact 5/5 |

T12 is performance-neutral. The first large jump is T13, commit `0e362d4`, which closes the LDST
lifecycle. T14-T20 add roughly another 13 percentage points. T23-T24 and T25-T33 recover roughly
26 percentage points in total, but current HEAD remains about 29% slower in this trace-off p01 case.

## T13 attribution

T13 activates two high-frequency paths that were dormant at T12:

1. LDST request and ACK packet birth registers an identity tag and active attempt. Existing terminal
   calls then validate and retire every attempt. At T13, existing per-hop transfer observers also
   become active for tagged packets.
2. Eight legacy typed `Simulator::ScheduleNow(&Function, args...)` call sites are replaced by a
   `Callback<void>` wrapper. Every action then performs tracked-event `Track`, `Attach`, `Begin`, and
   `Resolve`, maintaining both event records and coordinator lease state.

Diagnostic-only T13 ablations preserve task and throughput artifact bytes:

| T13 treatment | Wall delta | User CPU delta | Interpretation |
|---|---:|---:|---|
| bypass tracked-event records, keep `Callback<void>` | -7.89% vs T13 | -8.24% | tracked maps/leases are material |
| suppress packet birth/tag | -11.15% vs T13 | -12.00% | packet identity path is material |
| suppress both, keep generic callback scheduling | +18.98% vs next | +19.70% | generic callback path leaves a large residual |
| restore legacy typed scheduling and suppress packet lifecycle | +1.89% vs next | +1.52% | segment/container changes are not the regression |

The last row reduces the remaining difference to noise-scale wall/CPU, while retaining exact
simulation artifacts. This identifies generic callback dispatch plus lifecycle tracking and packet
identity accounting as the T13 regression, rather than LDST task/segment storage.

## Current HEAD attribution

Corrected HEAD ablations give these bounds:

| Treatment vs formal HEAD | Wall | User CPU | Confidence |
|---|---:|---:|---|
| restore pre-refactor typed LDST call sites and remove tracked leases | -15.71% | -17.05% | high; semantic exact |
| bypass tracked leases but retain generic callback callers | -5.97% | -6.82% | medium; late pairs drifted |
| retain packet tag but reduce packet ledger work | +1.23% | +1.15% | inconclusive/no gain |
| suppress packet identity tag | -7.59% | -7.69% | high; all pairs improved |

The direct-callsite result is an upper bound, not a shippable fix, because it removes forced-stop
event tracking. The gap between the first two rows indicates that roughly 9-10 percentage points are
associated with generic `Callback<void>` dispatch/copying rather than the tracked lease invariant
itself.

At current HEAD, `ObservePacketTransfer` returns on lifecycle `OFF` before reading the packet tag.
Therefore the no-tag gain is not leftover per-hop trace parsing. It comes from birth/terminal
correctness accounting: identity tag creation/copying, active-attempt bookkeeping, terminal tag read,
coordinator routing, and attempt retirement.

## Typed tracked scheduling prototype

A diagnostic prototype restored the eight pre-refactor typed LDST member-function call shapes while
retaining the lifecycle wrapper around each event. The wrapper still performs `Track`, `Attach`,
`Begin`, the original action, and `Resolve` in that order, and pending events remain cancelable during
forced stop. No packet builder, parser, ACK state machine, queue algorithm, or lifecycle invariant was
changed.

The prototype passed the targeted Unified Bus build and the `unified-bus-ldst-lifecycle` suite. In the
broader `unified-bus-lifecycle` suite, every forced-stop, reentrant-dispose, and
cancel-between-tracked-phases case passed. The suite retained the same pre-existing aggregate failure
as formal HEAD: after an earlier OFF-transfer test, the packet-copy case observes zero transfer
records. Formal HEAD and the prototype fail that same case with the same assertions, so it is not a
prototype regression.

A direct five-pair formal-versus-prototype comparison measured `-8.40%` wall and `-8.70%` user CPU,
with exact artifacts in all five pairs. A stricter three-role calibration then ran `next`, formal HEAD,
and the prototype inside each of six counterbalanced blocks:

| In-block comparison | Wall | User CPU | Semantic artifacts |
|---|---:|---:|---|
| formal HEAD vs `next@cf8ee683` | +18.12% | +19.28% | exact 6/6 |
| typed tracked prototype vs `next@cf8ee683` | +4.75% | +4.98% | exact 6/6 |
| typed tracked prototype vs formal HEAD | -9.22% | -9.76% | exact 6/6 |

Pairwise medians are not algebraically composable, but the direction is stable: the prototype is
faster than formal HEAD in every block, and reduces the median in-block wall gap to `next` from
`18.12%` to `4.75%`. This validates the first optimization hypothesis. The original `+29.27%` HEAD
measurement remains valid for its earlier block, but absolute host-runtime gaps are sensitive to the
machine's performance state; the counterbalanced three-role block is the stronger current
calibration.

## Conclusion

The current slowdown is real, but it is not all an unavoidable correctness tax. Typed tracked
scheduling removes most of the currently calibrated gap without weakening lifecycle correctness.

The lifecycle invariants that must remain are packet birth/terminal balance, generation validation,
forced-stop cancellation, terminal ordering, and cleanup. Lifecycle `OFF` disables output and
transfer evidence, not these correctness invariants.

The current implementation adds avoidable work around those invariants:

- high-frequency LDST callbacks are type-erased and copied through `Callback<void>`;
- tracked events use multiple ordered-map/lease operations per same-timestamp action;
- packet attempts still pay tag, active-attempt, coordinator-routing, and terminal costs even though
  transfer evidence is already skipped at `OFF`.

## Recommended implementation order

1. Promote the validated typed tracked LDST scheduling prototype into a production ticket. Preserve
   `Track/Attach/Begin/action/Resolve`, forced-stop cancellation, and the eight old typed caller shapes.
   Do not restore untracked direct calls as the final implementation.
2. Reduce tracked-event storage churn. Tokens are monotonic and same-timestamp events are short-lived;
   replace duplicate ordered-map lookups/lease records with a compact owner-local representation while
   preserving cancelable pending events and running-event semantics.
3. Optimize packet birth/terminal accounting without deleting identity. Prefer contiguous bitsets or
   another monotonic-attempt structure and reuse the canonical coordinator/owner reference. Keep the
   existing OFF-before-tag-read transfer fast path.

The first implementation ticket should target typed tracked LDST scheduling. Its acceptance gate must
include the existing forced-stop/reentrancy tests, exact p01 semantic artifacts, and a corrected
counterbalanced performance comparison. A realistic first target is to recover the 9-10 percentage
points attributable to generic callback wrapping while retaining lifecycle correctness.

## Evidence status

Authoritative summaries are under:

- `evidence/t34-main-gap/homogeneous-14596fc-vs-cf8ee683-v2/summary.json`
- `evidence/t34-main-gap/homogeneous-next-vs-t12-v2/summary.json`
- `evidence/t34-main-gap/homogeneous-next-vs-t13-v2/summary.json`
- `evidence/t34-main-gap/homogeneous-next-vs-t16-v2/summary.json`
- `evidence/t34-main-gap/homogeneous-next-vs-t20-v4/summary.json`
- `evidence/t34-main-gap/homogeneous-next-vs-t24-v2/summary.json`
- `evidence/t34-main-gap/homogeneous-next-vs-head-v1/summary.json`
- `evidence/t34-main-gap/ldst-callsite-ablation/runs-v2-fixed-dyld/summary.json`
- `evidence/t34-main-gap/ldst-direct-ablation/runs-v2-fixed-dyld/summary.json`
- `evidence/t34-main-gap/homogeneous-formal-vs-tag-only-v2/summary.json`
- `evidence/t34-main-gap/homogeneous-formal-vs-no-tag-v2/summary.json`
- `evidence/t34-main-gap/typed-tracked-ablation/runs-v1-fixed-dyld/summary.json`
- `evidence/t34-main-gap/homogeneous-next-vs-head-v2-recalibration/summary.json`
- `evidence/t34-main-gap/homogeneous-next-vs-typed-tracked-v1/summary.json`
- `evidence/t34-main-gap/three-role-typed-tracked-v1/summary.json`

The earlier `runs-v1` cross-library ablations and the first homogeneous v1 attempts must not be used
for causal conclusions. `homogeneous-next-vs-t20-v3` is also excluded because lifecycle output was
accidentally enabled and produced about 1.07 MiB of trace data.
