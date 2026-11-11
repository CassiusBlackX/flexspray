# FlexSpray — open items (default: v5.3 + T20 fix; last update round 11, 2026-11-11)

This file lists only items that are still open, plus a short log of what was closed and where.
The reasoning behind earlier entries is in `history/TODO_round4_v5.2.md`.

Each item is labelled:
- **external**: needs the problem setter or hardware information;
- **boundary**: outside the supported scope, measured and documented;
- **open**: a technical problem that remains.

## Open

| # | Item | Status / evidence | Next step |
|---|---|---|---|
| T2 | Benchmark traffic and topology with public sources (REQUESTS §6, CLAUDE.md §5) | **open, our responsibility** (no longer external): all results use our own one-shot t = 0 workloads (~175 µs jobs, 48 endpoints, no ring collectives). Independent review route P1 (work/REVIEW_judge_2026-11-11.md) | BENCHMARK.md with sourced parameters; ring / MoE / KVCache phases; ms-scale jobs |
| T4 | Real switch arbitration, VOQ, buffer and credit granularity; endpoint cost of per-packet SFQ, read steering and replay fence | **external** | Optional arbiter in `UbSwitchAllocator`; hardware review |
| T5 | Larger scale (hundreds of endpoints), higher oversubscription, MTP/MPI with `link_health` | Not in this round's scope | `link_health` across partitions; LP scaling (column generation) |
| T7 | Metric conventions (see DELIVERY.md §6 for the concrete questions) | **external** | — |
| T12 | KV incast onto a hotspot that lost a port shares the remaining port unfairly (finish spread 226–298 µs, mixed F1ssd s6) | **open**. v5.2's faster window refill shortens the lone-flow tail but not the unfairness; seed 6 is 4.5 pp below v5.1 (still ≥ 0.90) | Fairness among incast senders at a single-port hotspot (RTT/path asymmetry of culprits) |
| T13 | (superseded by T19 for 1/4; 1/8-1/64 remain continuous-throttle stress tests) Deep lane degrade (≤ 1/4) together with incast: long tail, since the backlog already in the degraded queue cannot be re-steered | **boundary**. The main matrix covers x4→x3 (3/4). Microcase: last task done at 167 / 354 / 1400 µs at 1/4 / 1/8 / 1/16 (ideal ≈ 90 µs) | Treat a severely slowed path as soft-failed (re-send via the fence path) or bound the startup burst |
| T15 | Probe timeout is fixed (10 µs + 3 × 5 µs) | **boundary**. No false death down to 1/16; four at 1/64, where the probe round trip crosses two 1/64 hops | Probe timeout ≥ serialization of 2 MTU at the slowest hop (the replier could report its port rate), or back off while suspect |
| T17 | After the fence-liveness fix, a2a H4 loses 1.2 pp on four seeds (0.945 → 0.933); mixed H4 s6 and H4p4 s1 lose 1.1–1.2 pp | **open, hypothesis**. The fence replaces remote detection (no probes or RTOs, lost bytes 5.2 → 2.4 MB), yet the run is slower | Compare re-dispatch timing and the rate estimates used for the re-dispatch target |
| T18 | Per-device single-fault loss < 1/N_own | **class A for all 56 failing rows (round 6)**: joint-eps fluid LP shows the requirement is physically infeasible even as "<=" for devices whose traffic is confined to the planes shared with fewer-plane peers (h2d F1far eps* -0.053, mixed F4 -0.053; cut in FEASIBILITY.md §2). Feasible devices (a2a F1far +0.064, d2d H1near +0.048) pass; measured loss exceeds the LP minimum by 0.5-2.5 pp. LP for H4p4 cells not completed (solver runs died, rerun pending) | Definition decision (T7); optionally a per-device LP for all 800 rows |
| T19 | **Lane levels x2 / x1: completion-time tail** | **open (implementation)**. Round 11 full-reading table (FEASIBILITY §9.3, DELIVERY §2a): post-rebalance steady-state loss equals the removed capacity for every scheme (default included) and passes 1/N_dev on 38/39 rows (the miss, h2d 1/4 far, is physical). What fails is the completion-time window: 10/42 rows on the default (slow-port drain tail of a2a / a2aread / mixed / mixedread at x2, x1); B1 10, M 6, S4 4, but M and S4 carry T19e and the remaining candidate C fails the oracle (T21) | T19f residual double counting (unverified); a tail mechanism without late binding and without stale-copy backlog |
| T19e | Late-bound READ responses are unrecoverable when their plane hard-fails while the request's TP lives | **boundary (protocol limit)**, round 11: requester-driven replay cannot regenerate them. Mechanism deleted (9f4dc077), M and S4 withdrawn as candidates | Only with responder-side replay + requester de-duplication + a new oracle check |
| T21 | Candidate C (B1 + PortCommitDelay + ServiceTimeScore/Residual + time READ budget + return-rate score) fails oracle P6 on slow_far_1_64 and fence_false_death_bidir | **open**: stale copies from false-death re-dispatch drain into the 1/64 port at link rate (3 MB; 0.84 MB left after a 3 ms drain; the second case passes with 3 ms). B1 passes. Unit suites 50/50, no hangs | Do not admit new data behind a TP being re-dispatched; or cancel unsent stale copies |
| T22 | links-per-pair 1 + H2up after the T20 fix: completes, but read workloads recover slowly (default a2aread 0.37, mixedread 0.46 vs healthy 0.81 / 0.73) | **open (performance)** | Check the in-flight loss window (2 us withdrawal) and the GBN / probe interplay on the affected plane |
| T23 | `UB_LINK_HEALTH_FABRIC_WITHDRAW_NS` = 2 us is an assumption | **assumption**; sensitivity: liveness holds at 2 / 20 / 200 us, mixed eff 0.70 / 0.70 / 0.50 | Source for fabric route convergence time (BENCHMARK.md) |
| T24 | Rebalance-time metric unreliable on read workloads (5 us rate fluctuates > 5 %) | **evaluator limit** | Longer jobs (P1) or a smoothed / byte-based definition |
| T1-rest | Atomic operations are not replayed; a WQE whose TP group has lost every member is rejected; dead TPs are kept, never recycled | **boundary**. The replay fence keys on the TPN, so TP recycling would need a generation field in the fence tag | Only if TP recycling or atomic recovery becomes a requirement |

