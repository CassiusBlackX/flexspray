# T25 Report: Compact Retired Lifecycle Resource Storage

## Outcome

T25 passes its storage, correctness, SUMMARY byte, and bounded p02 acceptance. It releases heavy
active lifecycle state after valid retirement while retaining compact generation and terminal
outcome history. It does not introduce TP cleanup or change modeled resource closure.

## Implementation

Each canonical resource slot now contains compact history plus an active allocation. The active
allocation owns the snapshot, attempt and lease identities, mutation sequence, and state-change
callback. `RetireResource` keeps its existing balance checks and RESOURCE_RETIRE trace order, then:

1. records the compact terminal outcome;
2. retains a final snapshot only when lifecycle evidence is enabled;
3. releases the complete active allocation.

Reopening the same local ID increments generation and allocates a fresh active state in the same
canonical slot. Validation still checks owner, generation, retirement, and mutable-state presence.

With lifecycle output OFF, a retired `GetSnapshot` reconstructs only `retired` plus any work terminal
outcome. Packet and lease totals are evidence-only after valid retirement and are no longer retained.
With SUMMARY/EVENT/DETAIL enabled, the complete final snapshot is retained and emitted unchanged.

## Focused Churn

The characterization opens, completes, and retires 100,000 distinct WQE resources using the
production coordinator API and lifecycle trace OFF.

| Metric | Pre-layout | T25 | Change |
|--------|------------|-----|--------|
| Canonical resources | 100,000 | 100,000 | generation history retained |
| Active states | 100,000 | 0 | -100% |
| Final summaries | 100,000 | 0 | -100% |
| Peak RSS | 59,441,152 B | 28,491,776 B | -52.07% |

Local-ID zero then reopens at generation two while resource count remains 100,000 and only the
replacement owns active state.

## SUMMARY Contract

The same explicit-SUMMARY input was run with frozen pre-T25 and post-T25 Unified Bus libraries. The
complete output and runlog artifact hash maps are identical, including lifecycle schema and both
node trace files. No byte exception is required.

## p02 OFF

One warm-up and five counterbalanced pre/post pairs ran strictly serially.

| Revision | Median wall | Median user | Median sys | Median RSS |
|----------|-------------|-------------|------------|------------|
| Pre-T25 `dfcfcb7` | 0.1911 s | 0.14 s | 0.03 s | 63,356,928 B |
| T25 `1afd398` | 0.1886 s | 0.14 s | 0.03 s | 63,324,160 B |

- Paired-median wall: `-1.87%`.
- Median RSS: `-0.05%`.
- Median page faults: 135 for both.
- Median block input/output operations: zero for both.

p02 is effectively flat because its distinct retired-resource population is not large enough for
compact history to dominate process RSS. T25's measured benefit is the long sequential-churn case,
not automatic TP reclamation.

## Review And Validation

- Targeted Unified Bus test and quick-example build: PASS.
- `unified-bus-lifecycle-storage`: PASS.
- `unified-bus-lifecycle --verbose`: PASS.
- `unified-bus-lifecycle-trace --verbose`: PASS.
- Explicit SUMMARY complete artifact bytes: PASS.
- p02 OFF serial pre/post measurement: PASS.

Review against `main@ccf41b58` and the pre-T25 coordinator path found no packet builder/parser,
protocol state machine, ACK/RTO, queue/scheduler, routing, congestion-control, TP cleanup, callback,
or trace-order change.
