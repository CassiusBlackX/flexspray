# 通用精确路由索引调查

## 结论

运行时不必依赖 `DIMENSION`、`TWO_LEVEL_SWITCH`、`DRAGONFLY` 等专用拓扑类型。

对当前 UB 接口，真正需要保存的是一个有限函数：

```text
(currentNode, destinationNode, destinationPort)
    -> (shortestPortSet, nonShortestPortSet)
```

只要离线阶段用现有路由语义精确得到这个函数，运行时可以使用完全不理解拓扑名称的紧凑
索引。拓扑专用规则仍有价值，但它应是可选的生成和压缩加速器，不应再是通用路由后端的
正确性前提。

最适合本项目的工程顺序是：

1. 先实现通用的 `port-set intern + packed action index`，获得接近普通查表的运行时速度。
2. 再比较 dense、sparse bitmap、range/trie 和 shared decision DAG 的实际字节数，逐块选择
   最小表示。
3. 保留现有 topology rules，专门负责避免全量路由生成并支持更大规模。
4. 专用识别器退到离线优化插件；识别失败时仍能编译为通用精确索引，不回到高开销
   `unordered_map` 全表。

不存在一个成熟算法能在任意图上同时保证：精确最短路、完整 ECMP、不增加报文标签、严格
次二次空间和常数级查询。通用后端必须按实际压缩效果选择表示，并保留局部 dense fallback。

## 当前实现为什么仍依赖专用类型

当前 C++ 运行时在 `UbTopologyRuleType` 中列出：

```text
DIMENSION
DIRECT_SWITCH
TWO_LEVEL_SWITCH
DRAGONFLY
```

`TopologyDistance()` 和 `AreTopologyNeighbors()` 分别解释这些类型。即使已经建立
`UbTopologyNextHopIndex`，查询仍会调用 `GlobalTopologyDistance()`、`TopologyDistance()` 和
`HasUpstreamSource()`。因此它是“先按类型解释，再计算候选端口”，不是已经编译完成的最终
next-hop 索引。

这个设计能用很少内存表示已知规则拓扑，但把拓扑识别能力和运行时查询能力绑在了一起。
新增一个拓扑家族通常需要同时增加 Python 编译逻辑、协议格式、C++ 枚举和距离分支。

## 三条不依赖拓扑类型的通用路径

### 1. Packed action index

先把所有不同的候选端口结果去重：

```text
routeClass 0 = miss
routeClass 1 = shortest [0], non-shortest []
routeClass 2 = shortest [1, 3], non-shortest []
...
```

每个查询项只保存一个 `routeClassId`。如果 route class 不超过 255 个，每项使用一个字节；
不超过 65535 个时使用两个字节。候选端口本身只在共享池中保存一次。

最简单的索引是：

```text
endpointIndex = endpointOffsets[destinationNode] + localPortIndex
routeClassId = portRoutes[currentNode * endpointCount + endpointIndex]
```

节点主地址查询使用独立的：

```text
nodeClassId = nodeRoutes[currentNode * destinationHostCount + destinationHostIndex]
```

查询只有边界检查和少量连续数组读取。它保留完整 shortest/non-shortest port set，不在运行时
扫描邻居或计算距离，也不要求目的节点 ID 呈现任何拓扑顺序。

这仍是最坏 `O(currentNodes * destinationEndpoints)` 的表示，但常数远小于当前逐项对象、哈希
表和智能指针组成的完整路由表。它是应该首先建立的通用精确基线。

### 2. Trie、ORTC 和共享 decision DAG

如果动作矩阵仍然过大，可以对同一个精确函数继续无损压缩：

- Patricia/radix trie：把连续地址前缀映射到一个 route class；
- ORTC：在最长前缀匹配语义下生成等价且表项数最小的前缀表；
- hash-consed multi-terminal decision DAG：把 key 的各个位作为判定变量，把 route class
  作为叶子，并全局合并相同子树；
- sparse bitmap + rank：只保存有效 cell，bitmap 判断 cell 是否存在，rank 找到紧凑值数组
  中的位置；
- row/block adaptive：每个 current、destination group 或固定大小 block 分别选择 dense、
  sparse、range 或 DAG。

这些结构都不需要知道 Clos、Torus 或 Dragonfly。它们压缩的是路由行为本身。

ORTC 和 Poptrie 的对象是 IP 前缀表。把排序后的完整 port set 作为一个不可分割的 next-hop
value，可以保持 ECMP 集合，但压缩率仍依赖节点编号和 port class 是否形成前缀。共享
decision DAG 可以跨 current node 合并相同子函数，对编号连续性的依赖更弱；代价是构建时间
和变量顺序敏感，最坏情况仍可能不压缩。

### 3. 通用 exact distance oracle

对于非负权图，最短 ECMP 集合可以按下式精确恢复：

