# 结构化拓扑与规则路由优化总览

状态：已在 `next` 落地并通过阶段验收
更新日期：2026-07-22

本文是该功能的入口文档，解释问题、设计演进、最终运行路径、正确性边界和当前性能结论。
详细方法、原始样本和历史阶段结果仍保存在本目录的专项报告中。

## 为什么做

原有路由路径为每个当前节点和目的地址保存候选端口。规则拓扑扩大到 1,024 NPU 时，
1DPOD 实图包含 2,336 个节点和 19,968 条物理链路，需要 9,427,968 个目的端口查询项。
逐目的表示带来四类成本：

1. 离线生成需要对大量源和目的组合求路；
2. 输入文件和解析过程需要处理数百万条记录；
3. 每个节点的哈希表和候选数组占用大量常驻内存；
4. 大表随机访问的缓存局部性较差。

单纯压缩 CSV 连续范围只能减少文本重复，无法消除运行时逐目的对象。Nüwa 的可借鉴点是把
路由视为编译问题：先验证物理图中的规则结构，再把结构和路由策略编译成共享规则。这里没有
要求用户声明 leaf、spine、pod 等角色，也没有假设任意图都能被一种通用公式压缩。

## Issue 16 覆盖范围

本功能处理 Issue 16 中由拓扑与路由表示直接造成的成本：大规模路由离线生成、完整表输入、
C++ 路由安装、路由常驻内存和候选端口查询。1024-NPU 结果证明这一部分可以从逐目的表改为
拓扑规则或通用 packed index。

它不限制同时激活的 traffic task，也不改变 AllToAll 的任务数量、报文数量或仿真语义；
不负责压缩 RTP/Jetty/WQE、packet/event backlog 或 MTP worker 状态；也不自动关闭 trace。
因此它是 Issue 16 的一个独立优化切片，不能单独证明 1024 x 1023 AllToAll 的 36 小时业务
仿真和 400 GB 日志问题已经全部解决。后两项仍需要端到端 profile、任务与事件生命周期优化，
以及按证据需要选择 trace 开关。

## 设计演进

### 第一阶段：先证明替换正确

第一阶段保留完整基准表，通过 NetworkX 图提出结构候选，回到包含物理端口和并行链路的
权威图验证，再逐查询项比较候选端口。只有完全等价的查询项才交给规则负责。

该阶段证明了表、规则和混合表示可以保持候选端口集合一致，但冷缓存仍会生成完整表，不能
作为最终性能方案。历史结果见 [stage1-report.md](stage1-report.md)。

### 第二阶段：避免生成完整基准表

默认最短路径由目的 DAG 直接产生候选端口，并把重复结果编译成 exact/range 规则。随后，
完整且满足安全组合条件的结构进一步改为坐标或分组拓扑规则，不再构造逐目的 DAG。

当前直接规则覆盖 Ring、Mesh、Torus、ND FullMesh、UBX、1DPOD 多平面分层网络、经典
Dragonfly、pair-design 网络、FullMesh 和交换网络组合，以及交换机端口扩展。部分结构或
存在不安全重叠时，`mixed` 只让验证通过的查询项使用规则，其余查询项使用表。

演进结果见 [stage2-report.md](stage2-report.md) 和
[topology-rule-report.md](topology-rule-report.md)。

### 运行时：分别优化规则和表

拓扑规则最初每次查询都扫描规则、计算距离并遍历邻接端口。`UbTopologyNextHopIndex` 将
结构成员和物理连接编译成紧凑索引，避免重复解释规则。结果见
[topology-next-hop-index-report.md](topology-next-hop-index-report.md)。

无法识别结构但已有完整候选集合时，`UbNextHopIndex` 将结构化 `TABLE` 编译成共享的
route-class 索引。它不依赖任何拓扑类型，是任意精确候选表都能使用的通用后端。结果见
[next-hop-index-production-report.md](next-hop-index-production-report.md)。

## 最终运行路径

```text
Python + NetworkX -----------\
                              -> NetworkSimulationGraph
JSON/YAML adapter ----------/          |
                                         v
                              规范化物理节点、端口和链路
                                         |
                                         v
                              结构发现 -> 物理验证 -> 路由编译
                                         |
                                         v
                              structured_topology.txt
                              structured_summary.json
                                         |
                                         v
                         scratch/run_structured_topo_case.py
                                         |
                                         v
                             Unified Bus C++ runtime
                       rule -> packed index -> existing CSV
                                         |
                                         v
                 SelectOutPort(HASH/RR/ADAPTIVE/packet spray)
```

