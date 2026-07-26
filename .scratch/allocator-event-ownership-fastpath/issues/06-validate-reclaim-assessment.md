状态: 可执行
阻塞于: 05

# 验证统一回收判定与兼容边界

## 目标

证明统一 assessment 只整合查询，没有改变 TP 生命周期、回收时机或兼容接口。

## 验证矩阵

- 每类 lifecycle lease 和 in-flight packet 单独阻塞。
- direct TP、TPG member、transaction remote work 单独阻塞。
- allocator pending/active、transport queue、PSN、timer、retransmission state 单独阻塞。
- congestion-control eligible/ineligible；assessment 通过后的 snapshot 物化失败单独验证为
  fail closed。
- backing reclaim、explicit destroy、DORMANT/LIVE/DRAINING 及错误 execution owner。

## 验收

- focused negative case 断言具体 blocker，不只断言 `false`。
- snapshot 物化失败不得改变 endpoint state，也不得释放 backing。
- 成功 reclaim/destroy 的 identity、generation、dormant snapshot 与恢复行为不变。
- ordinary CSV/case/CLI/trace 和 MTP owner routing 不变。
- 独立 code review 未发现 P1/P2 correctness 问题。
