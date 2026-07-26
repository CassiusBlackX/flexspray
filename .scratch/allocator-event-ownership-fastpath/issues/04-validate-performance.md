状态: 已完成
阻塞于: 03

# 完成 PR/main/next 性能归因与验收

## 目标

确认生产实现真正消除 allocator ownership 热点，并解释当前 PR 相对 main/next 的剩余成本。

## 实验

每项实验使用简洁的中文语义名称，运行三次配对样本，并把仿真稳定在约 20-30 秒：

- main 基线行为；
- next 基线行为；
- 当前 PR 完整事件所有权；
- 当前 PR 关闭 allocator 事件所有权诊断；
- 当前 PR 按端口事件所有权优化。

至少覆盖一个 allocator 密集型复杂 workload，以及一个包含拥塞控制、流量控制、重传或
多路径耦合的控制 workload。Runner 记录 retired instructions、cycles、RSS、wall/CPU、事件数
和 canonical 输出，主线程只读取汇总。

## 验收

- allocator 密集 workload 的完整 ownership 与诊断关闭差距稳定且至少为 5% 时，恢复该差距
  的至少 80%。
- ownership 差距小于 5% 的耦合控制 workload 不使用恢复比例；候选相对完整 ownership 不得
  出现方向一致且超过 2% 的 retired-instruction 回归。
- 三对 retired-instruction 方向一致；wall-time 波动不得替代 instruction 结论。
- 三次配对样本中不得出现方向一致且超过 2% 的 RSS 增长。
- canonical 差异为零或逐项解释；失败、跳过和 wall-limited case 保持可见。
- 报告单独列出仍未解释的 PR/main 成本，不把 retained correctness 成本称为回归。

## 验证结果

- main/next/PR、完整 ownership、关闭 ownership 和旧 local candidate 的跨分支归因已归档在
  `scratch/20260725-allocator-ownership-cross-branch-performance/`；既有 PR/main 报告保留
  lifecycle correctness 成本、结构化路由收益和 case-dependent 回归，不把正确性成本隐藏为噪声。
- 当前 production candidate 先移除 generic callback payload，再将 retrigger owner container
  改为端口本地连续存储；notification map、allocation wrapper、事件数量/时刻/顺序、token、
  `EventId`、dispose/residue/reclaim gate 均未改变。
- allocator-dense primary 三轮 instructions 中位数 `41,327,225,593`，相对完整/关闭 ownership
  差距恢复 `90.110087%`，超过 80% 门槛；canonical 全部一致，RSS 中位数下降 `0.057438%`。
- automatic backing reclaim 耦合控制三轮 instructions 中位数 `351,732,306,911`，恢复率
  `97.431004%`；canonical 全部一致，RSS 中位数增长 `0.990481%`，低于 2% guard。
- `unified-bus-switch-voq` 与 `unified-bus-lifecycle` focused suites 通过；所有 canary/measured
  probe return code 为 `0`，无 safety stop，残留进程检查为空。
- primary 结果：`scratch/20260726-allocator-retrigger-contiguous-storage-ablation/results/summary.json`。
- coupled 结果：`scratch/20260726-allocator-contiguous-storage-coupled-control/results/summary.json`。
- 独立 review 未发现生产实现 P1/P2；实验包 provenance P2 已通过冻结 control provenance、
  完整 treatment diff 和 CMake cache 的 build 前后 gate 修复。
- 最终保留候选相对 main：instructions `+7.862581%`、Simulator time `+18.598110%`、wall
  `+18.560432%`、cycles `+18.524308%`、RSS `-43.682422%`。12,288 个 canonical identity
  完全一致，299 个 timestamp 变化位于 `-2 ns` 到 `+3 ns`。
- 集中报告和结果矩阵见 `../performance-report.md` 与 `../result-matrix.yaml`。剩余 PR/main
  instruction 差距仅部分归因，不宣称与 main 等速。
