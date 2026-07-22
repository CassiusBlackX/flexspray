# 权威图结构工具选型

## 结论

截至 2026-07-20，没有一个成熟工具能够接收任意带端口、并行链路和链路属性的网络图，
自动输出无损的 Clos、BST、多平面或 Cartesian 结构以及可直接运行的路由规则。

可靠的工程方案是组合使用不同工具，但保持一个独立的物理展开和路由等价验证器：

1. NetworkX 3.6.1 继续作为默认图表示、候选构造和精确匹配基线。
2. SageMath 作为可选离线 reference backend，提供 equitable refinement、modular
   decomposition、Cartesian product recognition 和 design validation。
3. python-igraph/Bliss 只在需要稳定 canonical coordinates 或 automorphism generators 时评估。
4. OR-Tools CP-SAT 只负责从已经验证的重叠候选中选择全局最优组合。
5. 任何第三方结果都不能替代物理端口图展开验证和逐查询路由等价验证。

当前不需要修改 `requirements.txt`。先完成 NetworkX 主路径的通用分组和关系抽取，再根据
基准结果决定是否引入可选后端。

## 当前基线

项目固定使用 Python 3.12 和 NetworkX 3.6.1。当前已经使用：

- `connected_components()` 拆分候选区域；
- `could_be_isomorphic()` 做必要条件预筛；
- `vf2pp_isomorphism()` 和 `vf2pp_is_isomorphic()` 做精确节点映射；
- `cartesian_product()` 构造已知模板；
- `single_source_shortest_path_length()` 构造无权最短路 DAG。

当前识别路径不存在用近似算法代替精确同构的问题。主要缺口是：

- 从任意图自动形成稳定的候选节点分组；
- modular decomposition；
- 从图反向识别 Cartesian product factors；
- canonical labeling 和 automorphism generators；
- 从多个重叠候选中选择总表示成本最低的组合。

## 推荐矩阵

| 工具 | 可靠能力 | 不能解决的问题 | 工程决定 |
| --- | --- | --- | --- |
| NetworkX 3.6.1 | VF2/VF2++、WL hash、模板构造、quotient graph、最短路 | 没有 canonical labeling、modular decomposition 和 Cartesian 反向因式分解 | 保留为默认依赖 |
| SageMath 10.9 | Equitable refinement、线性 modular decomposition、Cartesian product recognition、incidence design validation | 不能从任意图自动命名网络拓扑；依赖重 | 可选离线 reference backend |
| python-igraph 1.0 / Bliss | Canonical permutation、automorphism group、精确同构 | Bliss 对平行边不安全；只直接处理 vertex color | 有 canonical coordinate 需求时评估 |
| nauty/Traces 2.9 | Canonical labeling、automorphism group，算法权威 | Python 包版本和 macOS wheel 滞后；属性和并行边需要编码 | 不作为默认依赖 |
| rustworkx 0.18 | Rust 实现的精确 VF2/VF2++，支持节点和边 matcher | 没有 canonical labeling、WL 或结构分解 | NetworkX 同构成为瓶颈后再 benchmark |
| OR-Tools CP-SAT 9.15 | Set packing、exact cover、整数化 MDL 全局选择 | 不发现候选，也不证明候选正确 | 候选冲突规模需要精确优化时接入 |
| graph-tool 3.1 | 高性能图容器和精确同构 | 不能补 canonical/automorphism 缺口；安装栈重 | 不采用 |
| METIS / KaHIP / NetworKit | 快速分区、社区发现 | 输出是优化或启发式分区，不是无损结构证明 | 只允许做候选提示，不进正确性路径 |
| GraphBLAS | 稀疏邻接统计和筛选加速 | 不理解结构；普通矩阵会归约并行边 | 后期性能后端 |
| cuGraph | Linux/CUDA 上加速部分图算法 | macOS 不可用，也不提供结构语义 | 不采用 |
| gSpan 和频繁子图挖掘包 | 图数据库中的 frequent pattern mining | 单图重复、端口化 MultiGraph 和无损分解契约不匹配 | 不采用 |

## 各阶段可复用的轮子

### 1. 候选分组

NetworkX 的 WL hash 可以作为快速候选指纹，但 hash 不是无碰撞证明，且 NetworkX 明确不对
MultiGraph 提供 WL hash。主路径应在简化图上使用明确的颜色编号：

