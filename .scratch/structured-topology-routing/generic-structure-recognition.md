# 泛用结构识别技术路径

## 目标和边界

用户只通过 Python 和 NetworkX 描述真实节点、物理链路和端口。工具不要求用户选择 Clos、
BST、FullMesh、plane、pod 等标签，而是从图中发现可以无损压缩的重复结构，并用现有 mixed
路由保证未识别部分仍然正确运行。

“泛用”不等于任意图都能压缩。一个没有重复关系的随机图，其最短描述可能就是全部节点和
边。正确目标是：自动找到图中存在的规则，证明规则能无损展开，并把剩余部分显式保存。

现成工具的能力、许可证和采用建议见
`authoritative-graph-tools-survey.md`。结论是保留 NetworkX 默认主路径，把 SageMath、
python-igraph/Bliss 和 OR-Tools 分别作为离线 reference、canonical labeling 和候选选择的
可选后端，不存在可以替代整个识别与验证链的一体化工具。

## 保留的稳定边界

当前四层可以继续复用：

1. `PhysicalTopology` 保存节点、端口、每条物理链路和并行链路。
2. `TopologyStructure` 保存结构坐标、结构边、本地端口组和目的覆盖。
3. `verify_topology_structures()` 负责物理展开验证和链路唯一归属。
4. 路由编译器比较完整图和规则图的候选端口，生成规则或必要表项。

要泛化的是候选发现和多个候选之间的选择，不需要先改 C++ 运行时。

## 内部抽象

通用发现层建议只引入四个简单概念：

```text
GraphStructure
  NodeGroup
    members
    indexByNode

  GroupConnection
    leftGroup
    rightGroup
    rule
    physicalLinkIds

  UnmatchedNode
  UnmatchedLink
```

`GroupConnection.rule` 第一批只表达可精确定义的数学关系：

- `allToAll`：两组之间所有节点对都有边；
- `oneToOne`：存在一个双射，每个节点只连接对应节点；
- `fixedDegree`：两侧每个节点具有固定连接数，但还不能进一步解释；
- `pairDesign`：一侧节点对恰好共同连接固定数量的另一侧节点；
- `cartesianDimension`：边只改变坐标中的一个维度；
- `explicit`：保存真实连接，不声称存在更短规则。

这些名称描述连接关系，不描述产品拓扑家族。BST、Clos 或二维 FullMesh 只是这些关系的
组合结果。

## 理论和职责

| 理论或算法 | 解决的问题 | 不能单独证明什么 |
| --- | --- | --- |
| 类型化多重图 | 保存端口、并行链路和链路属性 | 不负责发现重复结构 |
| 度数与邻接签名 | 低成本排除不可能同组的节点 | 签名相同不代表节点可互换 |
| 1-WL 颜色细化 | 自动得到稳定的候选节点分区 | 对高度对称图可能把全部节点留在同一组 |
| Equitable partition | 判断组内节点连接到其他组的数量是否一致 | 数量相同不代表具体连接关系相同 |
| Twin / modular decomposition | 发现相同外部邻居的可替换节点或模块 | 无法恢复二维 FullMesh 等坐标结构 |
| Quotient graph | 把已验证节点组折叠成上层图并递归分析 | 商图依赖正确分组，不能反向证明分组 |
| 二部关联图与组合设计 | 识别 BST、Steiner system、BIBD 等 pair-cover 结构 | 参数满足整除公式不代表组合设计一定存在 |
| Cartesian product factorization | 恢复 Mesh、Torus、Dimension FullMesh、Hypercube 的维度 | 不适用于任意重复子图 |
| Automorphism orbits | 找到对称图中可互换节点的轨道 | 自动同构群计算在大对称图上可能昂贵 |
| VF2++ 图同构 | 确认候选和模板相同并取得节点映射 | 不直接验证物理端口、并行边和链路属性 |
| Set packing / exact cover | 从重叠候选中选择不重复拥有物理链路的组合 | 全局最优选择一般可能是 NP-hard |
| Minimum Description Length | 在多个正确分解中选择序列化更小的结果 | 只能优化表示，不能作为正确性证明 |
| 最短路 DAG 等价 | 证明规则图返回与物理图相同的候选端口 | 只覆盖当前已声明的路由和 metric 语义 |

