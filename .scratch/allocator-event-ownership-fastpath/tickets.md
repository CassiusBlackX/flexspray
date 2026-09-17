# Allocator event ownership and reclaim assessment tickets

| 编号 | 任务 | 状态 | 阻塞于 |
| --- | --- | --- | --- |
| 01 | 固化行为与性能基线 | 已完成 | - |
| 02 | 实现按输出端口的 allocator 事件所有权 | 已完成 | 01 |
| 03 | 验证 allocator correctness 与 forced stop | 已完成 | 02 |
| 04 | 完成 PR/main/next 性能归因与验收 | 已完成 | 03 |
| 05 | 引入 typed TP reclaim assessment | 已完成 | 04 |
| 06 | 验证统一回收判定与兼容边界 | 已完成 | 05 |
| 07 | 归因其他 liveness ledgers | 待补充（非阻塞 follow-up） | 04, 06 |
| 08 | 完成独立 review 与父级任务包交接 | 已完成 | 06 |

任务 05 属于当前 PR 的必需交付，但只有任务 04 的 allocator 性能验收通过后才能开始。任务
07 只形成证据和处理结论；任何新增生产优化必须另建任务，不能并入任务 07。

当前性能报告和归一化结果矩阵已固化在 `performance-report.md` 与 `result-matrix.yaml`。
Ticket 07 的三个 ledger 尚未逐项完成，但它们不在本 PR 的生产 diff 内，转为进入 `next` 后的
非阻塞 follow-up。
