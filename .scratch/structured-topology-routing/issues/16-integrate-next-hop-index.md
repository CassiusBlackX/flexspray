状态: 已完成
阻塞于: 15

# 接入通用 next-hop index

把 prototype 已验证的候选集合去重和自适应 8/16/32 位索引实现为生产类，并用于结构化
输入中的 `TABLE`。索引由所有节点共享，不复制到每个 `UbRoutingProcess`。

## 语义边界

- 查询顺序为规则路由、通用 next-hop index、旧表路由；命中来源完整返回 shortest 和
  non-shortest 集合，不跨来源合并。
- 目的端口路由缺失时回退到目的节点主地址聚合集合，与现有完整表一致。
- `GetOutPort()`、HASH/ADAPTIVE、入端口过滤和一次绕路行为不变。
- 旧 CSV 和手工路由接口保持原有容器，不在本任务迁移。
- 本任务不把 topology rules 展开成稠密矩阵；结构规则继续负责免生成完整表的冷启动路径。

## 验收

- 结构化 `TABLE` 不再逐条写入每个节点的 `m_rtShortest` / `m_rtOther`。
- exact port、主地址聚合、缺失端口回退、non-shortest 和 mixed 规则优先级通过测试。
- route class ID 的 8、16 和 32 位路径均有构造测试。
- `GetOutPort()` 和 RTP reservation 使用的候选查询都能访问相同索引。
- unified-bus 结构化路由测试通过，并记录实际验证命令。

## 验收结果

- 9,427,968 个端口查询连续三次均为 `missing=0`，checksum 一致。
- 配置释放后 RSS 中位数从旧完整表的 1,646.47 MiB 降到 400.02 MiB，下降 75.70%。
- 顺序、随机和 workload candidate lookup 中位数分别为 18.08 ns、76.03 ns 和 18.47 ns。
- 对应 `GetOutPort()` 中位数分别为 54.83 ns、206.24 ns 和 49.72 ns。
- `unified-bus-structured-routing` 测试通过。

完整数据和测量边界见 `next-hop-index-production-report.md` 和
`next-hop-index-production-summary.json`。
