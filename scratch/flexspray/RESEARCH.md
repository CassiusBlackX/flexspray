# FlexSpray：面向多平面 AI 超节点集群的端网协同容错负载均衡研究（NSDI 评估档案）

**核心问题**：在多平面 UnifiedBus（UB）与超节点集群（32–128 NPU、`ub_pod`、`cm_pod`、`cm_pod_osub`、`ub_mesh`）中，单条链路的 **非对称 Lane 降速**（如 400G -> 100G `1/4` 降速）与 **静默硬断/动态闪烁**（`H1*`, `F3heal`, `F3flap`, `F3multi`, `H1heal`）会在分布式训练/推理的同步屏障处引发 **每阶段尾锁步雪崩（Per-Phase Lock-Step Tail Inflation）**：现有无中心多路径方案（ECMP+DCQCN、HPN、REPS、SIRD、MP-RDMA、Adaptive+CAQM）在静态按字节喷洒、陈旧 ECN/RTT 滞后或盲目补贴慢口的影响下，每阶段额外拖延最高达 **`+49.84%`**，在真实 Alibaba SimAI 混合并发 Trace 与复合故障（`F3multi`）下效率跌至 **`0.108–0.162`**，或在无替代路径的 `H1far`/`F3multi` 下大面积超时丢任务。

---

## 1. 系统设计三大支柱与 v7 无量纲增强

1. **M1 速率比例时间量子承诺（Time-Quantum Commitment, `UbTransaction`）**：
   - 将固定字节量子切换为与端口实测交付速率成正比的 **时间量子分段**（`TimeQuantumSeg=true`, `ReadQuantumBytes=8192`, `QuantumTailBytes=49152`, `SlackTailOnly=true`），外加 `DesyncTpgCursor` + `PrioritizeHeavyWqe` 打散跨机碰撞。
2. **M2 可达性感知 READ 重池化与非补贴不变量（Work-Conserving READ Pooling, `UbTransportChannel`）**：
   - 硬故障（`HardDown`/隔离）触发 `ReadBudgetRepoolDead + ReadBudgetWorkConserving` 按可用口返程速率守恒重分配信用；降速但不致死的慢口严格由 `ReadBudgetScaleByRate` 等比缩限而不参与重池，杜绝慢口通过 receiver-driven READ 反向补贴拖垮健康平面。
3. **M3 去自排队残差估计与 v7 无量纲自适应/对等活性（`UbTransportChannel` / `UbController` / `UbHostSprayCc`）**：
   - `ServiceTimeScore + ServiceResidual + ResidualSubtractOwn + ResidualIdleHalfLife=150us + SilenceAwareResidual` 在 `2–20 us` MoE 计算间隙平稳保持远端残余排队记忆；
   - **无量纲缩放（v7）**：`CommitDelayRttFactor=4.0`, `ProbeTimeoutSrttFactor=2.5` 自动适配 `100G–800G` 与 `1x–10x` RTT；
   - **控制面隔离（v7）**：`BypassCommitForHighPrio=true` 保护控制面/barrier 消息，在 `H1far` 降低 `ctrl_p99` 尾延 `3.4x–13.2x`；
   - **双向对等代际活性（v7）**：`RecycleAfterLinkUp=true` + `UbController::RecycleDeadTp()` 调用 `TryContinueJettyTransmission(tp)` 唤醒响应队列 + `GetNextPacket()`/`IsLimited()` 代际安全排空 `m_ackQ`，消除 `DOWN <-> UP` 闪烁期间的双向死锁。

---

## 2. 理论分析摘要（详见 [THEORY.md](THEORY.md)）

