# FlexSpray v7 技术研究与系统评估报告（Round 19 / NSDI）

本报告汇总结算 **FlexSpray v7**（含 M1 时间量子承诺、M2 可达性感知 READ 重池化、M3 去自排队残差估计、以及 v7 无量纲 SRTT 自适应与双向对等代际恢复增强）的完整工程实现、五道冻结门禁验证结果与 `2,898` 次盲测实验（未见种子 `15, 16`）定量结论。形式化数学证明见 [THEORY.md](THEORY.md)，学术定位论证见 [RESEARCH.md](RESEARCH.md)。

---

## 1. 架构核心机制与 v7 关键突破

1. **M1–M3 调度与流量/传输协同（沿用自 v6 核心并在 v7 无量纲化）**：
   - **M1 时间量子分段**：`TimeQuantumSeg=true` 按实时测量成员交付速率动态分配 WQE 段字节量子，配合 `ReadQuantumBytes=8192` 与 `SlackTailOnly=true`（`QuantumTailBytes=49152`），避免按定长字节喷洒向 `1/4` 降速链路过度提交数据导致的多阶段尾锁步扩张。
   - **M2 可达性感知 READ 重池化**：`ReadPortBudgetBytes=0`（v7 启用 `ReadPortCommitDelay=8us` 随 `CommitDelayRttFactor=4.0` 缩放）结合 `ReadScoreReturnRate`、`ReadBudgetScaleByRate`、`ReadBudgetRepoolDead` 与 `ReadBudgetWorkConserving`，在硬故障/孤立时工作守恒重池化、在降速时按物理残余比例限额而不补贴慢口。
   - **M3 去自排队残差估计**：`ServiceTimeScore` + `ServiceResidual` + `ResidualSubtractOwn` + `ResidualIdleHalfLife=150us` + `DeliveryRateQueueAware` 剔除本地在途字节重复计数，并在计算间隙衰减陈旧队列。
2. **FlexSpray v7 鲁棒性与活性增强（Round 18–19）**：
   - **无量纲自标定（`CommitDelayRttFactor=4.0`, `ProbeTimeoutSrttFactor=2.5`）**：自动跟随路径 SRTT 缩放提交窗口与探测超时，摆脱固定 `6us`/`25us` 微秒定时器在 `100G–800G` 与 `1x–10x` RTT 下的失配。
   - **高优控制流隔离（`BypassCommitForHighPrio=true` on TXN & TP）**：阻止高优先级控制/同步报文被大流量端口承诺窗口阻塞，在 `H1far` 下将 `ctrl_p99_us` 压低 `3.4x–13.2x`。
   - **动态恢复与双向对等代际活性（`SilenceAwareResidual`, `ResetRateOnPortUpgrade`, `RecoverFastOnUnmarked`, `RecycleAfterLinkUp`, `UbController::RecycleDeadTp` 响应队列唤醒与 `m_ackQ` 代际安全排空）**：解决双向 `URMA_WRITE` 在非对称链路闪烁（`DOWN -> UP -> DOWN -> UP`）下的响应饿死与死锁隐患。

---

## 2. 冻结二进制 `r19nsdi` 正确性验证

| 检查项 | 结果 | 详情 |
|---|---|---|
| Release 编译与指纹核对 | **通过** | `libns3.44-unified-bus.so` & `libns3.44-unified-bus-test.so` 与 `build/lib/` SHA256 完全一致 |
| `unified-bus-flexspray` | **17 / 17 PASS** | `3.498 s`；含双向 `0 <-> 1` 双周期 Flap `UbFlexSprayHardDownRecycleTest` |
| Golden 逐位对照（`r19def3 vs s131`） | **25 / 25 SAME** | 零扰动原生 UB 默认行为 |
| 全量 UB 单元套件（`r19all3`） | **50 / 50 PASS** | `0` 失败、跳过或断言放宽 |
| `flexspray_regress.py` | **exit 0** | `fence_on 22/22` 全过；`fence_off P5=False (late_execs=9)`；`tail4k missing_tasks=0 eff=0.9404` |

---

## 3. Round 19 全实验定量结论（`2,898` 次盲测，`seeds=15,16`）

### 3.1 E1 多拓扑 SOTA 盲测（`ub_pod` / `cm_pod` / `cm_pod_osub` / `ub_mesh`）