## Closed (with evidence)

| Item | Closed by | Evidence |
|---|---|---|
| T20: links-per-pair 1 + uplink hard down left 18-68 tasks unfinished (default config too): a spine that lost its only leaf link black-holed sprayed packets while probes succeeded, GBN livelocked | `68aeeb91`: fabric route withdrawal | 8/8 T20 cases complete (B0, B1); 18 lpp>=2 runs bit-identical; FEASIBILITY §9.1 |
| Knob audit: 14 rejected attributes | `9f4dc077` (history at tag `flexspray-r10-pre-audit`) | KNOBS.md; golden 25/25, 50/50 suites x3 configs, 34 runs bit-identical |
| T14: interleaved WQEs on one Jetty completed early and silently dropped unsent segments | `20ff1dde`: a WQE completes when its own last admitted TaSsn is acknowledged; aborts if a COMPLETED WQE would cancel an unsent segment | Unit test with 4 interleaving patterns (fails on old code). The FlexSpray matrix was not affected: one WQE per task-scoped Jetty |
| Replay fence liveness: work on a fenced pair waited forever | `20ff1dde`: a received fence declares the local TP of the pair dead | `fence_false_death_bidir`: 2 hung tasks → 0; P1–P6 pass |
| T11: read-mix worst device 0.889 | `a144494a` + `0413d8ef`: ingress-mark READ steering (weight 0.3) | Seen seeds 1,2: 0.927; unseen seeds 7,8 / map 3: see REPORT.md |
| T16: shallow-buffer read mix per device 0.881–0.885 | Same mechanism | 0.924 on seeds 1,2 (buf128) |
| mixed F1ssd / H4p4 below 0.90 | `f9ae93a7` (AiFraction 0.15) | See T12 for the residual unfairness |
| Comparator adaptive+CAQM+rtx hang on H2up | `dde52df8`: CAQM GBN rollback | 6 hung cells complete |
| 4 KB tail-segmentation hang | `a40f7cf0` | `flexspray_regress.py tail4k` |
| Stale old-path execution after completion | `a40f7cf0`: replay fence | `fence_on` / `fence_off` regression |
| T3 oracle-free remote detection; T1 minimal hard-down recovery; T6 time-indexed LP; T8, T9, T10 | Rounds 3–4 | `history/REPORT_round4_v5.2.md` |
