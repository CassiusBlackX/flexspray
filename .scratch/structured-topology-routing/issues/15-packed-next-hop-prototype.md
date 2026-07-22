状态: 已完成
阻塞于: 11

# 验证通用 packed next-hop index

从现有完整路由输入构建不依赖拓扑类型的紧凑 next-hop 索引。候选端口集合全局去重，每个
查询项只保存 8、16 或 32 位 route class ID。

本任务只实现独立 benchmark prototype，不修改生产协议格式、`UbRoutingProcess`、
`GetOutPort()`、HASH/ADAPTIVE 或一次绕路语义。

## 验收

- 所有源 `TABLE` 记录的 shortest/non-shortest 候选集合逐项一致；
- 9427968 个目的端口查询和 2145280 个节点主地址查询无缺失；
- 报告 route class 数、ID 位宽、核心 payload、构建时间和配置释放后 RSS；
- 使用现有顺序、随机和 workload 查询 corpus 报告平均值、P50、P99 和 QPS；
- 与当前完整表和 `UbTopologyNextHopIndex` 基准在相同查询条件下比较。

## 讨论

2026-07-22：先验证 dense packed 表。只有其内存或缓存行为仍不够好时，才继续实现 sparse
bitmap、range/trie 或 shared decision DAG。

2026-07-22：1K 场景验证完成。全部 9427968 条端口路由、2145280 条节点主地址查询和
65536 条 workload 查询均无缺失，三次进程的校验和一致。索引使用 139 个 route class，
当前数据自动选择 8 位 ID。相同物理拓扑加 packed index 的 RSS 中位数为 256.58 MiB，
比完整表降低 84.4%；workload 候选查询中位数为 25.28 ns，比完整表快 1.78 倍。

结论：通用 packed index 值得作为生产运行时后端继续实现，但不能替代结构规则的直接生成。
推荐路径是由结构规则或完整路由生成器产生相同的精确候选关系，再统一编译进 packed index。
详细结果见 `packed-next-hop-prototype-report.md`。