- **定理 1（时间量子承诺尾膨胀上界）**：证明静态字节喷洒每阶段遭遇 $\Omega(1/(\beta P) - 1/P)$ 锁步拖尾，而 M1+M3 贪心最小完成时间调度在适应后将每阶段超额约束在 $O(\tau_q / L_k) \ll 10\%$（One-off 一次性瞬态）。
- **定理 2（READ 信用工作守恒与非补贴不变量）**：证明死口重池维持 $\sum_{i \in \mathcal{U}} C'_i = C_{\text{tot}}$ 且均衡返回排空时间，而降速口按 $\beta$ 限额阻断慢口排队外溢。
- **推论 3（单平面损失 $< 1/N_{\text{dev}}$、网格不可约物理界与双向活性）**：确立 Clos 对称拓扑下损失上界、解释 `ub_mesh` 1-hop 直连口 `lp_lb >= 1/N_dev` (`phys`) 的物理切容量根因，并证明 `UbReplayFenceTag` 安全性（`P5=True` vs 关闭时 `9` 次迟到执行）与 `DOWN <-> UP` 双向活性。

---

## 3. Round 19 全面盲测评估结论（`r19nsdi`, Seeds `15, 16`, 共 `2,898` 次全仿真）

> 参数严格沿用既往开发集锁定配置，零针对未见种子 `15, 16` 或 E2–E5 新拓扑/流控/Trace 调参；全部图件由 [plot_nsdi_figs.py](plot_nsdi_figs.py) 从 [results/r19/r19_summary.json](results/r19/r19_summary.json) 自动生成。

| 评估维度 | 核心对照结果（均来自 `results/r19/`） | 对应图表 |
|---|---|---|
| **E1 多拓扑 SOTA 盲测** (`1,816` runs) | `flex+spraycc` 总均效 **`0.8181`**（`208/208` 零丢任务）：`ub_pod` **`0.8067`**、`cm_pod` **`0.8236`**、`cm_pod_osub` **`0.8029`**、`ub_mesh` **`0.8669`**；对比 `reps` `0.7530`、`sird` `0.7476`、`mprdma` `0.7274`、`adaptive` `0.6178 (86/96)`、`ecmp` `0.4806 (178/208)`、`hpn` `0.2816`；Clean DAGBound **`0.9319`**（`66/66`）vs `reps 0.8815 / sird 0.8674 / mprdma 0.8290` | [fig1_sota_overview.png](fig/nsdi/fig1_sota_overview.png) |
| **LossMatrix 与 PhaseInfl** (`77` v7 格点 / `34` `F3far` 例) | v7 扩展集 `lane` **`76/77` (`N_dev`) / `77/77` (`N_scope`, `0 phys, 0 impl`)**、`graceful` `47/51 (impl=0)`、`hard` `42/49`；分层 E1 SOTA `lane` **`25/25` (`100%`)**；`F3far` 阶段膨胀 **`34/34` `one-off`（`0 per-phase`）**，中位超额 `1.62%`（最大 `6.75%`），而竞品 `sird/reps/ecmp/mprdma` 分别高达 `13/12/11/8` 例每阶段拖尾（最大 `49.84%`） | [fig2_loss_phaseinfl.png](fig/nsdi/fig2_loss_phaseinfl.png) |
| **E2 动态与复合故障** (`244` runs) | `e2_dyn_ubpod` **`0.8383`**（`F3multi 0.8250` vs `mprdma 0.4017, sird 0.2841, reps 0.1887`；`F3up3 0.8367`；`H1heal 0.8470 vs v6 0.8088`）；`e2_dyn_cmpod` **`0.8128`**（`H1heal 0.8236 vs v6 0.7478`, **`+7.58 pp`**；`F3flap 0.8055 vs v6 0.7562`, **`+4.93 pp`**） | [fig3_dynamic_timeseries.png](fig/nsdi/fig3_dynamic_timeseries.png) |
| **E3 规模扩展** (`180` runs, `S=8,16`) | `cm_pod` 扩至 `64 NPU`（`--scale 0.5`）`flex+spraycc` **`18/18` (`0.7314`, `b_ring` DAGBound `0.8545`)**；扩至 `128 NPU` `d2d/b_kv`（`--scale 0.5`）`12/12` **`0.9343`**；`b_ring`（`--scale 0.25`）`6/6` 且 DAGBound **`0.8340`**（`F3far 0.8633` 零退化） | [fig4_scale_fc.png](fig/nsdi/fig4_scale_fc.png) |
| **E4 流控体制** (`192` runs) | 零重调参通过 `CBFC`（**`0.8923`**）、`PFC`（**`0.8923`** vs `sird 0.7883` / `ecmp 0.5493`）、`Lossy+SACK 256KB`（**`0.8001`** vs `reps 0.7119`）与 `128KB`（**`0.7694`**），96/96 零丢任务 | [fig4_scale_fc.png](fig/nsdi/fig4_scale_fc.png) |
| **E5 敏感性与 Alibaba SimAI Trace** (`516` runs) | `100G` **`0.8895`**（`18/18` vs `reps/sird 17/18`, `ecmp 16/18`）、`800G` **`0.8097`**、`5x RTT` **`0.8441`**、`10x RTT` **`0.7671`**（超 `v6` `+2.08 pp`）；SimAI `b_realtrace` 在 `ub_pod` / `cm_pod` 获 DAGBound **`0.7831`** / **`0.7949`**（`24/24`），竞品 `adaptive/ecmp` 于 `H1far` 与 `F3multi` 大面积失败 | [fig5_sensitivity_realtrace.png](fig/nsdi/fig5_sensitivity_realtrace.png) |
| **E6 传输健康与控制面隔离** (`2,898` 汇总) | `H1far` `ctrl_p99` 从 `v6` `13.8–21.7 us` 降至 **`1.47–5.45 us`**（**`3.4x–13.2x`**）；E2 峰值交换机队列 `370.7 KB`（对比 `reps 2574.2 KB` 降 **`6.94x`**, `mprdma 1364.2 KB`），乱序跨度 `<= 20` 包（对比 `151–175`） | [fig6_ccmetrics_tail.png](fig/nsdi/fig6_ccmetrics_tail.png) |

