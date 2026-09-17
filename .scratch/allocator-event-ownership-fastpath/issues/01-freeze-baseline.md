状态: 已完成

# 固化行为与性能基线

## 目标

把 allocator event ownership 的现有语义、诊断上界和可重复测量命令固定为后续实现的测试
oracle。不得修改生产调度行为。

## 工作范围

- 固定目标分支、commit、构建配置和五个语义化比较状态。
- 复核当前事件种类、每类并发基数、调度入口、callback 入口和 dispose 路径。
- 为同一输出端口多个 retrigger、scheduled notification、callback 内 reclaim 查询和 forced
  stop residue 补充或锁定回归测试。
- 记录 20-30 秒主 workload、三次配对顺序、canonical 输出和 retired-instruction 采集方法。

## 验收

- 测试在当前完整 ownership 上通过，并能在删除必要 ownership 时失败。
- 诊断 treatment 仍只用于性能上界，不进入生产默认值或用户配置。
- 基线 manifest 包含绝对工件路径、commit、命令、退出状态和失败可见性规则。

## Batch Reuse Map

Pre-refactor commit: `2bf02c8c082841381d599a10d6a1e461d4c24ea6`

Semantic batch: allocator event ownership 行为 characterization 与既有性能工件冻结。

Old active symbols: `UbSwitchAllocator::ScheduleTriggerAllocator()`、
`UbSwitchAllocator::HasPendingOrActiveWork()`、`UbSwitchAllocator::DoDispose()`、
`UbRoundRobinAllocator::AllocateNextPacket()`、`UbDwrrAllocator::AllocateNextPacket()`。

Old call paths: 同一 outPort 的 retrigger 调度；兼容模式的 scheduled port notification；
allocator callback 入口的 pending-work 查询；forced stop 的事件取消和 residue 统计。

Continuous code blocks reused: `UbSwitchVoqTestSuite` 的 switch/allocator fixture、现有 VOQ reclaim
gate 和 dispose residue 测试。

Reuse mode: 仅增加 characterization 断言和测试用例，不抽取或复制生产调度实现。

Required lifecycle adaptations: 无。

Preserved ordering and observable behavior: RR/DWRR 选择、allocation delay、同时间事件顺序、
direct/scheduled completion、pending/running reclaim gate、forced-stop cancellation/residue 均不变。

Existing characterization: `UbSwitchVoqDestroyGateBlocksAllocatorAndAccountingTest`、
`UbSwitchDisposeCancelsOwnedQueueEventsAndRecordsResidueTest`、
`UbAllocatorScheduledCompletionCompatibilityTest`。

Explicit behavior changes: 无；只锁定现有行为。

Excluded paths: allocator、switch、TP manager 和其他生产代码；事件合并、coalesce、reschedule；
诊断 ownership-disable 配置化；Ticket 02。

Focused validation: `ninja -C cmake-cache unified-bus-test`，随后
`./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq`；最后 `git diff --check`。

Re-approval triggers: 测试需要生产 API/行为变化、既有行为与设计不一致、focused suite 出现未知失败，
或无法在不伪造负例的前提下证明 ownership 删除会被断言捕获。

## 当前差异与基线

- 本 Ticket 开始时 HEAD 与设计基线一致，均为
  `2bf02c8c082841381d599a10d6a1e461d4c24ea6`。
- worktree 已有其他任务文档和实验工件改动；本 Ticket 不修改或清理这些内容。
- 已有构建缓存和 `build/utils/ns3.44-test-runner`，无 repo-local Python 环境；本 Ticket 不需要
  Python 依赖或新环境。

## 讨论

- RR 的 `Init()` 当前调度一个 deadlock event，DWRR 的 `Init()` 当前不调度；因此两个同端口
  retrigger 的精确 pending/residue 分别为 RR `3`、DWRR `2`。Ticket 01 将该差异作为现有行为
  oracle，不修改生产实现。
- scheduled completion fixture 在 `SendPacket()` 后已有一个 10ns allocation event；直接调用
  `AllocateNextPacket()` 增加 port notification 后精确 pending 为 `3`，notification 执行后为
  `2`。
- 未在当前生产 worktree 中临时删除 ownership 或启用诊断 bypass 来执行 mutation test，避免把
  会破坏 forced-stop 语义的诊断修改混入本 Ticket。精确事件计数、pending-peer reclaim gate 和
  callback-entry 前后断言从结构上会捕获必要 retrigger/notification/allocation/deadlock ownership
  的删除；此结论不冒充已执行的负例结果。

## 完成证据

- cwd：`/Users/ytxing/workspace/ns-3-ub-architecture-integration`
- `git diff --check`：退出码 `0`。
- `ninja -C cmake-cache unified-bus-test`：退出码 `0`；生成
  `/Users/ytxing/workspace/ns-3-ub-architecture-integration/build/lib/libns3.44-unified-bus-test.dylib`。
  仅有两个既有 unused-function warning。
- `./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq`：退出码 `0`，结果
  `PASS unified-bus-switch-voq 0.002 s`。
- 调试期间同一 focused suite 曾因本 Ticket 新增的 DWRR deadlock 和 scheduled allocation 精确
  计数假设错误退出 `1`；只修正测试 oracle 后最终通过，未改生产代码。
- 性能基线 manifest：
  `/Users/ytxing/workspace/ns-3-ub-architecture-integration/.scratch/allocator-event-ownership-fastpath/baseline-manifest.md`。
- 复用的绝对性能工件根目录：
  `/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution`。
- 主线程复核 provenance 的 base revision 与 source diff SHA-256 均与 manifest 一致，并再次运行
  `./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq`，退出码 `0`，结果
  `PASS unified-bus-switch-voq 0.001 s`。
- `UbAllocatorCallbackOwnershipProbe` 是重构前的白盒 characterization，直接使用当前 protected
  tracking seam。Ticket 02 必须把它迁移到新的 owner-local 测试接缝并保留 pending-peer 语义；
  不得为了保留该测试实现而继续维护 switch-wide prune。
