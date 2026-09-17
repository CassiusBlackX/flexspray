状态: 已完成
阻塞于: 01

# 实现按输出端口的 allocator 事件所有权

## 目标

消除 allocator 正常完成路径上的 switch-wide event vector prune，同时保留事件句柄、端口
归属、pending/running 状态、取消和 residue。

## 实现边界

- `ALLOCATION` 使用每输出端口单 slot，`DEADLOCK` 使用 allocator 单 slot。
- `RETRIGGER` 和非 direct `PORT_NOTIFICATION` 在本任务中保留多个具体 `EventId`。
- callback 入口自注销；`HasPendingOrActiveWork()` 只读取端口局部计数和 active queue。
- `DoDispose()` 取消所有 pending handles，并保持 forced residue 的定义。
- 不合并事件，不改变 schedule 时间，不引入 lifecycle lease，不抽取跨模块万能 helper。
- Ticket 01 的 callback-entry probe 允许迁移到新的 owner-local 测试接缝，但必须继续证明当前
  callback 已开始时 pending peer 仍阻塞、最后一个 peer 开始后不残留旧 scheduling gate；不得
  保留 `PruneResolvedEvents()` 仅用于兼容白盒测试。

## 验收

- 正常 allocator round 不调用全局 `remove_if`/`IsPending()` prune。
- 端口查询为 O(1)，dispose 成本为 O(output ports + pending events)。
- 任务 01 的回归测试全部通过。
- 变更限于 allocator 实现和必要测试，不修改其他账本。

## Batch Reuse Map

Pre-refactor commit: `2bf02c8c082841381d599a10d6a1e461d4c24ea6`

Semantic batch: allocator owner-local pending event handles and output-port scheduling-work state.

Old active symbols: `TrackEvent()`、`PruneResolvedEvents()`、`TriggerAllocator()`、
`ScheduleTriggerAllocator()`、`NotifyAllocationFinish()`、`CheckDeadlock()`、RR/DWRR
`AllocateNextPacket()`、`HasPendingOrActiveWork()`、`DoDispose()`。

Old call paths: allocation delay callback、same-time retrigger、compatibility-mode port notification、
periodic deadlock check、allocator reclaim gate 与 forced disposal。

Continuous code blocks reused: 原 RR/DWRR 调度、notification、deadlock 和 dispose 路径保持原位；
只把 `TrackEvent()` 包装替换为 owner-local schedule/callback-entry trampoline。

Reuse mode: 最小适配既有调度入口，不抽取跨模块 helper。

Required lifecycle adaptations: 无；未使用 lifecycle lease 或 `UbTrackedEventSet`。

Preserved ordering and observable behavior: schedule delay、same-time insertion order、RR/DWRR packet
selection、direct/scheduled completion、多个 retrigger、reclaim conservative gate、forced residue。

Existing characterization: Ticket 01 的 RR/DWRR multi-retrigger、callback-entry pending peer、
scheduled notification 和 exact forced residue tests。

Explicit behavior changes: 正常 callback 在入口注销 owner-local pending handle；正常 allocator round
不再 prune switch-wide vector；outPort reclaim 查询改为 O(1) state lookup。未初始化、越界或
dispose 后的 retrigger 请求现在同步 fail closed，不再先排入一个随后由 callback 范围检查忽略的
空事件；该收紧只覆盖 allocator 生命周期之外的无效调用。

Excluded paths: switch in-port、TP manager、transaction、lifecycle、reclaim assessment 与性能工件。

Focused validation: `git diff --check`；`ninja -C cmake-cache unified-bus-test`；
`./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq`。

Re-approval triggers: 需要改变调度时刻/顺序、coalesce callback、修改其他生产模块或 Ticket 03+
语义时停止。

## 实现与验证结果

- `ALLOCATION` 为每 outPort 单 pending slot；`DEADLOCK` 为 allocator-wide 单 slot。
- `RETRIGGER` 与 scheduled `PORT_NOTIFICATION` 以 token-keyed owner-local map 保留每个具体
  `EventId`，callback entry 先注销再执行原 action。
- 每 outPort 的 `schedulingWorkCount` 与 `activeIngressQueue` 支持 O(1)
  `HasPendingOrActiveWork()`；notification/deadlock 不进入 scheduling-work gate。
- `DoDispose()` 精确计数并取消全部 pending slot/map handle，running callback 已在入口注销，
  不会被 forced stop 当作 pending event 取消；实现需要遍历每个 output-port state 和其中的
  pending handles，因此复杂度为 O(output ports + pending events)。
- `git diff --check`、目标 build 与 focused suite 均以退出码 `0` 完成；未生成性能结果。