```text
ECMP(u, t) = {
    localPort(u, v)
    where weight(u, v) + distance(v, t) == distance(u, t)
}
```

因此可以用 APSP distance matrix、hub labeling、2-hop cover 或 contraction hierarchy 保存精确
距离，查询时扫描当前节点的所有出端口并比较距离。这个路径适用于任意图，也不需要拓扑
类型。

但它并不天然返回完整 ECMP。Hub labeling、PLL、CH/CCH 的常用接口只回答距离或一条路径；
为了得到所有第一跳，仍需对每个邻居查询距离。对当前约 1 微秒的 topology-rule 路径，继续
做运行时距离查询和邻居扫描不太可能成为最低延迟方案。它更适合作为通用离线生成器或 packed
action index 的低内存备选后端。

`nonShortestPorts` 不能由上述最短路等式推出。它必须继续由当前 routing policy 生成和验证，
作为 route class 的第二个集合保存。报文选中非最短端口后，现有“一次绕路后强制最短”语义
保持不变。

## 1DPOD 1024-NPU 的实际信息量

使用当前权威拓扑和
`build_shortest_route_lookups_for_destination()` 重新流式统计，结果是：

| 项目 | 数量 |
| --- | ---: |
| 物理节点 | 2336 |
| 目的 NPU | 1024 |
| 目的端口 endpoint | 15360 |
| 有效端口查询 | 9427968 |
| 有效节点主地址查询 | 2145280 |
| 端口查询候选集合种类 | 138 |
| 节点查询候选集合种类 | 139 |

因此 miss 加 route class 仍能放进一个字节。完全不做结构压缩、只做最简单 dense packed index
时：

```text
portRoutes = 2336 * 15360 bytes = 34.22 MiB
nodeRoutes = 2336 * 1024 bytes  =  2.28 MiB
total core action matrix        = 36.50 MiB
```

如果 class 数超过 255，自动切到 16 位时是 73.00 MiB。只保存有效 cell 的值数组分别约为
8.99 MiB 和 2.05 MiB，但还要增加 bitmap/rank 或 block 索引。

这些数字是根据真实查询关系得到的核心 payload 估算，不是实现后的 RSS。对象头、endpoint
映射、candidate pool、对齐和 allocator 都会增加内存；反过来，分块稀疏编码也可能低于
36.50 MiB。必须实现原型并在独立进程中测量实际 RSS。

当前基准用于比较的已测结果是：

| 路径 | workload `GetOutPort` | 配置释放后 RSS |
| --- | ---: | ---: |
| 当前完整表 | 104 ns | 1646.47 MiB |
| 当前 topology next-hop index | 1047 ns | 225.83 MiB |

packed action index 的查询路径比当前完整表的哈希/对象表示更短，但在 C++ 原型完成前不能声称
一定快于 104 ns。合理的验收目标是：RSS 接近 topology rules，查询接近或优于完整表。

## 通用方法的理论边界

### Compact routing

Thorup-Zwick 等 compact routing 面向一般图，用 stretch 换空间。经典方案不保证精确最短路，
也不返回现有接口要求的完整 ECMP 候选集合。某些方案还依赖节点 label 或报文中的额外路由
信息，因此不能在不改变 UB 语义的情况下直接采用。

Peleg-Upfal 证明了通用路由的路径效率和路由信息空间之间存在上下界 trade-off。Gavoille 等人
进一步证明一般图的 exact distance labeling 每个节点需要 `Theta(n)` 位。Distance labeling
模型比本项目的共享集中索引更受限，不能直接当作当前实现的严格下界；但它足以说明不存在
一个对所有一般图都保持精确、局部小状态的万能标签方案。

### Interval routing

Interval routing 用目的编号区间选择方向，正好对应当前 exact/range rules 的思想。它可以对
任意图定义，但只有部分图类能以每个方向一个或少量 interval 保持 shortest routing。一般图
可能需要很多 interval，因此应当作为自动成本评估后的编码，而不是通用压缩保证。

### Hub labeling 和 2-hop cover

它们能在任意图上建立精确距离索引，并在许多真实稀疏图上获得很好的空间和查询时间。
Pruned Landmark Labeling 的公开接口也是 `ConstructIndex` 加 `QueryDistance`。它们不直接
返回完整 ECMP first-hop set；本项目必须再扫描本地物理端口并验证最短路等式。

### Graph automorphism、equitable partition 和 2-WL

Automorphism orbit 能严格说明哪些带属性节点或有序节点对可以通过对称映射复用结果。它是
无损压缩的可靠来源，但不规则图可能几乎没有非平凡 automorphism。

1-WL、2-WL 和 equitable partition 可以高效提出可能共享的节点或有序节点对。相同 WL 颜色
不等价于相同 automorphism orbit，也不证明 route class 一致。它们只能用来减少候选比较，
最终必须比较精确候选集合或通过物理端口级路由等价验证。

### Symbolic shortest path