- **全量总效（E1 `eff_mean`，失败记 `0.0`）**：`flex+spraycc` **`0.8181`**（`208/208`）、`flex+spraycc+v6` `0.8103`（`138/138`）、`reps+spraycc` `0.7530`（`208/208`）、`sird+spraycc` `0.7476`（`208/208`）、`mprdma+spraycc` `0.7274`（`178/178`）、`adaptive+caqm` `0.6178`（`86/96`）、`ecmp+dcqcn` `0.4806`（`178/208`）、`hpn+spraycc` `0.2816`（`192/208`）。
- **分拓扑表现**：`ub_pod` **`0.8067`**（vs `sird 0.7426, reps 0.7257, mprdma 0.7139`）；`cm_pod` **`0.8236`**；`cm_pod_osub`（2.17:1 超分）**`0.8029`**；`ub_mesh` **`0.8669`**（vs `reps 0.8329, sird 0.7971, hpn 0.3828`）。
- **Clean E1 Fault-Aware DAGBound（`66` 多阶段作业，丢任务记 `0.0`）**：`flex+spraycc` **`0.9319`**（`F0 0.9579 / F3far 0.9428 / H1far 0.8950`），领先 `reps 0.8815`、 `sird 0.8674`、`mprdma 0.8290`、`adaptive 0.5822`、`ecmp 0.5330`、`hpn 0.2491`。
- **LossMatrix 规范通过率（`tdev < 1/N_dev`）**：Full v7 扩展集 **`lane` `76/77` (`N_dev`), `77/77` (`N_scope`, `0 phys, 0 impl`)**、`graceful 47/51 (phys 4, impl 0)`、`hard 42/49 (phys 4, impl 3)`；SOTA 统一 `ub_pod` 格点达 **`graceful 11/11`、`lane 11/11`（含 `4N 11/11`）、`hard 10/11`**。
- **阶段尾膨胀（`F3far`，`34` 例）**：`flex+spraycc` **`34 / 34 one-off（0 per-phase）`**，中位超额均值 **`1.62%`**（最差 `6.75%`）；全部竞品均有严重每阶段膨胀（`sird 13/34`, `reps 12/34`, `ecmp 11/34`, `mprdma 8/28`, `adaptive 2/14`）。

### 3.2 E2–E5 扩展维度（动态、规模、流控、参数鲁棒与 SimAI Trace）

- **E2 动态故障（`244` runs）**：`e2_dyn_ubpod` 均效 **`0.8383`**（vs `v6 0.8256, mprdma 0.5980, sird 0.5440, reps 0.5102`），其中 `F3multi` **`0.8250`**、`F3up3` **`0.8367`**；`e2_dyn_cmpod` **`0.8128`**（`H1heal +7.58 pp`, `F3flap +4.93 pp` vs `v6`）。
- **E3 `cm_pod` 扩展至 128 NPU（`180` runs）**：`S=8`（`--scale 0.5`）`flex+spraycc` **`0.7314`**（`b_ring` DAGBound **`0.8545`**）；`S=16` `d2d/b_kv`（`--scale 0.5`）**`0.9343`**；`S=16 b_ring`（`--scale 0.25`）`6/6` 完成且 DAGBound **`0.8340`**（`F3far` 仍维持 `0.8633`，无退化）。
- **E4 流控体制（`192` runs）**：在 `CBFC`（`0.8923`）、`PFC`（`0.8923` vs `sird 0.7883`）、`Lossy+SACK 256KB`（`0.8001` vs `reps 0.7119`）与 `Lossy+SACK 128KB`（`0.7694`）下保持 **96/96 零丢包失败**。
- **E5 速率/时延与真实 SimAI Trace（`516` runs）**：不修改参数直接扫 `100G–800G` 与 `1x–10x` RTT，`flex+spraycc` 在 `100G/800G/5x/10x` 分别为 **`0.8895 / 0.8097 / 0.8441 / 0.7671`**，相比固定定时器 `v6` 控制尾延迟降幅达 **`3.4x–13.2x`**；在 Alibaba SimAI `b_realtrace` 上 `ub_pod`/`cm_pod` 均 `12/12` 完成（DAGBound **`0.7831` / `0.7949`**），`ecmp` 与 `adaptive` 在 `H1far` 与 `F3multi` 均丢数千任务。

---

## 4. 论文资产索引

- 理论证明：[THEORY.md](THEORY.md)
- 全部 Round 19 图表（Vector PDF + 300-DPI PNG）：`fig/nsdi/fig1_sota_overview.{pdf,png}` 至 `fig/nsdi/fig6_ccmetrics_tail.{pdf,png}`，由 [plot_nsdi_figs.py](plot_nsdi_figs.py) 直接读取 [results/r19/r19_summary.json](results/r19/r19_summary.json) 生成。
- 汇总表与原始指标：[results/r19/](results/r19/)（`e1_sota_summary.csv`, `e1_dagbound_summary.csv`, `e1_losstable_{v7,sota}_{lt,le}.md`, `e1_phaseinfl_summary.csv`, `e2_dynamic_summary.csv`, `e2_timeseries_a2a_s15.csv`, `e3_scale_summary.csv`, `e4_fc_summary.csv`, `e5_sensitivity_summary.csv`, `e5_realtrace_summary.csv`, `e1_e2_ccmetrics_summary.csv`, `e3_e5_ccmetrics_summary.csv`, `e3_e5_dagbound_summary.csv`, `r19_summary.json`）。
