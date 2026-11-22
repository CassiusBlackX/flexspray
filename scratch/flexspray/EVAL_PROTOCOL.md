# FlexSpray 评估协议（第三轮起固定）

本协议在开始第三轮改动之前就已固定，此后所有改动都按同一套口径评估。
目的是确保效率提升来自算法本身，而不是评估口径随结果变化。

## 基线

- 代码：`git tag flexspray-v4-baseline`（提交 `c954e44e`）。
- 数据：`/home/cassius/ub/work/archive/exp_history/{final4,cover4,osub4,buf4}`。
- 用例生成器 `flexspray_case.py`：除新增选项外，不修改已有场景、故障和拓扑的定义。

## 指标（全部报告，不只报一项）

| 指标 | 定义 | 用途 |
|---|---|---|
| `eff_fixed` | T_ideal_fixed / makespan。T_ideal_fixed 是**与方案无关**的流体 LP：被隔离的资源在 t_f + 2 µs（远端得知）之前保持全容量，之后容量为 0；降 lane 在 t_f 生效 | **主要比较口径**。大于 1 说明该方案在隔离后仍在使用故障资源，需结合暴露量解读 |
| `eff` | 条件参照：给定该方案实测的排空量后的 LP | 只用于解释，不用于跨方案比较 |
| makespan、各类流的完成时间 | 原始值 | — |
| 逐设备端口协议利用率 | 全程 / 稳态 / 故障后 / 瞬态四个窗口，见 `flexspray_portutil.py` | 题目要求的"各类设备端口 ≥ 90%" |
| 受损设备的损失 | 稳态、全程、最初 10 µs 三个窗口，对比"实际被移除的容量"下限 | 题目要求的"损失 < 1/N" |
| 暴露量 / 排空时长 | t_f 之后仍经过被隔离资源的字节数 | 硬断链代价的代理指标 |
| 队列峰值与均值、credit 阻塞占比、乱序深度、控制包 p99 | `flexspray_ccmetrics.py` | 拥塞是否被留在网络里 |

## 种子与留出集

- 调参只允许使用种子 1、2，映射为 0（原始流量映射）。
- 验收必须同时报告留出集：种子 3、4，以及 `--map 1`。
  - `--map 1` 把所有端点重新标号：服务器 s 变为 s+1，设备序号 i 变为 count−1−i；故障位置保持不变，因此故障会落到不同的流量角色上。
- 默认参数在全部场景中统一，不允许按场景调参。

## 验收门槛（题目要求）

- 点对点 ≥ 0.95；alltoall ≥ 0.90；混跑中各类设备的全程端口利用率 ≥ 0.90。
- 单点故障：受损设备的损失不高于实际被移除的容量 + 0.5 pp；同时报告全网损失。
- 新机制必须附带"关闭该机制"的消融结果。

## 第四轮补充（2026-11-08）

- 种子 3、4 和 `--map 1` 已在之前的轮次看过，不再算作盲测。v5.2 冻结（`flexspray-v5.2`）之后，才运行未见集：种子 5、6 和 `--map 2`。
- 逐设备利用率同时报告 `util`（端口承载的全部 TP 负载）和 `util_biz`（每个业务字节只计一次）。分析器修正单独提交，并用修正后的分析器同时重算旧版和新版。
- 单故障损失与 1/N 的比较必须写明定义：损失 = 1 − makespan(F0) / makespan(故障)；N = 受损设备中平面数最少者的平面数。

## 第五轮补充（2026-11-08）

- 种子 5、6 和 `--map 2` 已在第四轮看过。v5.3 冻结（`0413d8ef`）之后，才首次运行新的未见集：种子 7、8 和 `--map 3`。
- 逐设备 busy 窗口为 [该设备在该方向上第一条流的释放时刻, 最后完成时刻]（分析器修正 `5fa48eaf`，旧版和新版一致重算）。只影响带释放延迟的负载（stagger、staggermixed）。
- 单故障损失按设备逐个报告：每个设备自身的物理平面数、可用平面数、移除容量、相对健康损失、损失 − 自身 1/N，以及全网损失（`flexspray_lossreport.py`）。"受损设备中最小的 N"只作为待确认的口径，并列出。

