# FlexSpray — open items (current: v5.3, round 5, 2026-11-08)

This file lists only items that are still open, plus a short log of what was closed and where.
The reasoning behind earlier entries is in `history/TODO_round4_v5.2.md`.

Each item is labelled:
- **external**: needs the problem setter or hardware information;
- **boundary**: outside the supported scope, measured and documented;
- **open**: a technical problem that remains.

## Open

| # | Item | Status / evidence | Next step |
|---|---|---|---|
| T2 | Official benchmark traffic and topology spec | **external**. All results use our own cases (`flexspray_case.py`) | Converter to the case format once the spec is provided |
| T4 | Real switch arbitration, VOQ, buffer and credit granularity; endpoint cost of per-packet SFQ, read steering and replay fence | **external** | Optional arbiter in `UbSwitchAllocator`; hardware review |
| T5 | Larger scale (hundreds of endpoints), higher oversubscription, MTP/MPI with `link_health` | Not in this round's scope | `link_health` across partitions; LP scaling (column generation) |
| T7 | Metric conventions (see DELIVERY.md §6 for the concrete questions) | **external** | — |
| T12 | KV incast onto a hotspot that lost a port shares the remaining port unfairly (finish spread 226–298 µs, mixed F1ssd s6) | **open**. v5.2's faster window refill shortens the lone-flow tail but not the unfairness; seed 6 is 4.5 pp below v5.1 (still ≥ 0.90) | Fairness among incast senders at a single-port hotspot (RTT/path asymmetry of culprits) |
| T13 | (superseded by T19 for 1/4; 1/8-1/64 remain continuous-throttle stress tests) Deep lane degrade (≤ 1/4) together with incast: long tail, since the backlog already in the degraded queue cannot be re-steered | **boundary**. The main matrix covers x4→x3 (3/4). Microcase: last task done at 167 / 354 / 1400 µs at 1/4 / 1/8 / 1/16 (ideal ≈ 90 µs) | Treat a severely slowed path as soft-failed (re-send via the fence path) or bound the startup burst |
| T15 | Probe timeout is fixed (10 µs + 3 × 5 µs) | **boundary**. No false death down to 1/16; four at 1/64, where the probe round trip crosses two 1/64 hops | Probe timeout ≥ serialization of 2 MTU at the slowest hop (the replier could report its port rate), or back off while suspect |
| T17 | After the fence-liveness fix, a2a H4 loses 1.2 pp on four seeds (0.945 → 0.933); mixed H4 s6 and H4p4 s1 lose 1.1–1.2 pp | **open, hypothesis**. The fence replaces remote detection (no probes or RTOs, lost bytes 5.2 → 2.4 MB), yet the run is slower | Compare re-dispatch timing and the rate estimates used for the re-dispatch target |
| T18 | Per-device single-fault loss < 1/N_own | **class A for all 56 failing rows (round 6)**: joint-eps fluid LP shows the requirement is physically infeasible even as "<=" for devices whose traffic is confined to the planes shared with fewer-plane peers (h2d F1far eps* -0.053, mixed F4 -0.053; cut in FEASIBILITY.md §2). Feasible devices (a2a F1far +0.064, d2d H1near +0.048) pass; measured loss exceeds the LP minimum by 0.5-2.5 pp. LP for H4p4 cells not completed (solver runs died, rerun pending) | Definition decision (T7); optionally a per-device LP for all 800 rows |
| T19 | **Real lane levels 1/2 and 1/4** | **open; round 9 re-baselined on B1 (FEASIBILITY §7)**. B1 = v5.3 + common RTO fix (RtoSrttFactor=8, RtoFloorFirstOnly, Karn): no row worse than B0 by > 1 pp on core/lane/hard; one unseen gate regression (scale 1.5 mixedread 1/2 near). Read-side minimum M = B1 + budget + return-rate score + send-time rebinding (response depth D removed: it caused mixedread F4/H2up/H4). Device-gate Delta: writes 1/4 +14..+29 us (cause: post-fault new commitment onto the slow plane; S3 bound <= 14 us < Delta, not implemented); a2aread 1/4 +8..+24 us (M), 1/2 +3..+6 us, 3/4 near +2.2 us (budget equal-bytes on own degraded port; class A); short jobs: state at fault binds, ideal S3 migration leaves 1.9 us margin; h2d likely physically infeasible (job LP > T_req) | T19a write dispatch without regressions (experimental DispatchQueueDelayWeight: +9..11 pp at 1/4 but h2d -5 pp, mixedread H2up -3.5 pp; subtract the CC target delay); T19b time-based (not byte) per-port budget; T19c device-level LP for h2d; T19d B1 regression at scale 1.5 |
| T1-rest | Atomic operations are not replayed; a WQE whose TP group has lost every member is rejected; dead TPs are kept, never recycled | **boundary**. The replay fence keys on the TPN, so TP recycling would need a generation field in the fence tag | Only if TP recycling or atomic recovery becomes a requirement |

## Closed (with evidence)

| Item | Closed by | Evidence |
|---|---|---|
| T14: interleaved WQEs on one Jetty completed early and silently dropped unsent segments | `20ff1dde`: a WQE completes when its own last admitted TaSsn is acknowledged; aborts if a COMPLETED WQE would cancel an unsent segment | Unit test with 4 interleaving patterns (fails on old code). The FlexSpray matrix was not affected: one WQE per task-scoped Jetty |
| Replay fence liveness: work on a fenced pair waited forever | `20ff1dde`: a received fence declares the local TP of the pair dead | `fence_false_death_bidir`: 2 hung tasks → 0; P1–P6 pass |
| T11: read-mix worst device 0.889 | `a144494a` + `0413d8ef`: ingress-mark READ steering (weight 0.3) | Seen seeds 1,2: 0.927; unseen seeds 7,8 / map 3: see REPORT.md |
| T16: shallow-buffer read mix per device 0.881–0.885 | Same mechanism | 0.924 on seeds 1,2 (buf128) |
| mixed F1ssd / H4p4 below 0.90 | `f9ae93a7` (AiFraction 0.15) | See T12 for the residual unfairness |
| Comparator adaptive+CAQM+rtx hang on H2up | `dde52df8`: CAQM GBN rollback | 6 hung cells complete |
| 4 KB tail-segmentation hang | `a40f7cf0` | `flexspray_regress.py tail4k` |
| Stale old-path execution after completion | `a40f7cf0`: replay fence | `fence_on` / `fence_off` regression |
| T3 oracle-free remote detection; T1 minimal hard-down recovery; T6 time-indexed LP; T8, T9, T10 | Rounds 3–4 | `history/REPORT_round4_v5.2.md` |
