# FlexSpray — open items (default: v6; last update round 17, 2026-11-21)

This file lists only items that are still open, plus a short log of what was closed and where.
The reasoning behind earlier entries is in `history/TODO_round4_v5.2.md`.

Each item is labelled:
- **external**: needs the problem setter or hardware information;
- **boundary**: outside the supported scope, measured and documented;
- **open**: a technical problem that remains.

## Open

| # | Item | Status / evidence | Next step |
|---|---|---|---|
| T5 | Larger scale (hundreds of endpoints), higher oversubscription, MTP/MPI with `link_health` | Not in this round's scope (`cm_pod` / `cm_pod_osub` / `ub_mesh` are 48–56 endpoints) | `link_health` across partitions; LP scaling (column generation) |
| T7 | Metric conventions (see `EVAL_PROTOCOL.md` / `DELIVERY.md` for all 4 `N` & 3 window conventions) | **external** (fixed problem-setter ambiguity; repo reports all readings side-by-side) | — |
| T12 | KV incast onto a hotspot that lost a port shares the remaining port unfairly | **open** (`r17_kv_io`: `flex+spraycc` reaches `0.7654` on `F3ssd3` vs SOTA `<= 0.4128`, `lm_r17_kv` `2/2` pass, but `pi_r17_kv` s16 shows `15.19%` excess vs `s15` `9.73%`) | Fairness among incast readers/writers at a degraded 2-plane SSD hotspot |
| T13 | Deep lane degrade (`<= 1/8..1/64`) stress tests | **boundary** (`x3/x2/x1` covered by v6 `F3*` / `F3*3`; `1/64` remains extreme stress in `recov200_2`) | Treat severely throttled (`<= 1/16`) path as soft-failed or bound startup burst |
| T15 | Probe timeout is fixed (`12 us` suspect + `25 us` x 2/3 retries) | **boundary**. `recov200_2` passes P1–P5 across all 20+ cases; `fence_false_death_bidir` triggers P6 fail under 1/64 bidir throttle (`false_dead_no_down=7, credits_stuck_live=2356640`) | Rate-aware probe timeout or backoff while suspect |
| T17 | After the fence-liveness fix, slight regression on a2a H4 / mixed H4 (`-1.1..1.2 pp`) vs pre-fence | **open, low priority** | Compare re-dispatch timing vs rate estimates after remote fence |
| T18 | Per-device single-fault loss `< 1/N_dev` under asymmetric attachment & mesh cuts | **boundary (LP-proven)**: on `ub_pod` (`28/28`) & `cm` (`11/12`) & `kv_io` (`4/4`) v6 meets `< 1/N_dev`; on `ub_mesh` (`2/12`), `a2a` has `4` LP-proven infeasible rows (`lp_lb >= 1/10`) and `6` single-hop detour rows | Multi-hop mesh detour routing or accept topology cut limit |
| T19e | Late-bound READ responses are unrecoverable when their plane hard-fails while request TP lives | **boundary (protocol limit)**, round 11: mechanism deleted (`9f4dc077`) | Only with responder-side replay + requester deduplication |
| T21 | P6 at 200 us drain on `slow_far_1_64` / `fence_false_death_bidir` | **characterised, not a leak** (round 13/15): passes at `DRAIN_US=10000` (`FLEX_DRAIN_AFTER_US`); CBFC packets drain at 6.25 Gbps on `1/64`; only `fence_false_death_bidir` holds stuck live credits via T15 | Resolved by T15 only |
| T22 | `links-per-pair 1 + H2up` read recovery slower than write recovery | **open (performance)** (`b1`/`v53`/`v6` complete all tasks without deadlock since T20) | Check in-flight loss window and GBN / probe interplay |
| T23 | `UB_LINK_HEALTH_FABRIC_WITHDRAW_NS` = 2 us is an assumption | **assumption**; sensitivity tested in round 11 (2 / 20 / 200 us) | Source fabric route convergence time |
| T24 | Rebalance-time metric noisy on short read workloads (`5 us` rate fluctuates `> 5%`) | **evaluator limit** (`steady` often `n/a` on short multi-phase jobs; kept in denominator) | Longer jobs or smoothed byte-based window |
| T26 | Benchmark parameter sensitivity (`b_aicb_tp_ep`, `b_deepep_skew`, `b_kv_skew` scale `0.5/2.0` partially done in `r16_kv_s05/s20`) | **partly done** (`r16_kv_s05`: 0.6625, `r16_kv_s20`: 0.9166; `b_ring` `x4` done R14) | `×0.5 / ×2` sweeps for remaining `b_aicb_*` / `b_deepep_*` phases if needed |
| T27 | Whole-job `eff_fixed` LP invalid on multi-phase DAGs | **evaluator boundary** (superseded by `flexspray_phaseinfl.py` + `flexspray_dagbound.py --fault-aware`) | Always use `dagbound --fault-aware` for multi-phase jobs |
| T29 | Short-barrier overhead on multi-phase MoE (`b_moe`): `-2.2 ~ -3.1 pp` vs `mprdma+dq2+ooo2` on `H1far` (`r17_cm` 0.6937 vs 0.7244, `r17_ub_pod` 0.6676 vs 0.6895) and decode-size gap | **boundary (design trade-off)** (`PortCommitDelay=6us` + 25 us probe wait adds ~2 us/phase on hard-down barrier restart, buying **3.42x** gain under `F3far3` 0.6552 vs 0.1913) | Adaptive commit delay / faster probe recovery at phase boundary |
| T31 | Hard link-down on barrier-per-step ring slightly above strict `1/8` (`12.7%` vs `12.5%` at `--scale 4`) | **reclassified, boundary** (fixed ~9 us detection delay amortizes from above) | Path-RTT-scaled probes only |
| T1-rest | Atomic operations are not replayed; a WQE whose TPG lost all members is rejected; dead TPs are kept, never recycled | **boundary** (replay fence keys on TPN; no TPN generation field; write/read remote idempotency assumed) | Only if TP recycling or atomic recovery becomes a requirement |

