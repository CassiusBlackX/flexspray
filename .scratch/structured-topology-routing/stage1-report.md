# 第一阶段验收报告

> 接口后续更新：本报告中的一次性 runner/stdin 路径已由任务 17 替代。当前路径先用
> `prepare_structured_case.py` 写入 case 目录，再用
> `scratch/run_structured_topo_case.py --case-path <case>` 读取 `structured_topology.txt`。
> 下文的测量数字和当时接口描述保留为阶段历史。

## 结论

第一阶段完成了从 Python + NetworkX 或 JSON/YAML 物理拓扑到结构发现、独立验证、路由
等价拆分和 C++ 混合运行时的完整正确性链路。原 CSV case 路径保持可用，两种路径可以
同时存在。

第一阶段没有完成最终性能目标。当前实现共享本地端口组，但规则仍按“当前节点 + 目的节点
+ 目的端口”逐项生成；冷缓存时还会先生成完整基准路由。因此它证明了规则替换的正确性
边界，但还不是真正的结构级路由压缩。

## 用户路径

主要路径是用户在 Python 中用 NetworkX 构造 `NetworkSimulationGraph`，builder 直接返回
图对象，再通过 `scratch/run_structured_topo_case.py` 运行。工具自动完成端口配置、结构
发现、验证、路由编译、缓存和仿真输入传递。用户不需要声明 leaf、spine、pod 等标签，也
不需要生成中间 CSV。

JSON/YAML 是同一 NetworkX 路径的薄适配器。诊断 CSV 仅在显式请求时导出，不参与默认
正确性路径。原有 CSV case 仍通过原入口运行。

## 正确性证据

- Python 工具测试：46 项通过。
- C++ `unified-bus-structured-routing`：通过，覆盖 stdin/CSV 候选端口等价、非法协议头
  和非法端口。
- UBX 16-NPU：20 个节点、88 条链路，880/880 路由查询项等价。
- 1DPOD 64-NPU：176 个节点、1,248 条链路，36,288/36,288 路由查询项等价。
- 1DPOD 1024-NPU：2,336 个节点、19,968 条物理链路全部验证；路由完成 290/290 个代表性
  查询项验证。该结果是受控抽样，不是全量路由等价。
- NetworkX mixed 冷缓存、热缓存、table、YAML 和原 CSV smoke 均通过。

## 第一阶段性能

1DPOD 64-NPU 的测量结果：

| 模式 | 冷编译 | Python 当前分配 | Python 峰值 | stdin 大小 |
| --- | ---: | ---: | ---: | ---: |
| table | 约 1.63 s | 12.37 MB | 24.36 MB | 611 KB |
| mixed | 约 4.66 s | 6.65 MB | 39.65 MB | 559 KB |

mixed 的常驻 Python 编译对象约下降 46%，缓存命中约 0.41 s；但冷编译更慢，峰值分配也
更高。UBX 小图中 mixed 的对象数和协议体量还可能超过 table。不能据此声称初始化内存或
查找时间已经优化。

## 进入第二阶段

现在正式进入第二阶段性能优化：

1. 默认 `nx.all_shortest_paths` 路径从已验证结构直接生成共享规则和必要表项，不再建立
   完整基准表。
2. 规则表示升级为结构级目的集合或范围，并保留精确例外；自定义 path finder 继续使用
   第一阶段基准/缓存路径。
3. C++ 运行时改为预索引只读结构，避免逐包多层 `std::map` 查找和临时候选数组拼装。
4. 在 1024-NPU 拓扑上量化冷启动、峰值 RSS、常驻内存和路由查找延迟。
