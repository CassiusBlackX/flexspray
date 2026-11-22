# FlexSpray：面向 UB 多平面 AI 集群的无锁定容错负载均衡与传输子系统（v7 / NSDI 版）

本目录集成 **FlexSpray v7**（含 Time-Quantum Commitment、Reachability-Aware READ Pooling、无量纲 SRTT 自适应门限、高优控制绕过及双向对等代际回收机制）的全部可复现试验工具链、公开超节点拓扑、开源 AI 集合通信基准、真实 Alibaba SimAI Trace、形式化理论证明与 Round 19 盲测（未见种子 `15, 16`，总计 `2,898` 次全仿真运行）结果资产。

## 1. 核心文档导航

| 文档 | 内容摘要 |
|---|---|
| [THEORY.md](THEORY.md) | **定理 1（时间量子承诺与每阶段尾膨胀上界）、定理 2（工作守恒 READ 重池化与非补贴不变量）、推论 3（单平面损失上界 / `ub_mesh` 物理不可约界 / 代际屏障安全与双向活性）** 及与 C++ 符号的一一映射 |
| [RESEARCH.md](RESEARCH.md) | 论文问题陈述、Motivation、M1–M3 + v7 系统设计、Round 19 E1–E6 全维度盲测结果与 NSDI 评估闭环 |
| [REPORT.md](REPORT.md) | 工程与科研完整技术报告（含 `r19nsdi` 五道门禁与 6 张论文图表索引） |
| [DELIVERY.md](DELIVERY.md) | 甲方规范与 NSDI 交付验收清单（逐项对应 `r19nsdi` 门禁、`LossMatrix`、`PhaseInfl`、`DAGBound`、`CCMetrics`） |
| [BENCHMARK.md](BENCHMARK.md) | 12 合成基元负载 + 4 多阶段基准 + 3 文献超节点拓扑（`ub_pod` / `cm_pod` / `cm_pod_osub` / `ub_mesh`）+ 5 开源 AI 负载 + Alibaba SimAI `b_realtrace` 说明 |
| [EVAL_PROTOCOL.md](EVAL_PROTOCOL.md) | 统计口径规范（`tdev` / `steady` / `trans`、4 种 `N`、`<` 与 `≤` 并列、`LP phys` 界、动态与灵敏度盲测协议） |
| [KNOBS.md](KNOBS.md) | `flex+spraycc`（v7）、`flex+spraycc+v6`、竞品基线及流控体制全部开关参数字典与默认状态说明 |
| [FEASIBILITY.md](FEASIBILITY.md) | 从 Round 1 到 Round 19 完整多轮演化实验日志与消融归因 |
| [TODO.md](TODO.md) | 阶段 0–4 执行状态总表与后续硬件/更大集群延展方向 |

## 2. 目录结构与关键工具

- **场景与拓扑生成**：[flexspray_case.py](flexspray_case.py)、[flexspray_topo_public.py](flexspray_topo_public.py)、[flexspray_bench.py](flexspray_bench.py)、[flexspray_aicb.py](flexspray_aicb.py)
- **真实 Trace 资产**：[traces/](traces/README.md)（Alibaba SimAI `HYBRID_TRANSFORMER_FWD_IN_BCKWD` 归一化 18 阶段 `real_collective_trace.csv`）
- **分析与验证套件**：[flexspray_analyze.py](flexspray_analyze.py)、[flexspray_lossmatrix.py](flexspray_lossmatrix.py)、[flexspray_losstable.py](flexspray_losstable.py)、[flexspray_phaseinfl.py](flexspray_phaseinfl.py)、[flexspray_dagbound.py](flexspray_dagbound.py)、[flexspray_ccmetrics.py](flexspray_ccmetrics.py)、[flexspray_timeseries.py](flexspray_timeseries.py)、[flexspray_regress.py](flexspray_regress.py)
- **Round 19 汇总数据与画图脚本**：[results/r19/r19_summary.json](results/r19/r19_summary.json)、各实验紧凑 CSV（`results/r19/*.csv`）、[plot_nsdi_figs.py](plot_nsdi_figs.py) 及生成的 6 组双栏矢量图 [fig/nsdi/](fig/nsdi/)

## 3. 快速验证与复现命令

```bash
# 1. Release 编译与 17 项 FlexSpray C++ 专项单元测试
cd /home/cassius/ub/ns-3-ub
./ns3 build ub-quick-example unified-bus-test test-runner
/home/cassius/ub/work/frozen/r19nsdi/testrunner --test-name=unified-bus-flexspray --verbose

# 2. 系统级恢复、重派屏障 SAFETY 与 4KB 尾部稳定性回归
python3 scratch/flexspray/flexspray_regress.py --bin /home/cassius/ub/work/frozen/r19nsdi/sim

# 3. 复现单个盲测算例（如 CloudMatrix 缩比 Pod 上的 Alibaba SimAI MoE Trace + F3multi 复合故障）
python3 scratch/flexspray/flexspray_case.py /tmp/demo_rt --topo cm_pod --scenario b_realtrace --fault F3multi --scheme flex+spraycc --seed 15
/home/cassius/ub/work/frozen/r19nsdi/sim --case-path=/tmp/demo_rt --stop-ms=4

# 4. 由 results/r19/ 一键重绘全套 6 张 NSDI PDF/PNG 图表
MPLCONFIGDIR=/tmp/mplconfig python3 scratch/flexspray/plot_nsdi_figs.py
```

## 4. 数据面状态与硬件开销摘要

- **核心 Fabric 交换机**：零修改、零逐流状态（对比 CAQM 全网每口 4×64b 信用/赤字阵列及定时器）。
- **Edge 交换机（`UbSwitchSprayCc`）**：仅 `m_kDelay` (8 B) + 每口 1 B 设备侧标识 + 合批触发标记。
- **端侧复杂度**：每本地口 `O(P)`（`P <= 10`）常数标量状态，分发路径对 `<= 10` 个活跃平面做 `O(P)` 最小值归约；无需固定 `52 B` REPS 环形缓存或 5 张 per-PSN 哈希表。
