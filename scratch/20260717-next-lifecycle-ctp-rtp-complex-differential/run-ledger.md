# Run ledger

- package: `scratch/20260717-next-lifecycle-ctp-rtp-complex-differential`
- checkpoint policy: `continue_full_matrix`
- runtime: local-single, release, `rng-run=1`
- execution: serial only

| Case | Role | Status | Return | Elapsed | Artifacts | Observation |
|---|---|---|---:|---:|---|---|
| `ctp-win1` | next | success | 0 | 0.164s | 16 files | analysis pending |
| `ctp-win1` | lifecycle | success | 0 | 0.167s | 22 files | analysis pending |
| `ctp-win4` | next | success | 0 | 0.114s | 16 files | analysis pending |
| `ctp-win4` | lifecycle | success | 0 | 0.135s | 22 files | analysis pending |
| `ctp-win128` | next | success | 0 | 0.111s | 16 files | analysis pending |
| `ctp-win128` | lifecycle | success | 0 | 0.135s | 22 files | analysis pending |
| `rtp-shared-win1` | next | success | 0 | 0.091s | 18 files | analysis pending |
| `rtp-shared-win1` | lifecycle | success | 0 | 0.090s | 23 files | analysis pending |
| `rtp-gbn-double-drop` | next | success | 0 | 0.090s | 25 files | analysis pending |
| `rtp-gbn-double-drop` | lifecycle | success | 0 | 0.090s | 31 files | analysis pending |
| `rtp-tpg-gbn` | next | success | 0 | 0.101s | 21 files | analysis pending |
| `rtp-tpg-gbn` | lifecycle | success | 0 | 0.105s | 27 files | analysis pending |

## Analysis decisions

| Case | Input bytes | Shared evidence | Lifecycle validation | Decision |
|---|---|---|---|---|
| `ctp-win1` | identical | 16/16 shared artifacts byte-identical | 23,408 records valid | matched |
| `ctp-win4` | identical | 16/16 shared artifacts byte-identical | 23,408 records valid | matched |
| `ctp-win128` | identical | 16/16 shared artifacts byte-identical | 23,408 records valid | matched |
| `rtp-shared-win1` | identical | only same-timestamp TaskTrace ordering differs; complete-line multisets identical | 506 records valid | partially matched, controlled ordering |
| `rtp-gbn-double-drop` | identical | one fewer cumulatively acknowledged DATA attempt; all tasks complete | 682 records valid | partially matched, approved T06 semantics |
| `rtp-tpg-gbn` | identical | wire/path/completion equal; host task-to-TP attribution differs | 1,452 records valid | partially matched, generation pinning |

No row failed, stalled, crashed, or remained inconclusive. No unexplained difference was accepted.