`ns-3-ub-tools` 只准备 case，不启动 ns-3。scratch runner 只读取已准备目录，不导入
NetworkX，也不识别结构。仿真开始后不存在 Python 与 C++ 的跨语言运行时交互。

现有 CSV case 是并列路径，不是兼容模式或待删除路径。诊断 CSV 只在显式请求时导出，
prepared case 的正式交接文件是 `structured_topology.txt`。

## 路由表示及适用范围

| 表示 | 数据来源 | 主要优势 | 主要代价 |
| --- | --- | --- | --- |
| 现有 CSV 表 | 逐目的或范围记录 | 行为稳定，支持手工路由 | 大规模逐目的存储和随机访问成本高 |
| packed next-hop index | 精确 shortest/non-shortest 候选 | 不依赖拓扑类型，查找和内存都优于旧完整表 | 输入仍需包含完整 `TABLE`，解析成本仍存在 |
| exact/range rules | 局部重复候选 | 支持部分结构和精确例外 | 压缩率依赖目的连续性 |
| topology rules | 已验证坐标、分组和物理连接 | 不生成完整表，冷启动和内存最低 | 在线需要解释结构，当前仍慢于完整表查询 |
| mixed | 每个查询项选择一种来源 | 任意图保持可运行，规则区域获得收益 | 需要清晰的查询归属和验证边界 |

一个查询项由“当前节点、目的节点、目的 UB 端口”确定。命中一个来源后，该来源完整返回
shortest 和 non-shortest 候选；不同来源的候选集合不会合并。

## 1024-NPU 结果

### 结构与正确性

1DPOD 实图被识别为 128 个组内 FullMesh 和 8 个带本地/共享 upper 的分层交换网络：

| 项目 | 结果 |
| --- | ---: |
| 总节点 | 2,336 |
| NPU | 1,024 |
| 物理链路 | 19,968 |
| 拓扑规则 | 136 |
| 规则成员映射 | 10,528 |
| 目的端口全量查询 | 9,427,968 |
| 缺失、额外或候选不一致 | 0 |
| node-primary 全量查询 | 2,145,280 |
| node-primary 校验不一致 | 0 |

校验比较完整候选集合及 checksum，不只比较端口数量或第一个端口。

### 离线生成与输入体量

逐目的流式暴力求解与拓扑规则生成使用相同物理图：

| 指标 | 完整表 | topology rules | 收益 |
| --- | ---: | ---: | ---: |
| 离线生成 | 17.98 s | 1.37 s | 13.1x |
| 序列化输入 | 162,121,150 B | 789,816 B | -99.51% |
| 查询记录或规则 | 9,427,968 | 136 | 不再逐目的展开 |

这些数字来自同机冷进程单次离线结果，不包含 `tracemalloc`。详细方法和其他拓扑结果见
[topology-rule-report.md](topology-rule-report.md)。

### 运行时内存

以下是创建真实 ns-3 节点、链路和 routing process 后，配置释放时的独立进程 RSS：

| 运行时表示 | RSS 中位数 | 相对旧完整表 |
| --- | ---: | ---: |
| 旧逐目的完整表 | 1,646.47 MiB | 基线 |
| packed next-hop index | 400.02 MiB | -75.70% |
| topology rules + next-hop index | 225.83 MiB | 约 -86.3% |

旧完整表与拓扑规则的活堆诊断分别为 874.52 MiB 和 201.05 MiB。RSS 差异大于活堆差异，
因为 macOS allocator 在解析数百万对象后保留了地址空间。详细原始样本见
[rss-1024-report.md](rss-1024-report.md) 和 `rss-1024-*.json`。

### 冷启动

不同专项进程得到两个互补结论：

- 旧完整表到 packed index：解析加安装由 16.871 s 降到 4.147 s；
- 同一 topology-rule lookup benchmark：完整表 setup 为 14.827 s，拓扑规则为 0.548 s。

两组数字来自不同进程组，不能组合成精确的单项归因。它们共同证明：packed index 消除了
逐节点安装成本，而 topology rules 进一步避免读取完整 `TABLE`。

### 在线查询

在线结果必须按表示分别解释。

通用 packed index 相对旧完整表，在相同 corpus 的三进程中位数中更快：

| 查询分布 | packed candidate | 旧表 candidate | packed `GetOutPort` | 旧表 `GetOutPort` |
| --- | ---: | ---: | ---: | ---: |
| 全量顺序 | 18.08 ns | 25.30 ns | 54.83 ns | 131.98 ns |
| 全量随机 | 76.03 ns | 170.86 ns | 206.24 ns | 601.89 ns |
| workload endpoints | 18.47 ns | 40.74 ns | 49.72 ns | 145.39 ns |

