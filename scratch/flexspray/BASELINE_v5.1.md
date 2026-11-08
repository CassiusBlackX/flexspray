# FlexSpray v5.1 基线记录（第四轮开始前冻结）

- **代码**：`git tag flexspray-v5.1-baseline` → `2dac7390`（实现代码为 `67471995`；`2dac7390` 只改了文档与图）。
  旧基线 `flexspray-v4-baseline`（`c954e44e`）保留不动。
- **构建**：`/home/cassius/ub/work/prof-tree` 为 static + LTO 的快速构建，与主树结果逐字节一致，见 golden 回归。
- **分析器版本**：`scratch/flexspray/flexspray_{analyze,portutil,portutil_table,summarize,report,ccmetrics,hardreport,timeseries,feasibility}.py`，均取自 `2dac7390`。
- **方案参数**：`/home/cassius/ub/work/baselines/v5.1/scheme_*.json`，由 `flexspray_case.scheme_attrs()` 导出，包含 FlexSpray 和全部对照方案的 ns-3 默认属性与 UB 全局量。
- **拓扑**：`flexspray_case.Topo`。4 台服务器 × 12 个设备（8×NPU 用 8 个平面、2×CPU 用 4 个平面、NIC 用平面 0–1、SSD 用平面 4–5），8 个平面，每个平面每台服务器一个 L1，4 个 L2，每对 L1–L2 有 3 条链路（`--links-per-pair 2` 时为 8 上行 / 11 下行），400G，主机链路 20 ns，fabric 链路 100 ns，交换机转发 100 ns。
- **流量**：`flexspray_case.py` 中的 SCENARIOS。包括 d2d、d2dread、h2d、a2a、mixed、a2aread、mixedread、stagger、staggermixed、incast；流大小为设计速率 × 160 µs。
- **故障**：FAULTS（F0、F1near/far、F2up、F3near/far、F4、F1ssd、F3ssd、F1nic、F3nic、F4p4，以及硬断链 H*），故障时刻 t_f = 50 µs，`--stop-ms 3`。
- **种子**：调参用种子 1、2；已看过的留出集为种子 3、4 和 `--map 1`。从第四轮起，它们**不再算作盲测**。
- **原始结果**（复制件，每个目录内是 results*.jsonl）：`/home/cassius/ub/work/baselines/v5.1/`

| 目录 | 内容 |
|---|---|
| r9_core | v5.1：7 个场景 × 7 种优雅故障 × 种子 1–4 |
| r9_cov | v5.1：SSD/NIC 平面故障；stagger、staggermixed、incast |
| r9_osub | v5.1：上行收敛 fabric，含关闭速率估计保护（noalg）的消融 |
| r9_map | v5.1：`--map 1` |
| r10_hard, r10_hardabl, r10_ts | v5.1：硬断链、硬断链消融、时间序列 |
| r10_buf, r10_neg, r10_abl, r10_check | v5.1：浅缓冲、误判反例、消融、修复前后一致性核对 |
| r8_hard, r8_hardabl | 对照方案（开启 GBN 重传）的硬断链结果 |
| final_merged, r2_*, r3_* | 对照方案在优雅故障、新场景、上行收敛 fabric 和浅缓冲下的结果 |
| hard8.json, hard10.json | `flexspray_hardreport.py` 的汇总 |

- **完整复现入口**：`/home/cassius/ub/work/bin/round9.sh` 和 `round10.sh`（v5.1）；对照方案见 `round2.sh`、`round3.sh` 和 `round8.sh` 中的硬断链部分。
