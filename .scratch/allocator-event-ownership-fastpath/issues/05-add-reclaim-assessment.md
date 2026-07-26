状态: 已完成
阻塞于: 04

# 引入 typed TP reclaim assessment

## 目标

让 `UbTransportChannelManager` 通过一个 typed assessment 统一 backing reclaim 和 explicit
destroy 的判定，删除重复的 lifecycle-idle 拼装，同时保留各 owner 的 typed 状态。

## 实现边界

- 定义 teardown intent、typed blocker set 和必要诊断 counts。
- assessment 显式组合 lifecycle、transaction、allocator、transport、retransmission、拥塞
  控制及 execution-owner 条件。
- backing reclaim 与 explicit destroy 使用同一入口，但保留不同 blocker 要求。
- cancellation、drain 和 forced cleanup 仍由 owner-specific 方法执行。
- 不引入 `IsAlive()`、万能 provider、property bag 或共享账本容器。

## 验收

- 现有两处 lifecycle-idle 条件不再各自复制字段列表。
- 每个拒绝路径返回稳定、可测试的 blocker reason。
- assessment 本身只读，不触发 backing restore、cancel、retire 或 callback。
- dormant snapshot 只在 assessment 通过后的执行阶段物化；物化失败继续 fail closed。
- allocator performance acceptance 不因该接口回退。

## 完成记录

- `UbTransportChannelManager::AssessTpQuiescence()` 成为 backing reclaim 与 explicit destroy
  的统一只读判定 seam，返回固定 bitmask typed blockers 与 diagnostics。
- transport 只暴露 typed quiescence facts；manager 显式组合 lifecycle、transaction、allocator、
  transport、retransmission、CC 与 execution-owner 条件。
- `BACKING_RECLAIM` 保留 automatic flag、allocator、outstanding retransmission、CC contract；
  `EXPLICIT_DESTROY` 保留原有 drain policy，不新增 allocator/CC blocker。
- assessment 不 restore、snapshot、cancel、freeze、retire 或 callback；dormant snapshot 仍只在
  assessment 通过后的 mutation phase 创建，失败继续 fail closed。
- focused TP reservation、switch VOQ、lifecycle、CTP lifecycle/transaction、retransmission/CC、
  retransmission timer、lifecycle storage suites 全部 PASS。
- 性能包 `scratch/20260726-tp-quiescence-assessment-performance`：回收关闭 instructions
  `+0.113751%`、RSS `-0.402299%`；自动回收 instructions `-0.172316%`、RSS
  `-1.592154%`；8 个 run canonical 全部一致，无 safety stop。
