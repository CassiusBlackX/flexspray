# ns-3-ub-tools v2 统一入口设计

状态: 已确认，待实施

## 目标

为 `ns-3-ub-tools` 提供一个统一、可发现的命令入口：

```text
ub_tools.py
├── topology
├── traffic
├── trace
└── case
```

用户通过逐级 `--help` 发现命令、参数、默认值、有效组合和修复建议。新功能只进入统一入口，
不继续扩展旧脚本的用户接口。

本设计只负责准备和检查仿真输入。tools 不启动 ns-3；准备完成的 case 仍由 scratch runner
读取并运行。仿真开始后不存在 Python 与 C++ 的跨语言运行时交互。

## 兼容边界

### 保留 v1 脚本

结构化拓扑出现前已经存在的脚本继续作为独立入口保留：

- `user_topo_example.py`；
- `user_topo_2layer_clos.py`；
- `user_topo_4x4_2DFM.py`；
- `traffic_maker/build_traffic.py`；
- `traffic_maker/all2allv_maker.py`；
- `traffic_maker/simai_flow_to_traffic.py`；
- `trace_analysis/parse_trace.py`；
- `topo_plot.py`。

这些脚本的现有参数、默认目录和 CSV 输出行为不因 v2 改造而改变。旧脚本可以与 v2 复用
内部实现，但继续独立解析自己的命令行。

### 不提供 v1 CLI 兼容层

`ub_tools.py` 不接受旧参数别名，不把旧命令转发给旧脚本，也不模拟旧脚本的时间戳输出目录。
文档需要旧行为的用户直接运行旧脚本。

结构化开发过程中的 `prepare_structured_case.py` 不属于 v1 稳定接口。v2 覆盖其能力后，用户
文档不再把它作为主要入口；代码可以保留为内部适配层。

## 命令树

```text
ub_tools.py
├── topology
│   ├── check
│   ├── build
│   └── plot
├── traffic
│   ├── allreduce
│   ├── reduce-scatter
│   ├── allgather
│   ├── alltoall
│   ├── alltoallv
│   └── convert-simai
├── trace
│   └── parse
└── case
    ├── show
    └── check
```

`topology`、`traffic`、`trace` 和 `case` 是一级子命令，不是选项。没有选中下级命令时，CLI
显示当前层帮助并以非零状态退出。

## 通用 CLI 行为

- `python3 ub_tools.py --version` 输出 v2 接口版本和 tools 版本。
- 每一级 `--help` 显示该层可用命令、参数默认值和至少一个可执行示例。
- 不支持的 collective/algorithm 组合必须列出当前 collective 的有效算法。
- 路径参数在帮助中明确标记为输入或输出。
- 人类可读错误写入 stderr；后续 `--json` 以稳定字段输出检查结果，不改变生成命令的工件。
- 生成命令不得默默覆盖非本工具生成的文件。

## Topology 命令

### build

```bash
python3 ub_tools.py topology build SOURCE \
  --output-dir CASE_DIR \
  --case-format structured
```

`SOURCE` 是输入，`--output-dir` 是输出。支持：

- `topology.py`，默认调用 `build_topology()`；
- `topology.py:custom_builder`；
- `topology.yaml`；
- `topology.json`；
- 已有 CSV case 目录。

Python builder 的公开契约为：

```python
from net_sim_builder import NetworkSimulationGraph


def build_topology() -> NetworkSimulationGraph:
    ...
```

返回值必须是 `NetworkSimulationGraph`。错误类型、缺失函数和错误返回值都应在进入结构发现前
报告。旧拓扑脚本可以提取上述函数，同时保留原 `main()` 的 v1 CSV 行为。

`--case-format` 第一版支持：

- `structured`：写入结构化拓扑与路由工件，默认值；
- `csv`：显式导出可检查的传统 CSV 快照。

CSV 是诊断或旧 case 输入格式，不是结构化编译的默认内部流转格式。

### check 和 plot

`topology check SOURCE` 加载并验证输入，但不准备完整 case。`topology plot SOURCE` 复用同一个
加载器和规范化物理图，不能再实现第二套输入解析。

## Traffic 命令

### Collective 与 algorithm registry

collective 是 `traffic` 的下一层命令，algorithm 统一由 `--algorithm` 选择：

| Collective | Algorithms |
| --- | --- |
| `allreduce` | `ring`, `nhr`, `recursive-halving-doubling` |
| `reduce-scatter` | `ring`, `nhr`, `recursive-halving` |
| `allgather` | `ring`, `nhr`, `recursive-doubling` |
| `alltoall` | `pairwise`, `scatter` |
| `alltoallv` | `pairwise` |

代码中只能有一份 registry。命令树、帮助、校验和 builder 分派都从该 registry 读取。

### 公共参数

```text
--host-count N
--group-size N
--data-size SIZE
--algorithm NAME
--group-layout contiguous|interleaved
--placement-file FILE
--phase-delay TIME
--output-dir DIR
```