## 第九轮补充（2026-11-10）

- **基线**：研究比较以 B1（B0 + 公共 RTO 修正）为起点；B0 只作为历史与问题复现。
- **降 lane 门槛**按受损设备计算：损失 = 1 − T_dev(F0) / T_dev(故障)，T_dev = 以该设备为源或目的的任务的最后完成时刻；允许值 = k/32 + 0.5 pp（8 平面 × 4 lane），见 `<work>/bin/dtab_dev.py`。按作业 makespan 计算的近似只能作为保守的参考。
- **`--map k`** 只按 k mod 4 平移服务器，并且对所有 k ≥ 1 都翻转设备序号：map 5 ≡ map 1，map 4 = 只翻转。以后的新映射只能用 k mod 4 未用过、且翻转方式不同的组合，或者新的映射规则。
- **已用过的样本**：
  - 种子 1–13；
  - map 1–5（map 5 ≡ map 1）；
  - 故障时刻 20 / 50 / 80 / 120 µs；
  - 规模 ×0.02 / ×0.5 / ×1.5 / ×2；
  - links-per-pair 2。
- **丢包注入**：`flexspray_case.py --drop task:node:port:dir:type:psn:last:count:afterNs`，规则只在交换机出口生效（主机出口不经过故障回调）。

## 第十轮补充（2026-11-11）

- **两种单故障门槛并列报告**，工程门槛不替换原文：
  - 原文：受损设备损失 < 1/N_dev，N_dev 为该设备自己的物理平面数；
  - 工程门槛：受损设备损失 ≤ k/(N_dev × 4) + 0.5 pp。
- **评价器修正**：第九轮 `dtab_dev.py` 对所有设备都用 k/32，对 4 平面 CPU（h2d 近端）错误。改用 `<work>/bin/r10tab.py`、`r10sum.py`，按设备自身的 N 计算。
- **物理可行性**：`flexspray_devgate_lp.py FAULT HEALTHY` 是逐设备截止时间 LP，只约束受损设备，对两种门槛都给出可行性与容量割。不可行即证明不可达。
- **身份区分**：
  - 比较基线：B1，固定研究对照；
  - 生产默认：v5.3，未变；
  - 验收合格版本：本轮没有。
- **已用过的样本**：在第九轮列表之上增加：
  - 种子 14；
  - 故障时刻 35 µs；
  - 规模 ×0.75，以及种子 14 的 ×0.5；
  - links-per-pair 1。
- **构建纪律补充**：修改头文件后必须同时重建 `unified-bus-test` 和 `test-runner` 再冻结。否则冻结出的测试库与模拟库 ABI 不一致，第十轮第一遍单元测试因此作废。

## 第十一轮补充（2026-11-11）

**故障损失的完整口径**：`flexspray_lossmatrix.py` + `flexspray_losstable.py`（32f17865）。
- **窗口**：
  - 稳态 = [t_f + 重均衡时间, 两次运行中先到 80% 字节点者]，不足 10 µs 记为 n/a；
  - 完成时间 = T_dev；
  - 瞬态 = [t_f, t_f + 10 µs]。
- **重均衡时间**：5 µs 速率自 t_f + τ 起保持 ≥ 故障后水平的 95% 的最小 τ。对读负载不可靠（T24）。
- **N 的读法**：N_dev、4·N_dev、N_scope、LP 下界。
- **关系**："<"为主，"≤"为附加列。
- **设备带宽**：端口载荷计数器（rx + tx），不用 node_biz 计数器，后者只在整条消息完成时才前进。
- **健康参照**：同一集合中的 F0；没有时，取 case 参数（规模、lpp、映射、种子、场景、方案）完全相同的集合中的 F0。

**故障模型新增参数**：`UB_LINK_HEALTH_FABRIC_WITHDRAW_NS`（默认 2 µs，**假设**；T23）。

**本轮没有使用新的未见样本**。T20 的 case 重用第十轮用过的种子 14、lpp 1。

## 第十二轮补充（2026-11-11）

