# FlexSpray Todo & Roadmap（Round 19 / NSDI 收口）

## 已完成里程碑（Stages 0–4）

- [x] **Stage 0–1（v6 + v7 C++ 内核与双向对等代际活性修复）**：完成 Time-Quantum Commitment、Reachability-Aware READ Pooling、无量纲 SRTT 自适应门限（`CommitDelayRttFactor=4.0`, `ProbeTimeoutSrttFactor=2.5`）、高优控制绕过（`BypassCommitForHighPrio`）与双向 `0 <-> 1` `UP -> DOWN -> UP` 响应唤醒/ACK 排空修复（`UbController::RecycleDeadTp` / `UbTransportChannel`），冻结于 `r19nsdi` 并 **5/5 全通门禁**（17/17 FlexSpray 单测、Golden 25/25 SAME、50/50 全套件 PASS、`flexspray_regress.py` exit 0）。
- [x] **Stage 2（公开超节点架构、开源基准与 SimAI Trace）**：集成 `cm_pod`（CloudMatrix384 1/12）、`cm_pod_osub`（2.17:1 超分）、`ub_mesh`（1-hop 直连+LRS）、`aicb`/`DeepEP`/`NCCL`/`Mooncake` 与 Alibaba SimAI Hybrid MoE Trace（`b_realtrace`）。
- [x] **Stage 3（Round 19 E1–E6 全维度盲测，`seeds=15,16`）**：完成全部 22 个批次 `2,898` 次全仿真（1,076 次 `flex+spraycc*` **100% 零失败**）、CCMetrics、`PhaseInfl`、Clean `DAGBound`、规范/分层 `LossMatrix` 及統一汇总 [results/r19/r19_summary.json](results/r19/r19_summary.json)。
- [x] **Stage 4（理论证明、论文图表、全套文档与 GitHub 备份）**：完成 [THEORY.md](THEORY.md)（定理 1–2 + 推论 3）、[plot_nsdi_figs.py](plot_nsdi_figs.py)（6 组高质量矢量 PDF + PNG 图表并经人工逐张视觉核验）、全部文档更新及 `feat/flexspray` / `flexspray-v7-nsdi` 同步至远端 `github`。

## 后续可选学术与硬件拓展

- **更大尺度微秒长序列与硬件原型验证**：探索 `1,000+ NPU` 更长时间窗口的周期性检查点流控交互，并将 edge/host 侧 `O(P)`（`P<=10`）状态表映射到 FPGA/NIC 硬件微码原语完成时序实测。
- **动态路由拓扑感知精细 `ALPHA` 模型**：在 `flexspray_dagbound.py` 中为 1-hop `ub_mesh` 链路引入逐对物理卸载延迟表，替代统一 `ub_pod` 3 跳 `ALPHA_US` 常数（消除 `b_ring F0 ~1.04` 边界差异）。
