# FlexSpray v7: Theoretical Foundations, Invariants, and Fault-Tolerant Convergence

This document formalizes the scheduling, flow-control, and transport-safety foundations of **FlexSpray v7** (integrated in [src/unified-bus/model/](file:///home/cassius/ub/ns-3-ub/src/unified-bus/model/)), linking each theoretical guarantee directly to C++ mechanisms and blind unseen-seed (`seeds=15,16`) Round 19 validation in [results/r19/r19_summary.json](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/results/r19/r19_summary.json).

---

## 1. System Model & Fault Abstraction

Consider a multi-plane UnifiedBus (UB) cluster hosting compute endpoints (NPUs/CPUs) and storage/network I/O endpoints (NICs/SSDs). Each healthy NPU connects through $P$ independent transport planes $\mathcal{P} = \{0,\dots,P-1\}$ ($P=8$ in `ub_pod` / `ub_mesh` host-facing planes; $P=7$ in CloudMatrix-derived `cm_pod`; up to $P \le 10$ including direct links in `ub_mesh`), where each port comprises $L$ physical lanes ($L=4$) with nominal unidirectional bandwidth $R_0$ (`400 Gbps`, evaluated across `100G–800G` in E5) and nominal unloaded cross-fabric round-trip time $\tau_{\text{RTT}}$.

A distributed AI training/inference job executes as a directed acyclic graph (DAG) of bulk transfer phases $\{1,\dots,K\}$ (`AlltoAll`, `Ring AllReduce`, `AllGather`, `ReduceScatter`, hybrid `TP/EP/DP` MoE, or KVCache read/write offload) interleaved with barrier compute gaps $g_k$ (`2–20 us`). At fault time $t_f$, one or more ports experience:
- **Graceful isolation** (`F1*`, `F2up`, `F4`): port capacity drops to $0$ after notifying local routing;
- **Asymmetric lane degradation** (`F3*`, e.g., $1/4$ rate $\beta = 0.25$ on $1$–$3$ lanes lost): port remains active with residual physical capacity $\beta_i R_0$;
- **Silent hard link-down & dynamic flaps** (`H1*`, `H2up`, `F3heal`, `F3multi`, `F3flap`, `H1heal`): remote senders receive no out-of-band link oracle and must detect loss, fence re-dispatches, and optionally recycle TPs after physical recovery (`UP` / `RESTORE`).

---

## 2. Theorem 1 — Asymmetric-Link Lock-Step Tail Inflation & Time-Quantum Commitment Bound

### Statement

Consider an $M$-phase synchronized collective workload where each barrier phase $k$ releases byte volume $B_k$ per endpoint across $P$ planes, with $1$ degraded plane $p^*$ operating at residual rate $\beta R_0$ ($0 < \beta < 1$, e.g., $\beta = 0.25$ in `F3far3`/`F3far`) while the remaining $P-1$ planes operate at rate $R_0$, separated by compute gaps $g_k > 0$.

1. **(Breakdown of Byte-Quantum & Reactive-Only Multi-Pathing)** Any scheme that either (a) distributes segments/packets in equal byte quanta $q_{\text{byte}}$ across $\mathcal{P}$ or (b) lacks residual drain-time tracking across inter-phase idle gaps incurs per-phase wall-clock inflation:
   $$\frac{D_k - L_k}{L_k} \;\ge\; \Omega\!\left(\frac{1}{\beta P} - \frac{1}{P}\right),$$
   repeating across **every** post-fault barrier phase $k$ (persistent per-phase straggling).

2. **(FlexSpray Time-Quantum Commitment + De-Self-Queued Residual Bound)** Suppose endpoint dispatch (`UbTransaction` + `UbTransportChannel`) enables rate-proportional time-quantum segmentation (`TimeQuantumSeg=true`, targeting quantum duration $\tau_q$, byte quantum $q_i = \min(Q_{\max}, \lfloor \tau_q \hat{r}_i \rfloor)$ per plane rate estimate $\hat{r}_i$, bounded within `QuantumTailBytes=49152` under `SlackTailOnly=true`) and selects next segment port via greedy minimum projected finish time:
   $$p^*(t) = \arg\min_{i \in \mathcal{P}_{\text{avail}}} \hat{T}_i(t), \qquad \hat{T}_i(t) = \hat{R}_i^{\text{ext}}(t) + \frac{b_i^{\text{own}}(t) + q_i}{\hat{r}_i},$$
   where $\hat{R}_i^{\text{ext}}(t)$ measures remote/external queuing residual by subtracting the sender's own committed in-flight bytes (`ResidualSubtractOwn=true`) and decaying across idle gaps with half-life $t_{1/2} = 150\,\mu\text{s}$ (`ResidualIdleHalfLife=150us`, enhanced by `SilenceAwareResidual=true`). Then for every post-adaptation phase $k$ whose ideal fault-aware makespan is $L_k \gg \tau_q$:
   $$\frac{D_k - L_k}{L_k} \;\le\; \frac{\tau_q}{L_k} + O(\varepsilon_{\text{rate}}),$$
   meaning per-phase lock-step tail inflation collapses from **persistent per-phase regeneration** to a **single one-off transition transient** right after $t_f$.

### Proof Sketch

In synchronized collective barriers, a single slowest straggler segment committed to $p^*$ at time $t$ delays barrier completion until $t + \text{backlog}(p^*)/(\beta R_0) + q(p^*)/(\beta R_0)$. Under fixed-byte quanta ($q_i \equiv q_0$), dispatching one extra segment onto $p^*$ adds $q_0/(\beta R_0) = 4 \cdot (q_0/R_0)$ drain time at $\beta = 0.25$. Moreover, without subtracting local in-flight bytes (`ResidualSubtractOwn`), self-induced queue buildup misleads probes across compute gaps $g_k$ (especially once rate memory goes stale when a phase goes idle for $g_k \in [2, 20]\,\mu\text{s}$).

By sizing WQE segment emission proportionally to measured service rate ($q_i \propto \hat{r}_i \tau_q$, enforced in [ub-transaction.cc](file:///home/cassius/ub/ns-3-ub/src/unified-bus/model/protocol/ub-transaction.cc) under `TimeQuantumSeg` + `ServiceTimeScore`) and resetting/decaying stale silence priors (`SilenceAwareResidual` + `ResidualIdleHalfLife`), every active port receives work whose physical drain duration equals approximately one common time quantum $\tau_q$. Because the potential function $\Phi(t) = \max_i T_i(t) - \min_j T_j(t)$ increases by at most $\tau_q$ per dispatch onto a strictly minimum-finish-time port $\arg\min_j T_j(t)$, greedy water-filling maintains $\Phi(t) \le \tau_q + \text{tail\_slack}$ at phase end. Hence every subsequent barrier phase finishes within $\tau_q + O(\text{RTT})$ of its fault-aware capacity lower bound $L_k$.

### Empirical Corroboration (Round 19 Unseen Seeds `15,16`)

From `e1_phaseinfl_summary.csv` across `34` multi-phase `F3far` cases per scheme:
- **`flex+spraycc` (v7)**: **`34 / 34` `one-off` (`0` `per-phase`)**, median post-fault excess **`1.62%`**, worst max excess **`6.75%`** (`< 10%` criterion across all 34 cases). (`flex+spraycc+v6`: `22 / 22 one-off`, mean median `1.34%`, max `3.51%`).
- **Baselines (`F3far`)**: `sird+spraycc` **`13/34` `per-phase`** (max `+28.46%`), `reps+spraycc` **`12/34` `per-phase`** (max `+28.46%`), `ecmp+dcqcn` **`11/34` `per-phase`** (max `+49.84%`), `mprdma+spraycc` **`8/28` `per-phase`** (max `+26.35%`).

---

## 3. Theorem 2 — Work-Conserving Reachability-Aware READ Pooling & Non-Subsidy Invariant

### Statement

For receiver-driven RDMA READ flows with endpoint READ response return budget $C_{\text{tot}}$ divided across usable ports $i \in \mathcal{P}_{\text{live}}$ (`ReadPortBudgetBytes=0`, dynamically driven by `ReadPortCommitDelay=8us`, `ReadScoreReturnRate=true`, `ReadBudgetScaleByRate=true`, `ReadBudgetRepoolDead=true`, `ReadBudgetWorkConserving=true`, `ReadRequestVl=1`, `CcExemptReadResponses=true`):

1. **(Work-Conservation under Hard Failure / Isolation)** When dead/isolated planes $\mathcal{D} \subset \mathcal{P}$ become unreachable (`tp->IsDead()` or port isolated), reallocating their budget shares strictly across reachable live planes $\mathcal{U} = \mathcal{P} \setminus \mathcal{D}$ in proportion to return-path delivery rate $\hat{r}_i^{\text{ret}}$ yields:
   $$\sum_{i \in \mathcal{U}} C'_i = C_{\text{tot}}, \qquad \frac{C'_i}{\hat{r}_i^{\text{ret}}} = \frac{C_{\text{tot}}}{\sum_{j \in \mathcal{U}} \hat{r}_j^{\text{ret}}} \quad \forall i \in \mathcal{U},$$
   eliminating artificial credit starvation of healthy READ planes when an orthogonal plane dies.

2. **(Strict Non-Subsidy under Lane Degradation)** When plane $p^*$ experiences lane degradation ($0 < \beta_{p^*} < 1$) while remaining alive, `ReadBudgetRepoolDead` does **not** shift dead credit onto $p^*$, and `ReadBudgetScaleByRate` scales $p^*$'s outstanding READ credit proportionally to $\hat{r}_{p^*}^{\text{ret}} / R_0 \approx \beta_{p^*}$ while capping `ReadRequestVl=1` steering by ingress ECN mark (`ReadIngressMarkWeight=0.3`). Thus:
   - The in-flight return backlog occupying degraded port $p^*$ is strictly bounded by $O(\beta_{p^*} B_{\text{base}})$, and
   - Slow degraded ports never capture healthy planes' READ budget (preventing a slow responder from monopolizing host READ pipelining or stalling fast planes behind head-of-line queue build-up).

### Implementation Mapping & Round 19 Verification

Implemented in [ub-transport.cc](file:///home/cassius/ub/ns-3-ub/src/unified-bus/model/protocol/ub-transport.cc) and [ub-transaction.cc](file:///home/cassius/ub/ns-3-ub/src/unified-bus/model/protocol/ub-transaction.cc). Verified in Round 19 `e1_losstable_v7_lt.md` across all I/O and mixed read/write stress tests (`d2dread`, `a2aread`, `mixedread`, `d2io`, `d2ioread`): `mixedread` & `d2dread` & `a2aread` achieve `tdev < 1/N_dev` across all `F1far`, `F3far`, `F3far2`, `F3far3`, and `H1far` cases, and `e1_lg_v7_*` completes `314/314` runs with `bad_rc=0, bad_miss=0`.

---

## 4. Corollary 3 — Bounded Single-Plane Loss, Mesh Irreducibility, Fencing Safety & Bidirectional Peer-Recycle Liveness

### Corollary 3a (Bounded Single-Plane Loss $< 1/N_{\text{dev}}$)

Under Theorem 1 and Theorem 2 on a non-oversubscribed symmetric Clos topology (`ub_pod`, `cm_pod`) where each endpoint uses $N_{\text{dev}}$ equal planes and at most one plane degrades ($\beta \in [0.25, 1)$) or fails cleanly after fast detection/drain, the steady-state sustainable endpoint throughput reduction relative to healthy capacity is bounded by at most $1/N_{\text{dev}}$ (or $(1-\beta)/N_{\text{dev}}$ for lane degradation). Once initial pre-detection queued bytes drain (within $t_{\text{dev}}$), post-rebalance completion loss satisfies:
$$\text{loss}_{\text{tdev}} < \frac{1}{N_{\text{dev}}},$$
subject to the granularity margin of residual tail quanta.

*Empirical validation (`e1_losstable_v7_lt.md` & `r19_summary.json`)*:
- Across **77 v7 LossMatrix `lane` cells**: **`76/77` (`N_dev`)**, **`77/77` (`N_scope`, `0 phys, 0 impl`)** in `tdev` (and `76/77 <= 1/N_dev`, `77/77 <= 1/N_scope`).
- Across **`ub_pod` SOTA blind matrix** (`tdev < 1/N_dev`): `flex+spraycc` achieves **`11/11` `graceful`**, **`11/11` `lane`** (incl. **`11/11` under strict `1/(4N)`**!), and **`10/11` `hard`** (with `0` missing tasks).

### Corollary 3b (Physical Cut Irreducibility & LP-Proven `phys` Lower Bounds on `ub_mesh` / I/O)

No transport or load-balancing policy can drop completion loss below the time-indexed multi-commodity fluid LP bound $\text{LP}_{\text{lb}}$ returned by [flexspray_analyze.py](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/flexspray_analyze.py). On topologies lacking global $N_{\text{dev}}$-way symmetric path diversity—specifically `ub_mesh` (`arXiv:2503.20377`) where 6 of 10 NPU ports are **1-hop direct intra/cross-board links**—killing one direct link removes $100\%$ of direct 1-hop capacity for that neighbor pair (or forces multi-hop detour over shared LRS planes), causing $\text{LP}_{\text{lb}} \ge 1/N_{\text{dev}}$ (**LP-proven physically infeasible**, marked `phys`) regardless of congestion control or spraying granularity. Similarly, `h2d` with `F4` (whole-server isolation) or 2-plane `SSD` isolation (`H1ssd`) imposes $\text{LP}_{\text{lb}} = 17.4\%\text{–}40.0\% > 1/N_{\text{dev}}$ (`phys`).

*Empirical check (`r19_summary.json`)*: In `ub_mesh` `hard` (`H1far`), `a2a H1far` yields `lp_lb = 0.1419..0.1978 >= 1/10` (`phys`), whereas on `lane` (`F3far`, 1/4 rate) `flex+spraycc` still achieves **`4/4` `N_dev` (`100%`)** vs `reps 3/4`, `sird 3/4`, `hpn 3/4`, `ecmp 2/4`.

### Corollary 3c (Generation-Epoch Fencing Safety & Bidirectional Peer-Recycle Liveness under Flapping)

Let each transport channel maintain monotonic transmit/receive epoch counters `(m_txGen, m_rxGen)` alongside explicit hard-fail isolation (`m_dead`, `m_inboundFenced`) and per-packet `UbReplayFenceTag` (`4 B`), managed by [ub-controller.cc](file:///home/cassius/ub/ns-3-ub/src/unified-bus/model/ub-controller.cc) (`RecycleDeadTp()`) and [ub-transport.cc](file:///home/cassius/ub/ns-3-ub/src/unified-bus/model/protocol/ub-transport.cc) (`AcceptNewInboundGeneration()`, `RecycleToGeneration()`, `GetNextPacket()`, `IsLimited()`):

1. **(Stale Execution Safety / P5)**: Every segment retransmitted after failure fencing carries a post-fence tag matched by `m_rxGen`; late pre-fence replicas arriving after epoch advance are fenced and dropped without corrupting WQE state, proving zero duplicate/late execution (`P5=True` in `flexspray_regress.py`, vs `late_execs=9` when fencing is disabled).
2. **(Deadlock-Free Bidirectional Peer-Recycle Liveness under `UP` / `F3flap`)**: Consider peers $A \leftrightarrow B$ exchanging bidirectional `URMA_WRITE` where link `DOWN` precedes `UP`. Under asymmetric timing (`PortCommitDelay=6us`, `RecycleAfterLinkUp=true`), peer $A$ may receive an `UP` notification and recycle `tp` to generation $g+1$ before peer $B$ finishes draining in-flight ACKs (`TAACK` / `READ_RESPONSE`) on `m_ackQ` or while $B$'s `{DIRECT_TP, owner}` response queue is parked awaiting wakeup:
   - Calling `m_transaction->TryContinueJettyTransmission(tp)` immediately after `tp->RecycleToGeneration(newGen)` in `UbController::RecycleDeadTp()` guarantees parked `{DIRECT_TP, tp->GetLifecycleOwner()}` response queues cannot remain starved after epoch reset.
   - Allowing `m_ackQ` draining when `m_dead && m_recycleAfterLinkUp && !m_inboundFenced && m_rxGen > 0` in `GetNextPacket()` / `IsLimited()`, while flushing `m_ackQ` cleanly inside `AcceptNewInboundGeneration()` (and guarding `RecycleToGeneration()`'s `m_ackQ` flush with `if (m_rxGen == 0 || m_inboundFenced)`), ensures bidirectional ACKs never get silently dropped or blocked behind forward-data fencing across `DOWN -> UP -> DOWN -> UP`.

*Empirical validation*:
- C++ regression `UbFlexSprayHardDownRecycleTest` in [ub-test.cc](file:///home/cassius/ub/ns-3-ub/src/unified-bus/test/ub-test.cc) verifies bidirectional `0 <-> 1` `URMA_WRITE` under dual-cycle flapping completes `completed == 4u` with `RECYCLE gen=2`.
- Round 19 E2 dynamic fault suite: `e2_dyn_ubpod` (`180/180`, `bad_rc=0, bad_miss=0`) reaches **`0.8337`** on `F3flap` (`+1.90 pp` vs `v6` `0.8147`) and **`0.8470`** on `H1heal` (`+3.82 pp` vs `v6` `0.8088`); `e2_dyn_cmpod` (`64/64`) reaches **`0.8055`** on `F3flap` (`+4.93 pp` vs `v6` `0.7562`) and **`0.8236`** on `H1heal` (`+7.58 pp` vs `v6` `0.7478`).

---

## 5. Dimensionless RTT/Rate Scaling & High-Priority Control Tail Isolation

Fixed microsecond thresholds ($\Delta_{\text{commit}} = 6\,\mu\text{s}$, $T_{\text{probe}} = 25\,\mu\text{s}$ in `flex+spraycc+v6`) either over-commit bytes at `800 Gbps` / short RTT or under-probe prematurely at `100 Gbps` / `10x` fabric RTT (`1000 ns`). FlexSpray v7 parameterizes commitment windows dimensionlessly relative to tracked path SRTT (`CommitDelayRttFactor=4.0`, `ProbeTimeoutSrttFactor=2.5`) while pairing `SilenceAwareResidual=true` + `ResetRateOnPortUpgrade=true` + `RecoverFastOnUnmarked=true` for rapid healing recovery (`F3heal`), and isolating short high-priority/control messages (`BypassCommitForHighPrio=true`) around bulk-port commitment stalls. Across Round 19 `H1far` experiments:
- **`ub_pod` (`a1`/`a2`)**: `ctrl_p99_us` drops from **`13.82–13.89 us`** (`v6`) to **`1.84–1.85 us`** (`v7`, **`7.5x` reduction**, matching healthy `F0` `1.84–1.85 us`);
- **Rate / RTT sweeps (`E5`)**: at `100G` (`4.12 vs 8.56 us`, max `4.26 vs 27.19 us`), `800G` (`1.47 vs 7.47 us`, max `1.49 vs 25.53 us`), `5x RTT` (`3.47 vs 7.52 us`, max `3.52 vs 27.71 us`), and `10x RTT` (`5.46 vs 10.88 us`, max `5.51 vs 29.72 us`), while improving `10x RTT` efficiency by **`+2.08 pp`** (`0.7671 vs 0.7463`) without re-tuning.