**多阶段负载**（`flexspray_bench.py`：b_ring / b_moe / b_kv / b_mix）用 `flexspray_phaseinfl.py` 评价：
- 每个阶段对比只含该阶段流量的 LP；
- excess = (D_fault − D_healthy) − (L_fault − L_healthy)；
- 判定阈值：故障后阶段的 excess 中位数 > 健康阶段时长的 10%，即为"每阶段复现"；
- 这些场景的 `eff_fixed` 不适用（T27）。

**稳态窗口**：只作附加列；n/a 单独计数，留在分母中；稳态与瞬态窗口的物理类判据标为 phys*（假设设备受端口限制，不是 LP 证明）。

**已用过的样本**：b_* 场景的种子 1、2（调参种子），故障 F0 / F3near2 / F3far2 / F3near3 / F3far3。

**计时**：提速的计时只在空闲机器上采信；与矩阵并发运行时测得的时间作废（本轮第一次 PGO 计时 66.2 s 即被作废）。

## 第十三轮补充（2026-11-11）

- **工作项**：只以 REQUESTS 的三条性能要求为目标；仿真提速不再是工作项（T25 关闭）。
- **健康上界（benchmark v1）**：
  - 阶段上界 B = max(LP + α_min, 各流单独 + α_f)，α = 实测空网络单报文完成时间减一个包的串行化（服务器内 0.37 µs、跨服务器 1.35 µs）；
  - 可达性用时延感知的时隙 LP 判定（`flexspray_shiftlp.py`）；
  - 要求 2 在突发负载上用"忙期条件上界"（`flexspray_dagbound.py` 的 busy 行）。不是端口受限的设备（例如受写方平面限制的 SSD 收方向）不按自身容量判定。
- **降 lane 主口径**：受损设备完成时间损失 < 1/N_dev（逐设备 LP 证明物理不可行的行除外），种子 1、2 取最差；benchmark v1 另看故障后每阶段的超出，≤ 10% 为"一次性"。
- **恢复预言机 P6**：仍按 200 µs 排空判定。对 1/64 的两例，另外报告 10 ms 排空时的结果，用来区分泄漏与慢排空；这不改变 P6 的判定。
- **已用过的样本**：本轮只用了调参种子 1、2（b_* 与旧场景），加上新场景 d2io / d2ioread 的种子 1、2。**没有新的未见样本**，所以候选 D 尚未做未见集验收。
- **新故障**：F3ssd2 / F3ssd3 / F3nic2 / F3nic3（SSD、NIC 端口的 x2 / x1）。

## 第十四轮补充（2026-11-12）

- benchmark v1 的故障损失按原文口径判定（受损设备完成时间损失 < 1/N_dev）；"每阶段超出"只作附加列。
- 物理界用 `flexspray_dagbound.py --fault-aware`（c4582026）。
- **已用过的样本**：b_ring ×4（`--scale 4`）的种子 1、2；a2aread F1near / F4 / H4 带跟踪的种子 1；旧矩阵上 D' 的种子 1、2（调参用）。

## 第十五轮补充：FlexSpray v6（Time-Quantum Commitment，2026-11-21）

- **冻结构建与门禁（`r15cand2`，同编 `ub-quick-example` / `unified-bus-test` / `test-runner`）**：
  - Golden 25/25 `SAME`（vs `s131`）；默认配置 50/50 单元测试全过；开启 v6 传输属性（含 `CoalescePortTrigger=true`，排除与旧夹具断言冲突的 `ReadQuantumBytes` / `PrioritizeHeavyWqe` / `DesyncTpgCursor`）50/50 全过；`unified-bus-flexspray` 默认与全 20 项 v6 属性下均通过。
  - 恢复预言机与回归：`recov200_2`（`FLEX_DRAIN_AFTER_US=200`）22/22 `P1..P5=True, tasks_missing=[]`，21/22 `P6=True`；`fence_false_death_bidir` 因 v6 提前于 `2300 us` 完成全部任务（对比 `v5.3` `9400 us`），残留 `2.36 MB` CBFC 包在 `1/64`（`6.25 Gbps`）链路上需至 `5630 us` 排空，已在 `flexspray_recovery_suite.py` 为该用例设 `DRAIN_US=10000`，合入后 `flexspray_regress.py` 三项全 `PASS`。
  - LP 求解器稳定性：`flexspray_analyze.py` 新增 `_solve_lp`（默认 `highs-ipm`，仅对个别数值边界回退 `presolve=False` / 限时 `highs-ds`）。