---

## 4. 诚实工程边界与未来方向

1. **`ub_mesh` 非对称 1-hop 物理 cut 限制**：`ub_mesh` 含 6 条单跳直连口，`H1far` 下 `a2a` 断直连边存在 `LP`-证明的物理下界（`phys=1`），且混合跨板迂回令 `hard` 通过 `1/4`（但在 `lane` 下 `flex+spraycc` 仍达 **`4/4` 100%**）。
2. **`b_moe H1far` 短阶段重启微小权衡**：在 Clos 拓扑（`ub_pod` / `cm_pod`）多屏障微阶段 `b_moe H1far` 上，v7 将差距收窄至与 `reps+spraycc` 相差 `-1.3 ~ -1.6 pp`（`0.6683 vs 0.6817`, `0.7017 vs 0.7182`），同时在 `cm_pod_osub`（`0.7006 vs 0.6927`）与 `ub_mesh`（`0.7974 vs 0.7417`）反超全部基线，并仍以较大优势领先 `mprdma+spraycc` 与 `sird+spraycc`。
3. **E3 规模实验载荷缩放与 `ub_mesh ALPHA_US` 注记**：`S=8`/`16` 为控制单批峰值内存与 4 ms 仿真壁钟而采用 `--scale 0.5`（`S=16 b_ring` 采用 `--scale 0.25`），`S=16` 128 机 `b_ring` 在极小切片下受限于多步串行固定时延，原始带宽效率为 `0.1053`（但 Fault-Aware DAGBound 达 `0.8340`, `F3far 0.8633`）；此外 `flexspray_dagbound.py` 的 `ALPHA_US=1.345us` 基于 3 跳 `ub_pod` 实测，故 1 跳直连 `ub_mesh b_ring F0` 出现 `B/T ~ 1.04` 属建模参数保守差异，统一在分析脚注中声明。
4. **评估范围限定**：全部数据源自周期精确的 Linux `ns-3-ub` Release 仿真，尚未在物理 FPGA/ASIC 与百毫秒级真集群上流片实测；数据面状态审计（边缘交换仅 8 B + 1 B/口、端侧每平面 `O(P)` `P<=10`）表明具备直接 RTL 映射可行性。
