# 通用 next-hop index 生产接入验收

## 结论

`UbNextHopIndex` 已接入结构化输入的 `TABLE` 运行路径。它不依赖 Ring、Clos、Dragonfly
等特定拓扑类型，输入只要能给出精确的 shortest/non-shortest 候选端口，就能使用同一个
共享索引。

正式 1024 NPU 路径的 9,427,968 个端口查询连续三次均为 `missing=0`，checksum 完全一致。
HASH/ADAPTIVE、入端口过滤和一次绕路后的强制最短路语义没有改变。

## 生产路径

```text
规则路由
  -> UbNextHopIndex
  -> 旧 CSV / 手工路由表
```

命中一个来源后完整返回该来源的 shortest/non-shortest 候选，不跨来源合并。结构化
`TABLE` 在解析时直接编译，不再先物化数百万个 `UbStructuredTableRoute`，也不再逐条写入
每个 `UbRoutingProcess` 的旧表容器。手工构造 `UbStructuredTopologyConfig::tableRoutes`
仍然可用；旧 CSV 和 `AddShortestRoute()` / `AddOtherRoute()` 的行为未改。

## 正确性

| 项目 | 结果 |
| --- | ---: |
| 节点 | 2,336 |
| NPU | 1,024 |
| 物理链路 | 19,968 |
| 输入路由 | 9,427,968 |
| 全量查询 | 9,427,968 |
| 独立进程样本 | 3 |
| 缺失查询 | 0 |
| candidate checksum | 7,603,632,706,628,156,928 |

单元测试还覆盖 exact port、主地址聚合、目的端口缺失回退、重复行合并、non-shortest、
入端口过滤、规则优先、packed miss 回退旧表、真实 `GetOutPort()`，以及 8/16/32 位
route class ID。

## 内存

以下 RSS 均包含正式安装的 ns-3 节点、链路和 routing process，是同口径的生产进程结果。

| 运行时表示 | 配置释放后 RSS | 相对旧完整表 |
| --- | ---: | ---: |
| 旧完整表 | 1,646.47 MiB | 基线 |
| 通用 packed index | 400.02 MiB | -1,246.45 MiB (-75.70%) |
| 直接 topology rules | 221.48 MiB | -1,424.98 MiB (-86.55%) |

packed 路径三次测量的活堆中位数为 236.77 MiB，峰值 RSS 中位数为 400.02 MiB。
它高于 prototype 裸索引加简化物理拓扑的 256.58 MiB，因为正式路径会创建全部 ns-3
对象；与旧完整表相比，仍然消除了主要的逐目的路由存储。

## 冷启动

RSS benchmark 中位数：

| 阶段 | 旧完整表 | packed index |
| --- | ---: | ---: |
| 解析 | 6.168 s | 3.819 s |
| 安装 | 10.703 s | 0.327 s |
| 两阶段合计 | 16.871 s | 4.147 s |

安装时间下降来自共享索引：不再把 9,427,968 条路由分发进每个节点的哈希表。解析仍需读取
155 MiB 的完整 `TABLE` 文本，因此无法达到直接 topology rules 的约 0.8 秒量级。

## 在线查询

每项为三个独立进程的中位数，每个进程执行 9,427,968 次查询并预热 100,000 次。

| 查询分布 | packed candidate | 旧完整表 candidate | packed `GetOutPort()` | 旧完整表 `GetOutPort()` |
| --- | ---: | ---: | ---: | ---: |
| 全量顺序 | 18.08 ns | 25.30 ns | 54.83 ns | 131.98 ns |
| 全量随机 | 76.03 ns | 170.86 ns | 206.24 ns | 601.89 ns |
| 现有 workload endpoints | 18.47 ns | 40.74 ns | 49.72 ns | 145.39 ns |

相对旧完整表，packed candidate lookup 分别快 1.40x、2.25x 和 2.21x；完整
`GetOutPort()` 分别快 2.41x、2.92x 和 2.92x。旧完整表数字来自此前同机、相同 corpus 和
benchmark 的三进程中位数；两轮使用的总迭代数不同，因此这里报告每次查询的平均延迟，
不把总运行时间直接比较。

这说明通用索引不是用内存换取更慢的规则解释：它在运行时直接读取紧凑 class ID，再返回
去重候选集合。当前 workload 只提供 endpoint 对，不包含逐包逐跳 trace，因此不能把该行
解释成完整业务仿真的单包延迟。

## 边界

- 结构化 `TABLE` 的流式编译要求 `NODES` 按 canonical node ID 排列；解析器会显式拒绝
  不满足条件的输入。
- topology rules 继续保持独立运行时表示，不在本任务中展开为稠密矩阵。因此它仍拥有最低
  冷启动和内存，packed index 则覆盖无法识别结构但已有精确路由的通用场景。
- 完整 `unified-bus` 测试套件仍会在已有 CTP 用例失败：`ub-test.cc:1243` 的
  `TaSsnSndNxt` 实际值为 1、期望值为 0；该失败与本任务无关。

## 验证

```bash
./build/utils/ns3.44-test-runner --test-name=unified-bus-structured-routing
```

结果：`PASS unified-bus-structured-routing`。
