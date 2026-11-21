# FlexSpray — 当前交付（默认配置 = v6；第十七轮 / Stage 3，2026-11-21）

本文件是当前仓库唯一对外交付入口。**最新冻结验证二进制为 `r17cand2`**（位于 `/home/cassius/ub/work/frozen/r17cand2/`，基于提交 `f2676403` 与 Stage 3 未提交补丁 `uncommitted_diff_sha256: 8231c8142c0d968e`，本轮收口后将作为 Stage 3 提交合入 `feat/flexspray`）。

> 历史交付归档：`v5.3`（第十三轮 `ea030dd2`）报告指标见本分支历史提交，Rounds 4–10 历史材料保留在 `scratch/flexspray/history/`。自第十五轮起，`flexspray_case.py` 中 `flex+spraycc` 默认启用 **FlexSpray v6（Time-Quantum Commitment & Reachability-Aware Pooling）**；全量 C++ `TypeId` 默认值保持关闭以保障 Golden 25/25 逐位一致。

---

## 1. 一键复现与回归验证门禁（`r17cand2` 全部已过）

最后一次 C++ 修改完成后的完整验证结果：

| 验证门禁 | 命令与目标 | 结果 |
|---|---|---|
| **编译构建** | `./ns3 build ub-quick-example unified-bus-test test-runner` | **exit 0**（release） |
| **FlexSpray 专项单测** | `/home/cassius/ub/work/frozen/r17cand2/testrunner --test-name=unified-bus-flexspray --verbose` | **11 / 11 PASS**（`1.345 s`，含新增 `UbDcqcnLateCnpDuringDrainTest`） |
| **Golden 逐位回归** | `J=16 /home/cassius/ub/work/bin/golden.sh r17def2 .../r17cand2/sim` + `cmpgold.sh s131 r17def2`（`a128_4m` `11.16 s`、`a32_32m` `11.68 s`） | **25 / 25 SAME** |
| **全量 UB 单元套件** | `/home/cassius/ub/work/bin/alltests_frozen.sh r17cand2 r17def2` | **50 / 50 suites PASS**（`rc=0`） |
| **系统级恢复与尾段回归** | `flexspray_regress.py --out .../reg_r17cand2 --bin .../r17cand2/sim` | **exit 0**（`fence_on 22/22` 全过；`fence_off P5=False, late_execs=9`；`tail4k missing_tasks=0, eff=0.9344`） |

> 完整细节与全机制文档见 [REPORT.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/REPORT.md)、[FEASIBILITY.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/FEASIBILITY.md)（§13）、[BENCHMARK.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/BENCHMARK.md)、[RESEARCH.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/RESEARCH.md)（§9.6）及 [TODO.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/TODO.md)。

---

## 2. FlexSpray v6 核心交付能力与实现位置

FlexSpray v6 在 `v5.3`（SprayCC + MIN_DRAIN_TIME + TpGroupFairShare + 12 µs 探测故障检测 + ReplayFence + T20 fabric 路由撤回 + 尾段切分）之上新增 4 项核心能力（20 项属性，见 [KNOBS.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/KNOBS.md)）：

1. **M1 时间量子分段与尾窗控制**（`ub-transaction.{h,cc}`）：`TimeQuantumSeg=true` 按成员相对速率分档（4/8/16 KB）分配 WRITE 段量子；`ReadQuantumBytes=8192` 拆分 READ 请求；`QuantumTailBytes=49152` + `SlackTailOnly=true` 消除末端单平面长尾串行化。
2. **M2 可达性感知 READ 重池化与工作守恒**（`ub-transport.{h,cc}`）：`ReadPortBudgetBytes=409600` + `ReadScoreReturnRate=true` + `ReadBudgetScaleByRate=true` + `ReadBudgetRepoolDead=true` + `ReadBudgetWorkConserving=true`，仅将 `HardDown`/隔离端口的配额按比例重池化到健康端口（**绝不扩大降 Lane 慢口配额**），并在未饱和时工作守恒推进。
3. **M3 去自排队残差与队列感知速率**（`ub-transport.{h,cc}`、`ub-spraycc.{h,cc}`）：`PortCommitDelay=6us` + `ServiceTimeScore=true` + `ServiceResidual=true` + `ResidualSubtractOwn=true` + `ResidualIdleHalfLife=150us` + `DeliveryRateQueueAware=true`，配合 `RtoSrttFactor=8, RtoFloorFirstOnly=true` 防止降速链路误触发超时与重传风暴。
4. **T29 调度协同与 DCQCN Teardown 修复**：`DesyncTpgCursor=true` + `PrioritizeHeavyWqe=true` + `CoalescePortTrigger=true`；Round 17 新增 `UbHostDcqcn::IsTpLifecycleActive()` / `m_tpDestroyRequested`（`ub-dcqcn.{h,cc}`），根除 TP 退场瞬间晚到 CNP 的竞态中止（`UbDcqcnLateCnpDuringDrainTest`）。

