# CTP Fast-Path Memory Scaling Run Ledger

- Package: `scratch/20260720-ctp-fastpath-memory-scaling`
- Branch: `codex/unified-bus-lifecycle-implementation`
- Runtime: local-single, release, `rng-run=1`
- Runner: `scripts/run_experiment.py`
- Checkpoint policy: `continue_full_matrix`
- User approval: 2026-07-20

| Case | Role | Status | Runs | Evidence | Observation |
|---|---|---|---:|---|---|
| `scale-512m` | next | success | 2 | `results/performance-compact-history-v1/summary.json` | one warm-up and one measured run |
| `scale-512m` | lazy | skipped | 0 | - | final compact-history gate compares production candidate directly with next |
| `scale-512m` | fastpath | success | 2 | `results/performance-compact-history-v1/summary.json` | wall -0.67%; RSS -0.74% |
| `scale-2g` | next | success | 6 | `results/performance-compact-history-v1/summary.json` | one warm-up and five measured runs |
| `scale-2g` | lazy | skipped | 0 | - | final compact-history gate compares production candidate directly with next |
| `scale-2g` | fastpath | success | 6 | `results/performance-compact-history-v1/summary.json` | wall +4.20%; RSS -0.74%; final gate PASS |
| `scale-8g` | next | success | 6 | `results/performance-compact-history-8g-v2/summary.json` | independent one-warm-up/five-pair replay |
| `scale-8g` | lazy | skipped | 0 | - | final compact-history gate compares production candidate directly with next |
| `scale-8g` | fastpath | success | 6 | `results/performance-compact-history-8g-v2/summary.json` | wall -0.01%; RSS -0.42% |
| `multi-entity-2g` | next | success | 1 | `results/diagnostics/compact-history-production-v1-multi-entity-2g/summary.json` | final production control |
| `multi-entity-2g` | lazy | success | 1 | `results/correctness/multi-entity-2g/summary.json` | initial diagnostic mismatch versus next retained |
| `multi-entity-2g` | fastpath | success | 1 | `results/diagnostics/compact-history-production-v1-multi-entity-2g/summary.json` | final production bytes and task rows exact |
| `mixed-bidir-2g` | next | success | 1 | `results/diagnostics/compact-history-production-v1-mixed-bidir-2g/summary.json` | final production control |
| `mixed-bidir-2g` | lazy | skipped | 0 | - | no final diagnostic role required |
| `mixed-bidir-2g` | fastpath | success | 1 | `results/diagnostics/compact-history-production-v1-mixed-bidir-2g/summary.json` | final production bytes and task rows exact |

## Checkpoints

- Inputs: complete; all templates and SHA-256 manifests materialized.
- Complex correctness: complete. Stable queue identity makes both multi-Entity and
  mixed-bidirectional production canaries byte-identical to next, including parsed task rows.
- Frozen correctness: complete. `ctp-win1`, `ctp-win4`, and `ctp-win128` are exact; see
  `results/frozen-ctp-compact-history-v1/summary.json`.
- Queue retirement causality: complete. The diagnostic isolated allocator-slot churn; the production
  repair retains empty queue scheduler identity until service disposal.
- Performance matrix: complete. The 2 GiB acceptance block passes the wall and RSS gates; the
  independent 8 GiB replay confirms neutral runtime and bounded RSS.
- Analysis: complete. The prior payload-scaled RSS high-water came from one trace-OFF retired map
  shell per Transaction segment. Compact runs remove that growth while preserving generation and
  terminal-outcome history.