## 发现过程

### 1. 构造两个图视图

权威视图使用物理多重图，每条链路包含：

```text
linkId
leftNode + leftPort
rightNode + rightPort
bandwidth + delay
routeRelevantAttributes
```

候选视图使用简单图，把并行链路折叠成一条带签名的边。边签名至少包含并行数量和所有参与
路由决策的属性集合。NetworkX 的 WL 和大部分同构预筛选在简单图上执行，任何候选最终都
回到物理多重图验证。

### 2. 初始颜色

为每个节点计算初始颜色：

```text
nodeType
simpleDegree
physicalPortCount
neighborNodeTypeCounts
parallelLinkCountHistogram
routeRelevantEdgeSignatureHistogram
```

初始颜色不同的节点永远不进入同一候选组。这一步是线性扫描，复杂度为 `O(V + E)`。

### 3. WL 颜色细化

每轮将节点当前颜色与邻居的 `(edgeSignature, neighborColor)` 多重集合组合并重新编号，直到
颜色不再变化：

```text
color[t+1](v) = canonical(
  color[t](v),
  multiset(edgeColor(v,u), color[t](u))
)
```

稳定颜色类是候选 `NodeGroup`。执行 `t` 轮的复杂度约为 `O(tE)`。WL 只负责缩小搜索空间；
不同图可能产生相同颜色，因此不能直接生成规则。

### 4. 分析组间连接矩阵

对每对候选组，只统计实际存在的边，不建立稠密邻接矩阵。按以下顺序判断：

1. 边数是否等于两组大小乘积，若是则候选 `allToAll`；
2. 两侧度数是否全为 1 且大小相同，若是则候选 `oneToOne`；
3. 两侧度数是否分别恒定，若是则得到 `fixedDegree`；
4. 是否存在稳定 index 映射或坐标差约束；
5. 其余连接保持 `explicit`。

每个关系都记录实际 `physicalLinkIds`，不能只记录计数。

### 5. 识别组合设计和 BST

BST 的一个 group 可以抽象成二部关联图：

- lower switch 是 point；
- upper switch 是 block；
- 一条 lower-upper 链路表示 point 属于 block；
- 任意两个 lower switch 恰好共同属于一个 block。

这就是 `2-(v,k,1)` block design。设 lower 数量为 `v`、每个 upper 的 lower 邻居数为 `k`，
每个 lower 的 upper 邻居数为 `r`，upper 数量为 `b`，必须满足：

```text
r = (v - 1) / (k - 1)
b = v(v - 1) / (k(k - 1))
bk = vr
```

这些公式只用于快速排除。真正验证通过枚举每个 block 内的节点对并计数：

```text
pairCount[a,b] == 1, for every a != b
```

复杂度是 `O(bk^2)`，而不是对每个 lower pair 扫描全部 block 的 `O(v^2b)`。论文中的
`bst(7,3,1)` 是 Fano plane，`v=7, k=3, r=3, b=7`。

这种识别器输出 `pairDesign`，不依赖输入或用户把它命名为 BST。参数满足公式但缺边、重边
或 pair 重复时均不采用。

### 6. 恢复乘积坐标

WL 无法区分很多高度对称图。例如二维 Dimension FullMesh 的所有节点可能保持同色。对
仍未解释且规则度数一致的连通分量，尝试图乘积分解：

```text
G = G1 square G2 square ... square Gn
```