```text
newColor(node) = intern(
  oldColor(node),
  sorted multiset(edgeType, oldColor(neighbor))
)
```

这个实现很短，并且不会因为摘要碰撞合并颜色类。SageMath 的
`coarsest_equitable_refinement()` 可作为差分测试 oracle。NetworkX 的 `equitable_color()` 是
另一类问题：它要求颜色类大小均衡，不是这里需要的 equitable refinement。

决定：第一版不为 1-WL 引入 SageMath runtime；本地实现确定性 refinement，并用 SageMath
离线对照代表拓扑和破坏性反例。

### 2. Modular decomposition

SageMath 提供 `Graph.modular_decomposition()`。默认实现基于
Corneil-Habib-Paul-Tedder 系列算法，官方文档给出的复杂度为 `O(V + E)`，输出
`PRIME / SERIES / PARALLEL` 分解树。Sage 文档还说明旧论文和实现曾存在错误，因此不建议
采用缺少维护和验证的小型 Python 包。

决定：不自行实现完整 modular decomposition。第一版只实现通过 open/closed neighborhood
signature 分桶的 true/false twins。确实需要一般 modules 时，使用 Sage reference backend，
再决定是否引入独立实现。

### 3. Cartesian product recognition

NetworkX 和 igraph 都能正向构造 Cartesian product，但没有从任意图恢复 prime factors 的
官方 API。SageMath 提供：

```python
is_cartesian_product(graph, certificate=True, relabeling=True)
```

它可以返回 factors 和原节点的 product coordinates，但只支持 simple、undirected、connected
graph。官方实现仍标有性能改进事项，因此适合作为离线 oracle 和中小候选验证器，不应未经
基准直接用于 1024-NPU 全图主路径。

决定：保留当前“生成可能模板 + VF2++ 取得映射”路径；加入 Sage 差分测试。只有模板参数未知
且候选规模证明可控时，才调用 Sage product recognition。

### 4. BST、Steiner 和 BIBD

SageMath 的 `IncidenceStructure.is_t_design()` 可以在 points 和 blocks 已经恢复后验证并推断
`t-(v,k,lambda)` 参数，也提供部分 Steiner/BIBD constructors。

它不会从任意未标注图自动判断哪侧是 points、哪侧是 blocks。项目仍需要：

1. 在物理图上恢复二部候选；
2. 确定 lower 和 upper 角色；
3. 构造 incidence structure；
4. 验证每个 pair 的 multiplicity；
5. 回到端口图验证所有物理链路。

决定：当前 `O(b*k^2)` pair counting 已经是直接、确定且容易验证的实现，不引入 Sage runtime。
Sage 用于生成更多合法 design 测试和差分验证。

### 5. Canonical labeling 和 symmetry

python-igraph/Bliss 是当前最容易安装的权威 Python 选项：

- `canonical_permutation()` 返回 canonical ordering；
- `automorphism_group()` 返回 generators；
- `count_automorphisms()` 计算 automorphism 数量。

igraph 官方 C 文档明确警告 Bliss 的这些接口不能直接正确处理平行边。使用前必须把权威图
转换为 vertex-colored simple graph，例如将每条带类型的物理链路变成一个 edge-instance
vertex，再连接两个 endpoint vertices。端口号、节点类型和链路属性也必须进入颜色编码。

决定：当前 VF2++ 已能取得一份正确映射，不需要立即增加 igraph。只有以下需求出现时才接入：

- 缓存键必须在节点重编号后保持稳定；
- 多个等价映射需要稳定选择同一组 coordinates；
- automorphism orbit 能显著减少候选枚举。

引入前必须审查 GPL-2.0 分发约束，并对 simple-graph encoding 做双向无损测试。

### 6. 候选冲突选择

Google OR-Tools CP-SAT 可以可靠表达：

```text
每个候选是否采用：boolean variable
共享物理链路的候选：sum <= 1
要求完整覆盖时：sum == 1
目标：structureBytes + residualBytes + routeBytes 最小
```

CP-SAT 要求整数模型，因此序列化字节数和路由成本必须直接使用整数，不能把未经定义的浮点
评分乘一个任意系数。`OPTIMAL` 只能证明“在给定候选集合和整数模型内最优”，不能证明候选
生成器已经找到了所有可能结构。