BDD/ADD 可以把 adjacency relation、节点对和距离层做符号化表示，从而在高度规则但未标注的
图上自动共享大量状态。这条路线理论上能做到“不先命名拓扑类型就符号化求路由”，但变量
顺序会让 decision diagram 大小出现数量级差异，坏例子可能爆炸。CUDD 是成熟研究轮子，适合
做离线原型和正确性对照，不适合未经基准直接放入每包转发热路径。

## 对现有架构的建议

### 用户和输入层

用户继续只提供 Python/NetworkX、JSON/YAML 或自定义拓扑。用户不选择拓扑标签，也不需要
知道运行时采用哪种编码。

### 离线编译层

编译器产生权威 route action relation，并按成本自动选择：

```text
exact route relation
  -> intern shortest/non-shortest port sets
  -> split into blocks
  -> estimate dense / sparse / range / DAG bytes and query steps
  -> choose the cheapest valid encoding per block
  -> optionally replace proven regions with topology formula artifacts
```

如果 route relation 的生成本身太贵，结构识别器可直接生成等价关系；如果没有识别出结构，
通用 SSSP/APSP 路径仍能生成，不影响可用性。

### C++ 运行时层

`UbTopologyRuleType` 不再是唯一的低内存运行时入口。建议增加一个通用 immutable next-hop
action index，返回已有的：

```text
UbRouteCandidates
  shortestPorts
  nonShortestPorts
```

新增后的查找顺序需要明确固定，并保持现有 exact/range 对 topology rule 的优先覆盖语义。
另一种更简单的做法是在编译阶段把 exact、range、topology formula 和 table residual 合并成
一个已经证明等价的 action index，运行时不再扫描多个后端。

HASH/ADAPTIVE、`GetOutPort()`、`selectedShortestPath` 和一次绕路后的
`ForceShortestPathRouting()` 不需要修改。

## 建议的最小验证原型

第一版只实现 packed dense，不先引入 CUDD、Poptrie 或新的第三方依赖：

1. 从当前完整路由生成器流式接收 route action，不保留 Python 全量字典。
2. Intern 完整 `(shortestPorts, nonShortestPorts)`，选择 8/16/32 位 class ID。
3. 为 endpoint 和 destination host 建立连续编号。
4. 生成连续 dense 数组，miss 使用 class 0。
5. C++ 直接加载 packed binary；可选调试输出仍可导出 CSV。
6. 用现有 9427968 个端口查询、2145280 个节点查询和 workload corpus 做逐集合校验。
7. 使用相同 1000 万查询基准报告顺序、随机、workload 的平均值、P50、P99、QPS 和 RSS。

只有 dense 原型证明 payload 或缓存行为仍不够好，再增加 sparse bitmap 和 hash-consed decision
DAG。这个顺序能先验证“专用类型是否真的必要”，不会一开始引入高复杂度 symbolic runtime。

## 权威来源和现成实现

- Peleg, Upfal, *A tradeoff between space and efficiency for routing tables*, STOC 1988：
  <https://doi.org/10.1145/62212.62217>
- Thorup, Zwick, *Compact routing schemes*, SPAA 2001：
  <https://doi.org/10.1145/378580.378581>
- Gavoille, Peleg, Perennes, Raz, *Distance labeling in graphs*, Journal of Algorithms 2004：
  <https://doi.org/10.1016/j.jalgor.2004.05.002>
- Fraigniaud, Gavoille, *Interval Routing Schemes*, Algorithmica 1998：
  <https://doi.org/10.1007/PL00009211>
- Draves, King, Srinivasan, Zill, *Constructing optimal IP routing tables*, INFOCOM 1999：
  <https://doi.org/10.1109/INFCOM.1999.749256>
- Cohen, Halperin, Kaplan, Zwick, *Reachability and Distance Queries via 2-Hop Labels*：
  <https://doi.org/10.1137/S0097539702403098>
- Akiba, Iwata, Yoshida, *Pruned Landmark Labeling* 论文和作者实现：
  <https://doi.org/10.1145/2463676.2465315>
  <https://github.com/iwiwi/pruned-landmark-labeling>
- RoutingKit / Contraction Hierarchy：
  <https://github.com/RoutingKit/RoutingKit>
- Poptrie 论文和作者 reference implementation：
  <https://doi.org/10.1145/2829988.2787474>
  <https://github.com/pixos/poptrie>
- CUDD BDD/ADD/ZDD library：
  <https://github.com/SSoelvsten/cudd>
- nauty/Traces graph automorphism：
  <https://pallini.di.uniroma1.it/>

Poptrie reference implementation 明确限制为 education、research 和 evaluation，不能直接作为
本项目的通用发行依赖。RoutingKit 和 PLL 可用于 exact distance 原型，但不能直接满足完整
ECMP first-hop 接口。当前最小原型不需要升级 NetworkX，也不需要增加第三方依赖。