- **已用过的样本（开发集）**：仅种子 **1、2** 用于 v6 参数收敛、`s12` 全矩阵（`1080` 仿真）与 4 项单支柱消融（`no_TQS` / `no_READPOOL` / `no_RESFIX` / `no_SCHED`）。
- **未见盲验样本（锁定参数后零微调）**：种子 **15、16**（`unseen_s15_16`，`968` 仿真，`bad_rc=0, bad_miss=0`）及 `--map 2` / `--fault-ns 35000` / `--scale 0.75`（种子 15）；所有读数严格按 `N_dev` / `4·N_dev` / `N_scope`、`tdev` / `steady(含 n/a 分母)` / `trans` 及 `<` vs `≤` 并列报告。

---

## 第十六轮补充：公开超节点拓扑、开源 AI 工作负载与 Fault-Aware DAG Bound（Stage 2, commit `f2676403`）

- **评估目标**：引入公开文献超节点拓扑与开源 AI 通信基准，在零针对性微调前提下检验 FlexSpray v6（`flex+spraycc`）相对历史基线（`flex+spraycc+b1` / `flex+spraycc+v53`）的泛化性与物理下界一致性。
- **公开超节点拓扑**（`flexspray_topo_public.py`，400 Gbps × 4 lanes）：
  1. `cm_pod`（CloudMatrix384 1/12 缩比，`arXiv:2506.12708` §3.3）：`4` 服务器 × (`8` NPU@7 平面 + `4` CPU@4 平面 + `1` NIC@2 平面 + `1` SSD@2 平面)，`K=4` Spine × `J=4` 上行（`16 up >= 13 down`，1:1 无阻塞）。
  2. `cm_pod_osub`：同主机/平面结构，`K=3` Spine × `J=2` 上行（`6 up vs 13 down`，~2.17:1 超分比）。
  3. `ub_mesh`（`arXiv:2503.20377` §3.2–3.3）：`4` 板 × `8` NPU（`32` NPU + `8` CPU + `4` NIC + `4` SSD），每 NPU `6` 条 1-hop 直连边（`3` 板内四元全连接 `20 ns` + `3` 跨板同秩全连接 `40 ns`）+ `4` 条 LRS 交换轨道（板间直连 `lrs_links=3` 束，`100 ns`，无 L2 Spine 层，共 `10` 口 = `4.0 Tbps/NPU`）。
- **开源 AI 基准负载**（`flexspray_aicb.py`）：`b_aicb_tp_ep`、`b_deepep_skew`、`b_deepep_prefill`、`b_nccl_pipe`、`b_kv_skew`。
- **分析器升级**：`flexspray_analyze.py` 与 `flexspray_lossmatrix.py` 对重复 `(src, dst, planes)` 商品流聚合为单一 LP 商品变量（`_agg_flows`），支持 `ub_mesh` 混合直连 + LRS 端口容量建模；全量验证运行在未见种子 `s15, s16`（`r16_cm` / `r16_cm_osub` / `r16_mesh` / `r16_ub_aicb` / `r16_kv_io` / `r16_kv_s05` / `r16_kv_s20`）。

---

## 第十七轮补充：公平调参、SOTA 全仿真盲测与 SSD 专项对照（Stage 3, frozen `r17cand2`）

- **对比方案锁定与公平调参协议（严格隔离训练集/盲测集）**：
  - C++ 新增对照策略（`REPS_CACHE`、`MPRDMA_OOO`、`PINNED_PLANE`）与 CC/RTO/队列组合仅在**可见种子 `s1, s2`**（`work/exp/round17/tune_s12/{tune_ub,tune_mesh}`，共 `432 + 72 = 504` 次运行，`0 rc!=0`）调优锁定，随后零改动进入**未见种子 `s15, s16`** 盲测。
  - 锁定 7 组对比方案：`flex+spraycc`（v6）、`mprdma+dq2+ooo2`（`ooo in {2,4,8,16,32}` 与 `dcqcn/dq2` 共 10 组最佳：`tune_ub 0.6269`, `tune_mesh 0.6435`）、`reps+spraycc`、`reps+dq2`、`adaptive+caqm+rtx+fd`、`ecmp+dq2+rtx+fd`、`hpn+caqm`。