完全图因子得到 Dimension FullMesh，Path 因子得到 Mesh，Cycle 因子得到 Torus，`K2`
因子得到 Hypercube。NetworkX 负责构造候选模板，`could_be_isomorphic()` 快速排除，VF2++
取得物理节点到坐标的映射。只有映射后的边集合完全一致才接受。

实现层统一输出 `dimension-network`，不为产品名或维度数增加新类型。每个因子记录：

```text
connection  line | ring | fullMesh
size        该维度的坐标数量
```

Ring 是单个 `ring` 因子；Mesh、Torus 和 ND Dimension FullMesh 分别由全部 `line`、`ring`
或 `fullMesh` 因子组成。因子可以混合。由于 `Path(2)` 和 `Clique(2)` 相同，`Cycle(3)` 和
`Clique(3)` 也相同，这些小因子不能仅凭邻接结构恢复用户原本的语义标签。识别器使用规范
选择规则保存一种等价表示；路由正确性只依赖展开后的物理边，因此不受该命名等价影响。

### 7. 识别经典 Dragonfly

Dragonfly 不能只靠度数识别。识别器先在路由器诱导图中提出等大小 clique 分区，再在组级
商图和物理图上同时验证：

1. 每个路由器只属于一个组，组内路由器两两直连；
2. 任意两个组之间恰好有一对全局相邻路由器，该路由器对可以有并行物理链路；
3. 每台路由器具有相同的全局链路数；
4. 组级商图是完全图；
5. 每台路由器连接相同数量的终端，每个终端只连接一台路由器；
6. 组内、全局和终端边覆盖完整；
7. 端口、并行链路和路由相关属性与物理图一致。

经典平衡 Dragonfly 还满足组数 `g = a * h + 1`，其中 `a` 是每组路由器数，`h` 是每台
路由器的全局链路数。该公式只用于快速排除和报告参数，不能代替逐组对和逐物理链路验证。
如果组间有多条链路、组内不是 clique 或全局度数不一致，候选不按经典 Dragonfly 接受，
但其他已验证区域仍可由 mixed 模式使用。

### 8. 递归分析 quotient graph

已验证的节点组折叠成 quotient node；组间连接关系成为 quotient edge。然后在 quotient graph
上重复颜色细化和关系识别，以发现：

- 多个相同设备组；
- 多个网络平面；
- 内部结构加外部互连；
- 多级层次结构。

递归只对已验证分组执行。每层都保存到原始节点和物理链路的映射，避免丢失端口所有权。

### 9. 选择重叠候选

不同发现器可能对同一批链路给出多个正确描述。先独立验证候选，再建立候选冲突图：共享
物理链路的两个候选之间连冲突边。

选择目标使用 MDL 风格成本：

```text
totalCost = structureBytes + residualLinkBytes + estimatedRouteBytes
```

第一版使用确定性的贪心选择：优先减少 `totalCost` 最多的候选，随后只处理未拥有链路。
只在很小的冲突分量中使用精确 set-packing 搜索，避免大图上的指数复杂度。

### 10. 物理正确性证明

候选采用前必须验证：

- 结构节点与物理节点双射；
- 节点类型一致；
- 展开后的每条结构边对应完全相同的物理端点；
- 覆盖全部并行物理链路；
- 本地端口组引用真实端口；
- bandwidth、delay 和参与路由决策的属性相容；
- 同一物理链路只有一个结构拥有者；
- 结构内部没有未解释的物理链路。

当前验证器已经覆盖节点、端点、并行链路、端口和所有权。属性相容验证需要在引入带权路由
前补齐。

### 10. 路由行为证明

Equitable partition、图同构和物理展开都不能单独证明运行时路由相同。最终仍按目的节点在
完整图和规则图上分别构造最短路 DAG，比较：

```text
(currentNode, destinationNode, destinationPort)
  -> shortest candidate ports
  -> non-shortest candidate ports
```

