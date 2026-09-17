状态: 可执行
阻塞于: 04, 06

# 归因其他 liveness ledgers

## 目标

判断 switch in-port events、`UbTrackedEventSet` owner scans 和 transaction TP reference scans
是否值得后续优化，不在本任务修改其生产实现。

## 方法

- 为每个候选建立能够放大其真实查询/更新路径的 workload。
- 先预测复杂度和可能结果，再做独立消融或采样。
- 分离正常完成、forced stop、owner cleanup 和 memory growth。
- 报告每个候选对 PR/main retired-instruction 差距的可解释比例。

## 验收

- 每个候选给出“另建优化任务、保留现状、证据不足”之一。
- 不把 queue/protocol/state-machine 数据仅因可计数就归类为 event ledger。
- 若需后续优化，ticket 必须声明 owner 粒度、取消语义、反向索引和正确性 oracle。
