# FlexSpray：UB 多平面 Packet-Spray 负载均衡与拥塞控制 —— 技术报告（v6 / 第十七轮）

**状态**：核心算法（FlexSpray v6，`r15_v6`）、公开超节点拓扑与开源 AI 负载适配（Stage 2）、主流 SOTA 对比策略与 `UbHostDcqcn` teardown 竞态修复（Stage 3）均已冻结验证（`r17cand2`）。所有评估严格在本地 Linux `ns-3-ub` 仿真器上完成。当前默认入口与简明结论见 [DELIVERY.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/DELIVERY.md)。

---

## 1. 问题定义与总体架构

针对异构超节点（NPU/CPU/NIC/SSD 按不同组合接入多 UB 平面）在 AI 训练/推理集合通信与 KVCache 存储读写混合流量下遇到的两大核心挑战：
1. **单通道/单 Lane 慢化长尾灾难**：当某一平面下行接口出现 Lane 降速（如 `4->1 lane` 的 `F3far3` / `F3ssd3`），传统按字节均分、反应式 ECN 拥塞避让（`REPS`）或单纯按包队列滞后剪枝（`MP-RDMA OOO`）都会因向慢口派发过量字节或误判排空能力，导致全组多阶段同步点周期性拖慢 `2x–6x`。
2. **硬断（`H1` / `H2` / `H4`）活性与无重复执行**：跨 TP 组重调度必须在无全局先知条件下完成故障检测与原子级重放围栏（`ReplayFence`），且不造成死口信用锁死或已完成段晚到重复写。

### FlexSpray v6 核心机制（五层协同）

1. **网络层与交换机极简支持**：Fabric 核心保持传统 ECMP/WCMP；Edge/L1 交换机启用 `UbSwitchSprayCc`，仅维护 `m_kDelay` 阈值与 per-port `int8_t m_deviceFacing` 标识，对局部出队排队超出 `KDelay` 的下行包执行 `1-bit` ECN 边缘标记（无需逐流表、无交换机 credit/deficit 阵列）；HardDown 时由 `UbLinkHealth` + T20 撤回 fabric 路由并本地快速失效回退。
2. **端侧速率耦合控制（`UbHostSprayCc`）**：以 `70 B` 带内标记 ACK（`UbCongestionExtTph = 8 B`）按周期调整发送速率与 `m_alpha`，并集成 **B1 自适应 RTO**（`RtoSrttFactor=8, RtoFloorFirstOnly=true`）与 `DeliveryRateQueueAware=true`，避免高排队降速队列被误判为 app-limited 或频繁超时。
3. **Time-Quantum Commitment（M1 + M3，v6 核心突破）**：
   - `UbTransaction::TimeQuantumSeg=true` + `ReadQuantumBytes=8192` + `QuantumTailBytes=49152` + `SlackTailOnly=true`：按各成员相对容量与实测速率分档切分 WRITE 量子与 READ 分段粒度，末段 `<= 48 KB` 时切换 `4 KB` 喷洒，避免大量子锁死慢平面尾包。
   - `PortCommitDelay=6us` + `ServiceTimeScore=true` + `ServiceResidual=true` + `ResidualSubtractOwn=true` + `ResidualIdleHalfLife=150us`：按 `(端口已准入未发字节/端口速率 + 在途净服务残差 + 本段耗时)` 进行 `MIN_DRAIN_TIME` 调度；残差扣减本 TP 自身在途串行化时间并跨越 MoE `10–20 µs` 计算间隙衰减（`150 µs` 半衰期），解决单流与间歇流量记忆丢失问题。
4. **Reachability-Aware Pooling & 公平份额（M2 + T29）**：
   - `ReadPortBudgetBytes=409600` + `ReadScoreReturnRate=true` + `ReadBudgetScaleByRate=true` + `ReadBudgetRepoolDead=true` + `ReadBudgetWorkConserving=true`：按局部端口实时返回速率动态缩放 READ 信用池，**仅在端口 `HardDown`/隔离时将预算按健康比例重池化**，绝不为降 Lane 慢口扩容；`TpGroupFairShare` 为异构 NPU/CPU/SSD 自动分配公平权重。
   - `DesyncTpgCursor=true`、`PrioritizeHeavyWqe=true`、`UbPort::TriggerTransmitCoalesced`（`CoalescePortTrigger=true`）消除同拍波次冲突与仲裁器饥饿。
5. **非先知故障检测与安全重派（`ReplayFence` + DCQCN 修复）**：`FailureSuspectAfter=12us` + `FailureProbeTimeout=25us`（复用保留 `TP_OPCODE_PROBE` / `PROBE_REPLY`）+ `UbReplayFenceTag`（`4 B`）；Stage 3 新增 `UbHostDcqcn::IsTpLifecycleActive()` / `m_tpDestroyRequested`，消除了 TP 退场时晚到 CNP 与事件销毁竞态。

---

## 2. 实验矩阵与评价方法

- **拓扑集**：`ub_pod`（经典异构 pod：NPU×8 平面 / CPU×4 平面 / NIC×2 / SSD×2，`400 Gbps × 4 lanes`）；Stage 2 新增 `cm_pod`（CloudMatrix384 1/12 缩比，7 平面 `4+4` 无阻塞，`arXiv:2506.12708`）、`cm_pod_osub`（`3+2` ~2.17:1 超分）、`ub_mesh`（2D-Mesh 1-hop 直连 + 4 轨 LRS，`arXiv:2503.20377`）。
- **负载集**：经典基准（`a2a, a2aread, mixed, b_moe`）+ 5 类开源 AI 负载（`b_aicb_tp_ep`, `b_deepep_skew`, `b_deepep_prefill`, `b_nccl_pipe`, `b_kv_skew`，见 [BENCHMARK.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/BENCHMARK.md)）。
- **公平对比协议**：在 `s1, s2` 独立网格调参（`tune_s12` `504` 次运行）锁定 `mprdma+dq2+ooo2`、`reps+spraycc`、`reps+dq2`、`hpn+caqm`、`adaptive+caqm+rtx+fd`、`ecmp+dq2+rtx+fd`，全部主结果仅在未见种子 `s15, s16` 上汇报，详见 [EVAL_PROTOCOL.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/EVAL_PROTOCOL.md)。