---

## 3. 核心实验结论（全仿真，未见种子 `s15, s16`，`r17cand2/sim`）

### 3a. 跨 SOTA 基线全 blind 矩阵对比（`756` 主矩阵 + `42` `r17_kv_io` SSD 专项 = `798` runs，`0` `rc!=0`）

所有对照策略均先在**可见种子 `s1, s2`**（`tune_s12` `504` 次运行）择优调参锁定最优组合（`mprdma+dq2+ooo2` 优于 `ooo4..32` 与 `dcqcn`；`reps+spraycc` / `reps+dq2`；`hpn+caqm`；`ecmp+dq2+rtx+fd`；`adaptive+caqm+rtx+fd`），再在**未见种子 `s15, s16`** 盲测：

| 方案 | 主矩阵总均效 (`n=108`) | `F0` | `F3far3` | `H1far` | 丢任务 (`n=108`) | `ub_pod` | `cm_pod` | `cm_osub` | `ub_mesh` | `kv_io F3ssd3` | `dag_r17_fa` (`n=54`) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **`flex+spraycc` (v6)** | **0.8271** | **0.8323** | **0.8206** | **0.8284** | **0** | **0.7779** | **0.8667** | **0.8283** | **0.9106** | **0.7654** | **0.9336** |
| `mprdma+dq2+ooo2` | 0.6354 | 0.7497 | 0.3820 | 0.7747 | 0 | 0.6137 | 0.6348 | 0.5939 | 0.6937 | 0.4128 | 0.7926 |
| `adaptive+caqm+rtx+fd` | 0.5953 | 0.6718 | 0.4202 | 0.6938 | 0 | 0.5391 | 0.6050 | 0.5566 | 0.7151 | 0.4070 | 0.6610 |
| `reps+spraycc` | 0.5929 | 0.7374 | 0.3413 | 0.6999 | 2 | 0.5546 | 0.5973 | 0.5965 | 0.6193 | 0.3878 | 0.7979 |
| `reps+dq2` | 0.5311 | 0.7076 | 0.1533 | 0.7324 | 8 | 0.4935 | 0.5312 | 0.4953 | 0.5940 | 0.3702 | 0.7103 |
| `ecmp+dq2+rtx+fd` | 0.5099 | 0.6484 | 0.2128 | 0.6684 | 1 | 0.5141 | 0.5252 | 0.4144 | 0.5260 | 0.3830 | 0.6694 |
| `hpn+caqm` | 0.2735 | 0.2846 | 0.2218 | 0.3142 | 0 | 0.2262 | 0.2772 | 0.2319 | 0.3487 | 0.3619 | 0.3471 |

### 3b. 单故障损失约束 `< 1/N`（严格按 `flexspray_losstable.py` 全口径并列 `<` 与 `<=`）

| 口径 / 数据集 | 语义 | `flex+spraycc` `tdev/N_dev` (`<` / `<=`) | `4N` / `N_scope` | `steady` (`n/a` 计分母) / `trans` | 最强 SOTA 对照 `tdev/N_dev` |
|---|---|---|---:|---|---:|
| **R17 主矩阵逐格 (`n=17`/语义)** | `hard` | `13/17` / `13/17` (`P=1, I=3`) | `1/17` / `13/17` | `6/17 (na=7)` / `13/17` | `mprdma`/`reps+dq2` `14/17` (`P=1,I=2`) |
| **R17 主矩阵逐格 (`n=17`/语义)** | `lane` | **`14/17` / `14/17`** (`P=1, I=2`) | `0/17` / **`14/17`** | **`9/17 (na=6)`** / `14/17` | `hpn` / `reps+spraycc` **`1/17`**, 余 `0/17` |
| **R17 主矩阵逐行 (`n=34`/语义)** | `hard` / `lane` | `26/34` / **`29/34`** (总 **`55/68`** `P=4,I=9`) | `3/68` / `55/68` | `28/68 (na=28)` / `55/68` | `mprdma` `28/34` / `1/34` (总 `29/68`) |
| **R17 联合 `kv_io` 逐行 (`n=36`/语义)** | `hard` / `lane` | `28/36` / **`31/36`** (总 **`59/72`**) | `3/72` / `59/72` | `28/72 (na=32)` / `57/72` | `mprdma` `30/36` / `1/36` (总 `31/72`) |
| **R15 `lm_old_cov` (`130` 格)** | `lane` | **`57/58`** (`P=0, I=1`) vs B1 `47/58` | `24/58` / **`58/58`** | **`57/58 (na=0)`** / `57/58` | 仅 B1 对比（已超越） |
| **R15 `lm_bench` (`50` 格)** | `lane` | **`20/20`** (`P=0, I=0`) vs B1 `8/20` | `8/20` / **`20/20`** | `8/20 (na=12)` / `16/20` | `lm_aicb` (R16): **`18/18`** vs B1 `14/18` |