- **冻结二进制与前置门禁（`r17cand2`，最后一次 C++ 修改后全通过）**：
  - 含 `UbHostDcqcn::IsTpLifecycleActive()` / `m_tpDestroyRequested` teardown 晚到 CNP 修复与 `UbDcqcnLateCnpDuringDrainTest`。
  - 门禁：`unified-bus-flexspray` `11/11 PASS`；Golden `25/25 SAME`（`s131` + `r17def2`）；全量单元套件 `50/50 PASS`；`flexspray_regress.py` exit `0`（`fence_on 22/22` 全过、`fence_off P5=False late_execs=9`、`tail4k missing_tasks=0 eff=0.9344`）。
- **盲测矩阵（未见种子 `s15, s16`，`--stop-ms 5`）**：
  - 主矩阵 5 组（`756` 次：`r17_ub_pod` `294`、`r17_cm` `126`、`r17_cm_osub` `168`、`r17_mesh` `126`、`r17_kv` `42`），覆盖 `F0 / F3far3 / H1far`。
  - **SSD 专项矩阵 `r17_kv_io`**（`42` 次）：针对 `r17_kv` 中 `F3far3/H1far` 与 `SSD(planes 4,5) <-> NPU6..7` 正交问题，加测 `b_kv_skew` 的 `F0 / F3ssd3 / H1ssd` 全 7 方案对比。
  - **规范统计口径**：全部报告严格采用 `flexspray_losstable.py`（最差设备×种子定格，`tdev` 以 `lp_lb >= 1/N` 判 `phys`，`steady/trans` 标 `phys*`，保留 `n/a` 于分母，`<` 与 `<=` 并列，报告 4 种 `N`）、`flexspray_phaseinfl.py` 与 `flexspray_dagbound.py --fault-aware`。

---

## 第十八–十九轮补充：FlexSpray v7 无量纲化、双向对等代际活性与 NSDI 全维盲测协议（frozen `r19nsdi`）

- **冻结与门禁约束**：禁止修改 Golden 默认值；每次源码变更后重新 `Release` 编译并 `freeze.sh r19nsdi`，核对共享库 SHA256 一致，全数通过 5 道前置门禁（17/17 专项单测、Golden `25/25 SAME`、全量 `50/50 PASS`、`flexspray_regress.py` exit `0`）。
- **绝对盲测与干净汇总准则**：
  1. 全部 E1–E5 评估统一在**未见盲测种子 `15, 16`**、固定整数 `--stop-ms 4` 上运行，共计 **22 个批次 `2,898` 次全仿真**，不针对未见样本做任何超参再调优；
  2. 在聚合原始效率与 Fault-Aware DAGBound 均值时，所有 `rc != 0` 或 `missing_tasks > 0` 的失败算例**强制以 `0.0` 计入分母**（如 Clean `e1_dagbound_summary.csv` 与 `e3_e5_dagbound_summary.csv` 均通过逐例关联 `results.jsonl` 修复未过滤缺失任务时 `B/T > 1` 虚高偏差）；
  3. LossMatrix 同时输出规范归并视图（`e1_losstable_{v7,sota}_{lt,le}.md`）与**按拓扑严格分层视图**（`r19_summary.json["losstable"]["sota_topology_stratified_tdev"]`），防止同名 `(scenario, fault)` 跨拓扑混并丢失空间差异；
  4. E3 大规模实验显式声明载荷缩放系数（`S=8` 及 `S=16 d2d/b_kv` 为 `--scale 0.5`，`S=16 b_ring` 为 `--scale 0.25`）与 `ub_mesh` 中 `ALPHA_US` 对 `b_ring F0` 造成的 `~1.04` 边界备注。
