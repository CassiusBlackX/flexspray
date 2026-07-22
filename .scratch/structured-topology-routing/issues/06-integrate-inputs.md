状态: 已完成
阻塞于: 03, 04, 05

后续决定: 一次性编译并启动的接口已由任务 17 替代。任务 06 保留为第一阶段历史验收记录。

# 接入两种输入路径

Python + NetworkX 是主要用户路径，通过
`run_structured_case(graph, case_path, routing_mode="mixed")` 一次完成结构化编译和仿真运行。
JSON/YAML 只负责构造同一种 NetworkX 图，并共用结构发现、验证、缓存和规则运行时。现有
CSV 自定义拓扑路径继续存在。结构化路径默认不落盘内部编译结果，内部也不限定使用 CSV；
用户显式请求诊断导出时，才输出对应 CSV 供查看。

JSON/YAML 使用 `nodeGroups`、`groupConnections`、`layeredConnections` 和
`explicitConnections` 四个可组合部分。`groupConnections` 处理单层批量连接；
`layeredConnections` 用层序号、共享或独立平面、任意层间连接和并行链路表达多层多平面
结构；`explicitConnections` 用于逐端口补充不规则连接或独立表达小型任意图。所有连接先
展开成同一物理拓扑，再执行结构发现和验证；不得把任何生成配置当作规则路由正确性的证明。

公开命令入口为 `scratch/run_structured_topo_case.py`；它可以加载 Python builder 或
JSON/YAML 适配器，并保持轻量。核心编译和运行 API 位于 `scratch/ns-3-ub-tools/`。现有 CSV
case 继续使用 `ub-quick-example --case-path=...`。
拓扑 JSON/YAML 不包含路由表示选择，入口通过
`--routing-mode=table|rule|mixed` 选择本次运行方式，默认值为 `mixed`。
输入顶层必须声明 `version: 1`；缺失或不支持的格式版本在解析阶段报错。
入口通过可选的 `--path-finder path/to/routes.py:function_name` 复用现有
`function(graph, source, destination)` 调用契约；未指定时使用 `nx.all_shortest_paths`。
每条 `explicitConnections` 记录必须用 `group + index + port` 显式声明两个端点。节点
`portCount` 可省略并按最大已用端口号加一推导；显式填写时允许保留未连接端口，但不得小于
已用端口范围。第一阶段不引入独立端口列表。
编译器必须将组内节点确定性地映射到连续内部 `nodeId` 范围 `0..N-1`，并在诊断导出中记录
映射。结构识别不得从组名、ID 范围或声明顺序推断节点角色。
端口分配必须先保留 `explicitConnections` 端口，再处理 `groupConnections` 和
`layeredConnections`。批量连接可分别限制两端端口范围；指定范围不得溢出，未指定范围时
使用当前最低空闲端口。分配结果必须与输入顺序一起保持确定性。
`allocationDelay` 和 `forwardDelay` 必须作为独立节点属性解析；JSON/YAML 不使用旧四列
CSV 的字段兼容规则，也不允许一个字段隐式填充另一个字段。
`defaults.node` 可提供两个节点时延，`defaults.link` 可提供 `bandwidth` 和 `delay`；对象上
的同名字段优先。解析后的每个对象必须具有完整物理属性，不得依赖 C++ 默认值。

验收：现有 case 默认行为不变；两种路径可分别选择表、规则或混合路由；JSON/YAML 默认
运行不依赖任何生成 CSV；诊断导出关闭或开启时，实际安装的路由配置完全相同；入口脚本
不包含结构识别或路由编译的重复实现；自定义 path finder 与现有 NetworkX 路径使用相同
函数签名和基准路由语义；显式端口冲突、负端口号和过小的 `portCount` 必须在启动仿真前
报错；不存在的组、越界的组内 `index` 和重复占用的端点必须给出明确错误。

## 验证结果

- Python builder 可以直接返回 `NetworkSimulationGraph`，入口负责补齐图配置并启动仿真。
- JSON/YAML 通过同一 NetworkX 编译路径运行，默认不依赖生成 CSV。
- `table`、`rule` 和 `mixed` 三种模式、可选诊断导出和自定义 path finder 均有测试覆盖。
- 原 CSV case 的启动路径保持不变。