## Closed (with evidence)

| Item | Closed by | Evidence |
|---|---|---|
| **T32 + T19 + T19f + T28 + T30**: READ budget hurt after port loss; x1 lane degrade long tail; ServiceResidual h2d penalty; multi-phase per-phase tail | **Round 15 FlexSpray v6 (`54db3bb8`)**: Time-Quantum Segmentation (`M1`) + Reachability-Aware `HardDown`-only READ Repooling & Work-Conserving (`M2`) + Subtract-Own Residual & `150 us` Idle Half-Life (`M3`) + T29 Desync/HeavyWqe/Coalesce (`KNOBS.md` R15) | Unseen `s15, s16`: `lm_bench` `lane` `8/20 -> 20/20`; `lm_old_cov` `lane` `47/58 -> 57/58` (`N_scope 58/58`); `phase_bench` `21 -> 0` per-phase (`112/112` 1-off, `1.06%`); `u_old` min `0.3919 -> 0.9166`; ablations confirm M1–M3 (`FEASIBILITY §13.1`) |
| **T2**: Public supernode topologies & open-source AI workloads | **Round 16 Stage 2 (`f2676403`)**: `cm_pod`, `cm_pod_osub` (`arXiv:2506.12708`), `ub_mesh` (`arXiv:2503.20377`), `b_aicb_tp_ep`, `b_deepep_skew`, `b_deepep_prefill`, `b_nccl_pipe`, `b_kv_skew` (`flexspray_topo_public.py`, `flexspray_aicb.py`, `BENCHMARK.md §9`) | Unseen `s15, s16`: `r16_cm` `0.8248 vs B1 0.7880`; `r16_cm_osub` `0.8616 vs 0.8186`; `r16_mesh` `0.7733 vs 0.7237`; `lm_r16_aicb` `lane 18/18`; `dag_r16_fa` `0.9452`; `pi_r16_bench` `216/216` non-KV 1-off |
| **Stage 3 SOTA comparison + `UbHostDcqcn` teardown late-CNP race** | **Stage 3 (`r17cand2`)**: `REPS_CACHE`, `MPRDMA_OOO`, `PINNED_PLANE` in `ub-transaction.{h,cc}` & `ub-transport.{h,cc}`; `IsTpLifecycleActive()` / `m_tpDestroyRequested` in `ub-dcqcn.{h,cc}`; `UbDcqcnLateCnpDuringDrainTest` | Seen `s1, s2` tuning `504/504 rc=0`; blind `s15, s16` `756 + 42 (kv_io) = 798` runs `0 rc!=0`; `flex+spraycc` `0.8271` vs next `0.6354` (`F3far3` `0.8206 vs <=0.4202`; `kv_io F3ssd3` `0.7654 vs <=0.4128`; `dag_r17_fa` `0.9336 vs <=0.7979`) |
| **T4**: Hardware state, header & switch datapath overhead audit (endpoint/switch SRAM, ECN/packet cost, arbiter scope) | **Round 17 hardware audit** (`FEASIBILITY §13.3`, `DELIVERY §3c`): direct source inventory across `ub-{transport,transaction,spraycc,caqm,dcqcn}.h`, `ub-port.h`, `ub-header.h`, `ub-datatype.h` | Core fabric `0` change; L1 `m_kDelay + 1B/port`; `70 B` SprayCC ACK; `4 B` fence tag only on redispatch; `O(P)` (`P<=10`) endpoint state & min-drain scan (vs CAQM `4×64b` switch vectors + 5 PSN maps) |
| **T25**: Simulator speed | **closed: not a work item** (user decision 2026-11-11). Reached 1.72x in round 12 | — |
| T20: `links-per-pair 1` + uplink hard down livelock | `68aeeb91`: fabric route withdrawal | 8/8 T20 cases complete; 18 lpp>=2 runs bit-identical |
| Knob audit: 14 rejected attributes (+1 R15 `ReadQuantumOnFaultOnly`) | `9f4dc077`, `54db3bb8` | `KNOBS.md`; Golden 25/25 SAME, 50/50 suites |
| T14 / Replay fence liveness / T11 / T16 / H4 / CAQM GBN rollback / tail4k / fence / T1 / T3 / T6 / T8–T10 | Rounds 3–13 (`history/REPORT_round4_v5.2.md`, `DELIVERY.md`, `RESEARCH.md`) | Verified continuously in `r17cand2`: `testrunner` `11/11 PASS`, `alltests_frozen` `50/50 PASS`, Golden `25/25 SAME`, `flexspray_regress` `exit 0` |