两个候选集合完全相同才由规则负责，否则 mixed 模式保存表项。全结构覆盖时复用同一棵
DAG，不重复计算。未来若引入带权最短路，这一层改用 Dijkstra DAG，并把权重属性加入物理
验证和缓存键。

## 不采用的捷径

- 不用社区发现决定结构。社区算法优化割或密度，结果近似且通常不唯一。
- 不把 WL hash 相同当作同构证明。
- 不在大图上枚举全部子图同构或全部 automorphism。
- 不把满足 BIBD 整除公式当成 block design 已存在。
- 不因为节点 ID 连续、组名相同或端口范围相似就采用结构。
- 不把结构发现逻辑放进逐包运行时。

## NetworkX 3.6.1 的使用边界

直接使用：

- `connected_components()`：拆分独立候选区域；
- `could_be_isomorphic()`：度数和三角形快速排除；
- `vf2pp_isomorphism()` / `vf2pp_is_isomorphic()`：精确映射；
- `cartesian_product()`：构造乘积模板；
- `single_source_shortest_path_length()`：无权最短路 DAG。

计划加入：

- `quotient_graph()`：把已验证分组折叠成上层图，再递归发现多级和多平面关系；
- `weisfeiler_lehman_graph_hash()` 和节点子图 hash，用于候选分桶和缓存指纹；
- ISMAGS，仅在需要处理小规模对称候选时评估。

WL hash 不支持 MultiGraph，因此只用于简化候选图。NetworkX 版本、WL 轮数、标签字段和
hash 长度必须进入缓存键。当前锁定的 NetworkX 3.6.1 已满足需要，不需要升级。

## 分阶段落地

### A. 组合设计识别

当前加入 `pair-design-network`，覆盖论文 `bst(7,3,1)`：

- 7 个 host、7 个 lower switch、7 个 upper switch；
- 7 条 host-lower 链路、21 条 incidence 链路；
- 原图与打乱 ID/边顺序的图均验证通过；
- 删除一条 incidence 链路后必须拒绝；
- 逐路径枚举和目的 DAG 的全部路由查询结果一致；
- 112 个查询项全部由规则负责，生成 60 条精确规则、16 条范围规则、0 个表项。

### B. 通用分区和连接关系

新增 `graph_decomposition.py`，实现初始颜色、WL 细化、equitable partition、组间关系和
unmatched links。先以 shadow 模式运行，与当前专用识别器比较，不影响仿真输入。

### C. 商图递归和候选选择

实现 quotient graph 递归、候选冲突图和 MDL 选择。在 UBX、1DPOD、BST、端口外扩和
破坏性反例上证明物理所有权及路由结果不变后，通用发现成为主路径。

### D. 运行时结构规则评估

只有当通用分解稳定生成少量 `NodeGroup + GroupConnection`，且当前精确/范围规则仍占用明显
内存时，再评估 C++ 组级规则：

```text
currentGroup + destinationGroup + indexRelation -> localPortGroup
```

否则保持现有 C++ 格式，避免把复杂图关系解释带入逐包热路径。

## 论文拓扑证据边界

BST 定义来自 *Balanced Sparse Tree: A Scalable Network Topology for Large Language Models*：

- PDF p.3，Sec. 3.1 和 Fig. 3：稀疏二部正则图、group 构造和 NPU-ToR 连接；
- PDF p.4，Sec. 3.2-3.3 和 Fig. 4：Steiner system、pair 唯一覆盖及 Fano blocks
  `124, 135, 167, 237, 256, 346, 457`；
- PDF p.5，Sec. 3.4 和 Fig. 6：Spine/ToR fabric scaling；
- PDF p.15-16，Appendix H：`S(2,4,13)` 的 64-port 扩展示例。

基础测试只采用没有歧义的单 group `bst(7,3,1)`。论文对多 group 可扩展拓扑给出了逻辑关系
和规模公式，但没有给出唯一、通用的完整端口级 Clos wiring，因此不从图示猜测跨 group
物理连接。