- `host-count` 是可参与通信的物理节点总数。
- `group-size` 是每个 communication group 的 rank 数，默认等于 `host-count`。
- `group-layout` 控制自动分组，默认 `contiguous`。
- `placement-file` 显式提供 communication group、rank 到物理 nodeId 的对应关系。
- `group-layout` 和 `placement-file` 互斥。
- `phase-delay` 是上一依赖阶段完成后到下一阶段任务可启动之间的延迟，默认 `0ns`。
- `output-dir` 是直接写入 `traffic.csv` 的目录；v2 不创建隐藏的时间戳子目录。

`placement-file` 格式为：

```csv
groupId,rankId,nodeId
0,0,17
0,1,9
```

使用 `placement-file` 时从文件推导通信组数量及成员；命令行给出的数量如果同时存在，必须与
文件一致，不能静默覆盖。

### data-size

`--data-size` 表示一次 collective 在算法切分前处理的完整逻辑数据大小。它不是一个 rank
在某个 phase 的发送量，也不是所有链路累计传输量。

| Collective | `data-size` 定义 |
| --- | --- |
| AllReduce | 完整规约张量大小；每个 rank 最终得到同样大小的结果 |
| ReduceScatter | 完整待规约张量大小；输出按 rank 切分 |
| AllGather | 完整聚合结果大小；每个 rank 的初始 shard 是其中一部分 |
| AllToAll | 每个 rank 参与重新分发的完整输入缓冲区大小 |
| AllToAllV | `uniform`/`random` 时每个 rank 的完整待分发数据量；`matrix` 时省略 |

切分不能丢失字节。不能整除时使用稳定、可测试的余数分配规则，所有数据块之和必须等于
`data-size`。

### AllToAll scatter

`--peers-per-phase K` 只对 `alltoall --algorithm scatter` 有效，表示每个 rank 在一个 phase
中同时向多少个其他 rank 发送。它改变 phase 数和并发度，不改变总逻辑数据量。有效范围为
`1 <= K < group-size`。

### AllToAllV

```text
--distribution matrix|uniform|random
--traffic-matrix FILE
--seed N
--include-local
```

- `matrix` 使用按字节计数的 `N x N` 矩阵；必须提供 `traffic-matrix`，不使用 `data-size`。
- `uniform` 按目标 rank 稳定均分 `data-size`。
- `random` 使用固定、文档化的随机分配规则；相同 `seed` 必须生成相同矩阵，每行字节和必须
  等于 `data-size`。
- `include-local` 决定是否保留对角线上的本地复制，默认关闭。

第一版不把 MoE 的 batch、token、top-k 和 expert 参数混入通用 `random`。MoE 分布以后作为
独立 distribution 加入。

### 输出语义

builder 先生成与 CSV 无关的 `Schedule`，writer 再写 `traffic.csv`。Schedule 必须保留 phase、
每 rank 的发送和接收对象、字节数以及 AllToAllV 所需偏移。writer 负责 taskId、phaseId、
依赖、opType、priority 和 delay 的 case 映射。

第一版继续默认生成 `URMA_WRITE`、priority `7`。这两个值可显示在高级帮助中，但只有确认
其他操作的 schedule 和仿真语义后才开放为公共参数。

## Trace 和 Case 命令

`trace parse` 复用现有 trace parser，不重新定义 trace 语义。`case show` 汇总 case 输入、
格式版本、节点/链路/任务数量和路由工件；`case check` 检查必需文件、格式版本、引用关系和
可加载性，不启动仿真。

## 非目标

- tools 不启动 ns-3，也不合并 prepare 与 run。
- v2 不兼容或转发 v1 CLI。
- 第一版不提供任意 collective DSL。
- 第一版不根据拓扑自动选择 collective algorithm。
- 第一版不把 Schedule 的 block、slice、step 或 displacement 暴露成通用用户参数。
- 第一版不声称支持尚未形成独立 builder 的 ReduceScatter 或 AllGather。

## 验收

- 顶层及每层帮助包含默认值、有效组合、示例和修复建议。
- v2 不接受旧 CLI 参数，旧脚本仍能独立运行原有 smoke。
- 现有 AllReduce 和 AllToAll 的 v2 输出与既有算法语义逐 phase、逐 transfer 对齐。
- ReduceScatter 与 AllGather 有独立 Schedule builder 和字节守恒测试。
- AllToAllV 三种 distribution 有确定性、矩阵校验和字节守恒测试。
- Python、JSON/YAML 和 CSV topology source 走同一规范化边界。
- `topology check/plot` 不复制 source loader。
- `case check` 和 `trace parse` 不启动仿真。
- CLI 变更同一提交更新 `scratch/ns-3-ub-tools/README.md`、traffic README 以及仓库对应的
  OpenUSim reference 文档。