---

## 3. 核心实验数据总结（`r17cand2`，`s15, s16`）

### 3.1 效率与故障韧性总表

| 方案 | 主矩阵总均效 (`n=108`) | `F0` | `F3far3` | `H1far` | 丢任务 (`n=108`) | `kv_io F3ssd3` | `dag_r17_fa` (`n=54`) | `dag_r17_kv_fa F3ssd3` | `PhaseInfl F3far3` 非 KV 1-off (均值/最差) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **`flex+spraycc` (v6)** | **0.8271** | **0.8323** | **0.8206** | **0.8284** | **0** | **0.7654** | **0.9336** | **0.8780** | **16/16 (`3.24%` / `7.86%`)** |
| `mprdma+dq2+ooo2` | 0.6354 | 0.7497 | 0.3820 | 0.7747 | 0 | 0.4128 | 0.7926 | 0.4736 | 2/16 (`241.34%` / `582.69%`) |
| `adaptive+caqm+rtx+fd` | 0.5953 | 0.6718 | 0.4202 | 0.6938 | 0 | 0.4070 | 0.6610 | 0.4669 | 3/16 (`81.13%` / `198.28%`) |
| `reps+spraycc` | 0.5929 | 0.7374 | 0.3413 | 0.6999 | 2 | 0.3878 | 0.7979 | 0.4448 | 4/16 (`115.09%` / `238.62%`) |
| `reps+dq2` | 0.5311 | 0.7076 | 0.1533 | 0.7324 | 8 | 0.3702 | 0.7103 | 0.4246 | 2/16, 8 miss (`602.88%` / `1185.85%`) |
| `ecmp+dq2+rtx+fd` | 0.5099 | 0.6484 | 0.2128 | 0.6684 | 1 | 0.3830 | 0.6694 | 0.4393 | 2/16, 1 miss (`240.43%` / `425.90%`) |
| `hpn+caqm` | 0.2735 | 0.2846 | 0.2218 | 0.3142 | 0 | 0.3619 | 0.3471 | 0.4151 | 3/16 (`34.63%` / `85.86%`) |

### 3.2 LossMatrix `< 1/N`（全窗口与 4 种 `N` 见 FEASIBILITY §13.3）

- **主矩阵逐行（`n=68`/方案）**：`flex+spraycc` 达 **`55/68`**（`hard 26/34`, **`lane 29/34`**, `phys=4, impl=9`）；并入 `r17_kv_io` 联合（`n=72`）达 **`59/72`**（`hard 28/36`, **`lane 31/36`**），`r17_ub_pod` **`28/28`**、`r17_kv_io` **`4/4`** 全通过，而全部 6 个竞品在 `lane` 语义仅通过 **`1–2 / 36`**（`r17_kv_io F3ssd3` 全部 `0/2`）。
- **主矩阵逐格（`n=34`/方案）**：`flex+spraycc` 达 **`27/34`**（`hard 13/17`, **`lane 14/17`**）vs 最优竞品 `14/34`（`hard 14/17`, `lane 0–1/17`）。
- **Round 15 专项覆盖对比（vs B1）**：`lm_old_cov` `lane` 格从 `47/58` 升至 **`57/58`**（`N_scope 58/58`）；`lm_bench` `lane` 格从 `8/20` 升至 **`20/20`**；`phase_bench` 从 `91 one-off / 21 per-phase` 升至 **`112/112 one-off`**。

---

## 4. 硬件状态与数据面开销结论（T4 收口）

- **Fabric 无侵入**：不修改核心交换机路由与排队，无需中心仲裁，仅边侧 `UbSwitchSprayCc` 保存每口 1 字节方向标记及 `m_kDelay` 标量。
- **端侧可控状态**：端口维度 `O(P)`（`P <= 10`）结构与分发期 `O(P)` 最小排空比较，无 `UbHostCaqm` 式 5 张 per-PSN 哈希表或交换机逐口 4×64b 信用/赤字数组；`UbReplayFenceTag` 4 B 仅用于硬断重调度片段；`TP_OPCODE_PROBE`/`REPLY` 零额外包头扩容。

---

## 5. 局限性与未解边界

- **`H1far` 多屏障短阶段代价**：`r17_cm b_moe H1far`（`0.6937 vs mprdma 0.7244`）与 `r17_ub_pod b_moe H1far`（`0.6676 vs 0.6895`）反映 `PortCommitDelay=6us` + 25 µs probe 的 ~2 µs/phase 保守预算开销（同场景 `F3far3` 仍领先 `3.42x`）。
- **`ub_mesh` 物理不可行割与 `r17_kv_io s16`**：`ub_mesh a2a` 单跳直连边受损导致 `4` 处 LP-proven 物理不可行与 `6` 处绕行约束失败；`r17_kv_io s16 F3ssd3` 超出 10% `PhaseInfl` 线（`15.19%`，但竞品为 `69.75%–262.91%`）。
- 其余长尾/语义边界见 [TODO.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/TODO.md)（`T12, T15, T17, T18, T22, T23, T26, T29, T5, T7, T1-rest`）。
