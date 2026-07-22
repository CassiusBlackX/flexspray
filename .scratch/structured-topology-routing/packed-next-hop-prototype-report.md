# Packed next-hop index prototype

## 结论

这个 prototype 证明了一个不依赖 Ring、Clos、Dragonfly 等专用拓扑类型的运行时表示可行：
只要离线阶段能给出精确的 next-hop 候选关系，就可以把完整路由表编译成全局去重的候选端口
集合和紧凑 route class ID 矩阵。

在 1024 NPU 场景中，它保持完整表的候选端口语义，同时将相同物理拓扑下的进程 RSS 从
1646.47 MiB 降到 256.58 MiB，降低 84.4%。三类可直接对齐的 candidate lookup 都比完整
表更快，workload endpoint 查询从 44.95 ns 降到 25.28 ns。

因此推荐的生产方向不是在“专用结构规则”和“通用索引”之间二选一，而是统一成：

```text
结构规则直接生成 ─┐
                  ├─> 精确 next-hop 候选关系 ─> packed runtime index
完整路由生成器 ────┘
```

结构规则解决冷启动问题，避免生成全量最短路；packed index 解决运行时内存和查询速度问题，
并为无法识别结构的自定义拓扑提供通用后端。

## 验证规模与正确性

- 物理拓扑：2336 个节点、1024 个 NPU。
- 目的地址：15360 个端口地址。
- 源完整表：9427968 条端口路由。
- 节点主地址 corpus：2145280 条查询。
- workload corpus：65536 条 endpoint 查询。
- route class：139 种，端口矩阵和节点矩阵均自动选择 8 位 ID。
- 三次独立进程中，所有 corpus 均为 `missing=0`，完整校验和一致。

构建使用两遍流式读取：第一遍发现并编号去重候选集合，第二遍填充矩阵并逐条核对源路由。
它没有在内存中物化 9427968 个 C++ route 对象。

## 内存

| 指标 | 中位数或确定值 |
| --- | ---: |
| 端口 route class 矩阵 | 35,880,960 bytes |
| 节点主地址矩阵 | 2,392,064 bytes |
| 索引估算总分配 | 38.90 MiB |
| packed index 独立进程 RSS | 53.53 MiB |
| 相同物理拓扑加 packed index RSS | 256.58 MiB |
| 峰值 RSS | 345.55 MiB |
| 完整表 RSS 基线 | 1646.47 MiB |
| `UbTopologyNextHopIndex` RSS 基线 | 225.83 MiB |

packed index 加物理拓扑比完整表少 1389.89 MiB，降低约 84.4%；比当前
`UbTopologyNextHopIndex` 多 30.75 MiB，约 13.6%。这部分空间换来了不依赖拓扑 pattern
的精确索引，以及更好的随机查询缓存行为。

## 查询性能

每行是三个独立进程的中位数。每个进程先预热 100000 次，再测量 10000000 次查询。
P50/P99 是 4096 次查询批次平均延迟的分位数，不是单包尾延迟。

| 查询分布 | Mean | P50 | P99 | QPS |
| --- | ---: | ---: | ---: | ---: |
| 端口路由，顺序 | 15.28 ns | 14.70 ns | 25.33 ns | 65.45 M |
| 端口路由，随机 | 81.44 ns | 79.96 ns | 127.20 ns | 12.28 M |
| 节点主地址，顺序 | 21.45 ns | 24.36 ns | 35.53 ns | 46.63 M |
| 现有 workload endpoints | 25.28 ns | 24.68 ns | 38.93 ns | 39.56 M |

与相同 corpus 的现有 candidate lookup 中位数比较：

| 查询分布 | 完整表 | Topology index | Packed | Packed 相对完整表 |
| --- | ---: | ---: | ---: | ---: |
| 端口路由，顺序 | 30.80 ns | 347.00 ns | 15.28 ns | 2.02x 更快 |
| 端口路由，随机 | 244.52 ns | 581.81 ns | 81.44 ns | 3.00x 更快 |
| 现有 workload endpoints | 44.95 ns | 820.40 ns | 25.28 ns | 1.78x 更快 |

## 构建时间

- 两遍流式 packed 构建中位数：10.84 s。
- 清空路由后安装相同物理拓扑中位数：0.541 s。

这个构建时间仍然发生在完整路由已经生成之后。因此 prototype 只证明了通用压缩和运行时
查询，不证明它能替代结构规则的直接生成。对于可识别结构，应直接生成精确候选关系再编译
packed index；对于无法识别的部分，才运行通用最短路生成器。

## 尚未证明的边界

- 当前只 benchmark candidate lookup，尚未接入并测量真实 `GetOutPort()` 调用链。
- 这个 1K 输入没有 non-shortest 候选；数据结构支持分别保存 shortest/non-shortest，但仍需
  一个显式构造案例验证一次绕路语义。
- 139 个 route class 只覆盖了 8 位 ID 路径；16 位和 32 位自动升级仍需构造测试。
- workload corpus 只有 endpoint 对，不含逐包逐跳 trace，不能称为真实转发查询分布。
- 三次样本适合报告中位数和可见波动，不足以给出窄置信区间。

## 复现

当前已构建 prototype 可用一条命令复跑 1K 测量：

```bash
tmp/packed-next-hop-prototype/benchmark_packed_next_hop \
  tmp/packed-next-hop-prototype/1k-table.protocol \
  tmp/packed-next-hop-prototype/1k-port.queries \
  tmp/packed-next-hop-prototype/1k-node.queries \
  tmp/packed-next-hop-prototype/1k-workload.queries \
  tmp/packed-next-hop-prototype/1k-packed-result.json \
  --topology-protocol tmp/packed-next-hop-prototype/1k-topology.protocol
```

原始三次测量保存在 `packed-next-hop-prototype-run1.json` 至
`packed-next-hop-prototype-run3.json`。