拓扑规则的 next-hop index 相对旧解释器已经明显加速，但仍慢于完整表：

| 查询分布 | 完整表 candidate | topology index candidate | 完整表 `GetOutPort` | topology index `GetOutPort` |
| --- | ---: | ---: | ---: | ---: |
| 端口顺序 | 30.80 ns | 347.00 ns | 113.57 ns | 414.90 ns |
| 端口随机 | 244.52 ns | 581.81 ns | 565.79 ns | 775.97 ns |
| workload endpoints | 44.95 ns | 820.40 ns | 104.32 ns | 1,047.46 ns |

因此当前结论不是“结构规则所有方面都更快”：它主要优化离线生成、输入体量、冷启动和
常驻内存。packed index 则证明通用紧凑表可以同时改善内存和在线查找。workload corpus
只有 endpoint 对，不包含逐包逐跳查询，不能解释成完整业务仿真的单包延迟。

## 行为是否改变

结构化优化只改变候选端口的产生和存储：

- `GetOutPort()` 外部入口不变；
- `RoutingTypeUsesShortestPaths()` 的过滤不变；
- 命中规则或索引后仍进入原 `SelectOutPort()`；
- HASH、RR、ADAPTIVE、packet spray 和 ingress stripe 的最终选择逻辑不变；
- 入端口过滤、主地址回退和 CSV miss fallback 保持原边界；
- non-shortest 被选中后，报文继续使用现有的一次绕路后强制最短语义。

专项测试覆盖 exact/range 优先级、规则 miss 回退 CSV、packed exact port、node-primary
聚合、重复行合并、非法协议、非法端口、真实 `GetOutPort()` 和原 routing-modes 回归。

## 当前限制

- 自动规则只用于完全验证的结构或查询项；未知结构继续使用 exact/range 或表。
- topology next-hop index 当前主要加速最短候选；non-shortest 仍由已验证 exact/range 规则
  或表提供。
- 当前 metric 表示只保留最短候选组和一个合并的 non-shortest 候选组，不保留任意数量的
  数值 metric 层级。代码在 `ub-utils.cc` 中保留了 TODO。
- 自定义 path finder 可能具有非标准语义，因此继续使用完整基准验证和内容缓存。
- endpoint workload benchmark 不等于逐包逐跳真实流量分布；要回答业务查询延迟，需要采集
  至少包含当前节点和目的地址的 route-lookup trace。

## 提交对应关系

| 提交 | 内容 |
| --- | --- |
| 子模块 `359adad` | NetworkX/JSON/YAML 输入、结构发现与验证、table/rule/mixed 编译、prepared case |
| 父仓库 `e92960f` | 将 `ns-3-ub-tools` 指向结构化拓扑编译器提交 |
| 父仓库 `c71eb2a` | 设计稿、测试拓扑、benchmark、原始样本和阶段报告，增加 scratch runner |
| 父仓库 `724e9fe` | `UbRuleRoutingTable`、`UbTopologyNextHopIndex`、`UbNextHopIndex` 和 C++ 安装路径 |
| 父仓库 `71d5ad6` | prepared case 用户文档和统一领域用词 |
| 父仓库 `0e060dd` | 优化总览、1024-NPU 结果索引和 ADR 0002 |
| 子模块 `895335c` / 父仓库 `d530fb8` | README 改用仓库内现有组合拓扑示例 |
| 父仓库 `3016963` | scratch 用户入口说明 CSV 与 prepared structured 两条并列路径 |

## 证据索引

- [design.md](design.md)：完整领域模型、输入约束、结构发现和分阶段设计；
- [stage1-report.md](stage1-report.md)：先证明正确性的第一阶段；
- [stage2-report.md](stage2-report.md)：直接 DAG 和 exact/range 编译的阶段历史；
- [topology-rule-report.md](topology-rule-report.md)：纯坐标/分组规则和离线收益；
- [topology-next-hop-index-report.md](topology-next-hop-index-report.md)：拓扑规则运行时索引；
- [next-hop-index-production-report.md](next-hop-index-production-report.md)：通用 packed index 生产接入；
- [rss-1024-report.md](rss-1024-report.md)：独立进程内存与初始化方法；
- [lookup-1024-index-report.md](lookup-1024-index-report.md)：当前拓扑索引查询 benchmark；
- `*-summary.json` 和 `*-run*.json`：机器可读聚合结果与独立进程样本；
- `benchmark_*.cc`、`benchmark_*.py`：可复核 benchmark 实现。
