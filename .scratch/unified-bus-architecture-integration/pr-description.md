# Unified Bus architecture integration

## 背景

本 PR 将 structured topology/routing、allocator direct completion、Unified Bus lifecycle 和
TP backing compaction 集成到一个基于 `next` 的架构验证分支。目标是先进入 `next` 做更广泛的
实验验证，而不是在合入实验分支前穷举所有算法、拓扑和 workload。

## 主要改动

- structured rule、portable compressed next-hop 与 legacy CSV route 共存；
- allocator direct/scheduled completion 和按输出端口的事件所有权；
- owner-local lifecycle、local/MTP cleanup 与 dependency visibility；
- typed TP quiescence assessment、dormant backing reclaim/rehydration；
- automatic reclaim 关闭时的快速返回，避免逐包执行无效的完整 reclaim assessment。

## 合入 `next` 的条件

- [x] focused Unified Bus、lifecycle、structured-routing、TP-reservation、allocator suites 通过；
- [x] legacy CSV example、portable compressed artifact、prepared structured rule 代表性验证通过；
- [x] allocator on/off、direct/scheduled、local/MTP owner cleanup、forced stop、dependency release、fault、CC、retransmission 控制通过；
- [x] 1024-device/64-switch PR-vs-main 代表性对照完成，canonical 与性能差异已归档；
- [x] 独立 review 未发现阻塞 correctness 或 compatibility 问题。

以下不是合入 `next` 的硬门槛，转为非阻塞 follow-up：全路由/协议/workload 组合、重复的 1024
规模 sweep、剩余 CPU 差距完全归因、其他 liveness ledger 优化。

## 代表性大规模对照

对照使用 1024 device、64 switch、6,144 条 traffic、2,048 条 topology 和 6,078 条 route 的
同一 case。最终保留候选与当前 `main` 各运行两次 measured sample，并运行 canonical canary。

| 指标 | PR 相对 main |
| --- | ---: |
| Retired instructions | `+7.862581%` |
| `Simulator::Run()` | `+18.598110%` |
| Wall time | `+18.560432%` |
| Cycles | `+18.524308%` |
| Peak RSS | `-43.682422%` |

Canonical 两侧各 12,288 个 event identity，identity multiset 完全一致；299 个 timestamp 变化均在
`-2 ns` 到 `+3 ns`，符合本 PR 已接受的 same-time ordering 边界。

结论：内存显著改善，速度尚未恢复到 `main`。该差异被明确接受用于进入实验性质的 `next`，
不宣称与 `main` 等速。

## 验证与工件

- 性能来龙去脉：`.scratch/allocator-event-ownership-fastpath/performance-report.md`
- 机器可读矩阵：`.scratch/allocator-event-ownership-fastpath/result-matrix.yaml`
- focused XML/日志：`.scratch/allocator-event-ownership-fastpath/verification-20260726-ticket-06/`
- integration tracker：`.scratch/unified-bus-architecture-integration/tickets.md`

## Knowledge Sources

- `CONTEXT.md`：统一仓库领域术语和 ownership/lifecycle 边界。
- `docs/adr/0002-compact-tp-backing-with-owner-local-resolution.md`：TP backing 压缩、owner-local
  resolution 和 fail-closed 设计依据。
- `.scratch/structured-topology-routing/design.md`：structured/compressed/legacy routing 的所有权
  与兼容顺序。