### 3c. 多阶段尾膨胀与公开超节点 / AI Benchmark 总结

- **多阶段尾膨胀（`flexspray_phaseinfl.py`）**：
  - `R15 phase_bench`（`112` 例）：v6 **`112/112 one-off（0 per-phase）`**，中位超额均值 **`1.06%`**（max `8.75%`），B1 为 `91/21`（`10.07%` / max `92.88%`）。
  - `R16 pi_bench` 非 KV（`216` 例）：v6 **`216/216 one-off`**（`1.45%` / max `8.76%`）vs B1 `184/32` / v53 `49/7`。
  - `R17 pi_r17` 非 KV `F3far3`（`16` 例）：`flex` **`16/16 one-off`（`3.24%` / max `7.86%`）**；全部 6 个竞品陷入 per-phase 重放拖尾（`reps+spraycc 115.09%`、`adaptive 81.13%`、`hpn 34.63%`、`mprdma 241.34%`、`ecmp 240.43%`、`reps+dq2 602.88%`）。`r17_kv_io F3ssd3`：`flex` `9.73% / 15.19%` vs 竞品 `69.75%–262.91%`。
- **硬件复杂度（T4 收口）**：Fabric 核心交换机 `0` 改动；L1/Edge `UbSwitchSprayCc` 仅 `m_kDelay + 1 B/口`（vs CAQM `4×64b` 向量 + 定时器）；报文头 `70 B` ACK；端侧 `O(P)`（`P<=10`）状态与分发归约；`TP_OPCODE_PROBE` 复用保留字。

---

## 4. 已验证但尚未彻底消除的诚实边界与限制

1. **`b_moe H1far` 短阶段权衡**：在 `r17_cm b_moe H1far` 与 `r17_ub_pod b_moe H1far`，`flex`（`0.6937 / 0.6676`）低于 `mprdma+dq2+ooo2`（`0.7244 / 0.6895`，`-2.2 ~ -3.1 pp`），源于 `PortCommitDelay=6us` + 25 µs 探测在多屏障重启微增约 2 µs 延迟；同一工作负载在 `F3far3` 仍以 `0.6552 vs 0.1913` 领先 **3.42x**。
2. **`ub_mesh` 单跳直连物理/路由限制**：`ub_mesh` 单跳直连边跨板绕行受限，`lm_r17` `2/12`（`phys=4, impl=6`）；整体盲测效率仍以 `0.9106` 显著高于 `adaptive 0.7151` / `mprdma 0.6937`。
3. **`r17_kv_io F3ssd3` s16 指标**：SSD 仅 2 平面，单平面降至 1/4 后余下 62.5% 物理容量，`pi_r17_kv` s16 中位超额 `15.19% > 10%` 阈值（同组竞品为 `69.75%–262.91%`）。
4. **其余开放项（见 [TODO.md](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/TODO.md)）**：`T12` KV 混合读入公平性、`T15` `1/64` 单流极限下激进假死与 `fence_false_death_bidir`、写/读幂等与不重放/不回收语义假定（`T1-rest`）、`T22`（25/50/100 us）与 `T23`（`UB_LINK_HEALTH_FABRIC_WITHDRAW_NS`）常规敏感性已由早期轮次标注；仿真器运行速度不在性能要求范围（`T25`）。

---

## 5. 数据索引与工作树状态

- **冻结产物与日志**：`/home/cassius/ub/work/frozen/r17cand2/`；`/home/cassius/ub/work/exp/round15/{unseen_s15_16,s12,recov200_2}`；`/home/cassius/ub/work/exp/round16/`；`/home/cassius/ub/work/exp/round17/{tune_s12,r17_ub_pod,r17_cm,r17_cm_osub,r17_mesh,r17_kv,r17_kv_io,reg_r17cand2,lm_r17.jsonl,lm_r17_kv.jsonl,pi_r17.jsonl,pi_r17_kv.jsonl,dag_r17_fa.jsonl,dag_r17_kv_fa.jsonl}`。
- **未跟踪目录保留**：`scratch/2nodes_single-tp/test/` 按约束保持原样未跟踪。
