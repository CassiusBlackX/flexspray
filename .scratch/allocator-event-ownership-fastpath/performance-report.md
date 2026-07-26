# Allocator ownership and TP lifecycle performance report

状态: 已完成（剩余性能归因为非阻塞 follow-up）

## 结论

本轮确认 allocator 的 switch-wide 事件所有权扫描是可优化热点。生产实现改为按输出端口维护
连续事件所有权，并保留取消、forced-stop residue、reclaim gate、事件数量、时刻和顺序。该实现
在 allocator 密集用例中回收完整 ownership 与关闭 ownership 差距的 `90.110087%`，在自动
回收耦合控制中回收 `97.431004%`。

随后引入 typed TP quiescence assessment，统一 backing reclaim 与 explicit destroy 的只读
判定。该接口本身没有形成 instruction 回归。最终保留的附加优化是在
`EnableAutomaticBackingReclaim=false` 时，在逐包完整 TP reclaim assessment 之前直接返回；它
相对未优化生产候选减少 `2.951547%` instructions。

最终 PR 相对 main 仍增加 `7.862581%` retired instructions 和 `18.560432%` wall time，同时
peak RSS 降低 `43.682422%`。因此当前结论是“内存目标显著达成、正确性边界通过、速度仍未恢复
到 main”，不能描述为与 main 等速。

完整机器可读结果见 `result-matrix.yaml`。

## 来龙去脉

1. 诊断消融显示，关闭 allocator event ownership 可减少约 `22.55%` instructions，但直接关闭
   会丢失 forced-stop cancellation 和 residue reporting，不能成为生产实现。
2. 生产实现将 allocator 事件 ownership 从 switch-wide generic tracking 改为 out-port-local
   状态，并使用连续 retrigger 存储；不删除、不合并、不重排 callback。
3. allocator 优化通过后，TP reclaim/destroy 的分散 liveness 查询被集中到 typed assessment，
   collaborator 仍保留 owner-local typed 状态，没有引入统一 `IsAlive()` 账本。
4. profile 显示回收关闭时仍逐包执行完整 reclaim assessment。缓存 manager 并在 automatic
   reclaim 关闭时提前返回，形成最终保留的 `2.95%` instruction 收益。
5. retrigger coalescing 和 lifecycle attempt-word cache 均用同 binary A/B 验证。前者只有删除
   既有 allocation round 才出现 `2.18%` 收益；保留语义后 instruction 变化仅 `+0.014732%`。
   后者 instruction、wall、cycles 和 RSS 全部回归。两项均已撤销。

## 最终结果矩阵

百分比均为 treatment 相对 control；负值表示下降。

| 实验 | 处置 | Instructions | Wall | RSS | Canonical | 结论 |
| --- | --- | ---: | ---: | ---: | --- | --- |
| 按输出端口连续存储事件所有权 | 保留 | `-2.595%` | `+0.478%` | `-0.057%` | 一致 | 回收 ownership 差距 `90.11%` |
| 自动回收耦合下的端口本地所有权 | 保留 | `-21.875%` | `-61.537%` | `+0.990%` | 一致 | 回收差距 `97.43%`，wall 仅作支持证据 |
| 回收关闭下 typed assessment | 保留 | `+0.114%` | `-2.381%` | `-0.402%` | 一致 | 低于 `1%` instruction gate |
| 自动回收下 typed assessment | 保留 | `-0.172%` | `+12.974%` | `-1.592%` | 一致 | instruction 无回归，wall 波动不覆盖主指标 |
| 回收关闭快速返回 | 保留 | `-2.952%` | `-1.746%` | `-0.610%` | 12,288 个事件完全一致 | 最终新增生产优化 |
| 保留语义的 retrigger 合并 | 撤销 | `+0.015%` | 两轮方向相反 | `-0.469%` | 完全一致 | 无稳定 instruction 收益 |
| 删除 allocation round 的合并原型 | 诊断限定 | `-2.178%` | `-2.069%` | `-0.689%` | 1,260 个 timestamp 改变 | 收益来自不可接受的语义变化 |
| lifecycle attempt-word 缓存 | 撤销 | `+0.143%` | `+1.449%` | `+0.781%` | 完全一致 | 全指标回归 |
| 最终 PR 对 main | 保留现状 | `+7.863%` | `+18.560%` | `-43.682%` | identity 一致；299 个 timestamp 改变 | 内存改善，速度差距仍存在 |

## 正确性和兼容性

- `unified-bus-switch-voq`、`unified-bus-tp-reservation`、
  `unified-bus-lifecycle-storage`、`unified-bus-lifecycle` focused suites 通过。
- reclaim assessment 的 lifecycle lease、packet、transaction、allocator、transport、PSN、
  retransmission、CC、execution-owner blocker 使用 typed reason 验证。
- allocator on/off、direct/scheduled completion、RR/DWRR、single/multiple retrigger、forced stop、
  owner cleanup 和 snapshot fail-closed 路径保留。
- 最终 PR/main canary 各有 12,288 个 canonical identity，identity multiset 完全一致。299 个
  timestamp 变化落在 `-2 ns` 到 `+3 ns`，属于该 PR 已接受的 same-time ordering 边界。
- `git diff --check` 通过，最终独立 code review 未发现 P1/P2 correctness finding。

## 证据边界

`result-matrix.yaml` 保存归一化数值、比较角色、状态、原始 summary 路径和 SHA-256。仓库内
已有的实验包保留完整 plan、matrix、ledger 和 summary；位于 `/private/tmp` 的最终快速实验不
作为永久工件，因此其关键结果和摘要哈希已固化到矩阵。

当前仍有两项非阻塞 follow-up：

- Ticket 07 尚未逐项完成 switch in-port events、`UbTrackedEventSet` owner scans 和 transaction
  TP reference scans 的放大与消融；这些格子保持 `inconclusive`。
- 最终 PR 相对 main 的 `7.862581%` instruction 差距只完成部分归因。现有证据不能保证速度
  与 main 相同，也不能把全部剩余差距归到某一个 ledger。

因此父级任务的性能文档和独立 review 已补齐，architecture integration PR 可进入 `next`。
这些 follow-up 必须保持可见，但不再阻止架构验证分支接收该 PR。