决定：第一版使用确定性贪心。只有候选冲突分量较大、贪心结果被构造反例证明明显更差时，
才把 OR-Tools 作为可选依赖接入；小冲突分量也可以先用本地 branch-and-bound。

## 不进入正确性路径的工具

### 社区发现和图分区

METIS、KaHIP、Louvain 和 NetworKit 优化 edge cut、balance、modularity 等目标。它们可以提出
搜索区域，但分区不等于拓扑模块，结果也不能证明端口和链路的无损覆盖。

### 频繁子图挖掘

标准 gSpan 面向 graph database，其 support 通常表示一个 pattern 出现在多少张图中。当前问题
只有一张权威拓扑图，需要的是同一图内重复 occurrence、互斥物理链路所有权和无损 residual。
现有 Python gSpan 包维护不足，输入契约也不覆盖端口化 MultiGraph。

### 加速后端

GraphBLAS、cuGraph 和 rustworkx 可以减少部分算法耗时，但不会增加结构语义。只有 profiling
确认某个精确阶段成为瓶颈后，才针对该阶段做转换和 benchmark。

## 推荐落地顺序

### Phase 1：不增加依赖

1. 使用 NetworkX simple candidate graph 和权威 physical graph 双视图。
2. 实现无 hash 碰撞的确定性 1-WL refinement。
3. 实现 twins、allToAll、oneToOne、fixedDegree 和 explicit 关系。
4. 复用当前 VF2++、物理展开验证、最短路 DAG 等价和 mixed fallback。
5. 现有专用识别器继续作为回归基线和关系证明插件。

### Phase 2：SageMath reference tests

在独立环境中对以下结果做差分验证，不加入默认 `requirements.txt`：

- equitable refinement；
- modular decomposition；
- Cartesian product factors 和 coordinates；
- Steiner/BIBD incidence validation。

### Phase 3：按证据选择后端

- ID 无关 canonical coordinates 成为真实需求：评估 python-igraph/Bliss；
- VF2++ 成为实测热点：benchmark rustworkx；
- 候选冲突选择明显影响压缩率：接入 OR-Tools CP-SAT；
- 稀疏统计成为实测热点：评估 GraphBLAS。

## 权威来源

- NetworkX VF2++：<https://networkx.org/documentation/stable/reference/algorithms/isomorphism.html>
- NetworkX WL hash：<https://networkx.org/documentation/stable/reference/algorithms/graph_hashing.html>
- NetworkX quotient graph：<https://networkx.org/documentation/stable/reference/algorithms/generated/networkx.algorithms.minors.quotient_graph.html>
- Sage equitable refinement：<https://doc.sagemath.org/html/en/reference/graphs/sage/graphs/generic_graph.html#sage.graphs.generic_graph.GenericGraph.coarsest_equitable_refinement>
- Sage modular decomposition：<https://doc.sagemath.org/html/en/reference/graphs/sage/graphs/graph_decompositions/modular_decomposition.html>
- Sage Cartesian recognition：<https://doc.sagemath.org/html/en/reference/graphs/sage/graphs/graph_decompositions/graph_products.html#sage.graphs.graph_decompositions.graph_products.is_cartesian_product>
- Sage incidence structures：<https://doc.sagemath.org/html/en/reference/combinat/sage/combinat/designs/incidence_structures.html#sage.combinat.designs.incidence_structures.IncidenceStructure.is_t_design>
- python-igraph GraphBase API：<https://python.igraph.org/en/stable/api/igraph.GraphBase.html>
- igraph isomorphism and Bliss limits：<https://igraph.org/c/html/develop/igraph-Isomorphism.html>
- nauty/Traces：<https://pallini.di.uniroma1.it/>
- rustworkx isomorphism API：<https://github.com/Qiskit/rustworkx/blob/main/docs/source/api/algorithm_functions/isomorphism.rst>
- OR-Tools CP-SAT：<https://developers.google.com/optimization/cp/cp_solver>
- graph-tool isomorphism：<https://graph-tool.skewed.de/static/docs/stable/autosummary/graph_tool.topology.isomorphism.html>
- METIS：<https://github.com/KarypisLab/METIS>
- KaHIP：<https://github.com/KaHIP/KaHIP>
- NetworKit：<https://networkit.github.io/>
- GraphBLAS：<https://graphblas.org/>
- gSpan 原始论文：<https://doi.org/10.1109/ICDM.2002.1184038>
