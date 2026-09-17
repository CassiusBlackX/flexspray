**中文** | [English](README_en.md) | [ns-3-ub-tools](./ns-3-ub-tools/README.md)

# scratch 仿真使用说明

推荐先用 `ns-3-ub-tools` 准备拓扑、路由和流量，再由 scratch runner 读取已经准备好的
目录启动仿真。tools 不直接启动 ns-3，runner 也不会在运行时导入 Python 或 NetworkX。

交换机 `forwardDelay` 表示固定、可并行的入端口处理时延。缓冲占用（Buffer Occupancy）
统计 processing + VOQ + egress；DCQCN/CAQM 使用的排队积压（Queue Backlog）统计
VOQ + egress，不含 processing。Ingress admission 和 PFC 仍统计处理中的报文。
现有 QueueTrace 输出是缓冲占用口径，具体接口见
[交换机缓冲架构](../src/unified-bus/doc/switch-buffer-architecture.md#23-buffer-occupancy-与-queue-backlog)。

仿真产生的 `runlog/` CSV 文件类别、字段和单位见
[Unified Bus trace runlog 格式说明](../doc/trace-runlog-format.md)。

## 构建一次

在 parent 仓库根目录执行：

```bash
BUILD_JOBS=${BUILD_JOBS:-$(python3.12 -c 'import os; print(os.cpu_count() or 1)')}
python3.12 ./ns3 configure \
  --enable-modules=unified-bus \
  --disable-examples --disable-tests --disable-mpi --disable-mtp \
  --disable-werror -d release -G Ninja
python3.12 ./ns3 build -j "$BUILD_JOBS" ub-quick-example
```

后续 case 使用 `--no-build` 复用该二进制。

## 推荐使用路径

### 1. 准备拓扑与路由

```bash
python3 scratch/ns-3-ub-tools/ub_tools.py topology check \
  scratch/structured_topo_example.py

python3 scratch/ns-3-ub-tools/ub_tools.py topology build \
  scratch/structured_topo_example.py \
  --output-dir path/to/case \
  --route-generation mixed
```

拓扑输入也可以是 `path.py:function`、JSON/YAML 文件或已有 CSV 拓扑目录。默认使用结构化
case 格式，不生成大型拓扑和路由 CSV 中间文件。

### 2. 准备流量

```bash
python3 scratch/ns-3-ub-tools/ub_tools.py traffic allreduce \
  --host-count 4 \
  --data-size 1MB \
  --algorithm ring \
  --output-dir path/to/case
```

case 还需要一份完整的 `network_attribute.txt`。`transport_channel.csv` 可选；默认推荐运行时
按需预留和物化 TP，避免为大规模 host pair 预先创建全部对象。

### 3. 检查

```bash
python3 scratch/ns-3-ub-tools/ub_tools.py case show path/to/case
python3 scratch/ns-3-ub-tools/ub_tools.py case check path/to/case --json
```

只有 `case check` 成功后再启动仿真。

### 4. 运行结构化 case

```bash
python3 scratch/run_structured_topo_case.py \
  --case-path path/to/case \
  --route-runtime-mode compressed-table-lookup
```

成功运行应出现：

```text
Simulation finished.
Processed ... tasks.
```

### 5. 解析 trace

```bash
python3 scratch/ns-3-ub-tools/ub_tools.py trace parse \
  path/to/case \
  --output-dir path/to/analysis
```

主要输出：

- `task_statistics.csv`：任务完成时间和包时间戳摘要；
- `throughput.csv`：端口方向吞吐统计。

## 路由选项

离线生成方式：

| 参数 | 行为 |
| --- | --- |
| `--route-generation mixed` | 默认。已验证结构用规则，其余部分自动回退 |
| `--route-generation structured` | 要求全部路由由结构规则负责 |
| `--route-generation full` | 全部候选从物理图生成，用于兼容和对照 |

运行时方式：

| 参数 | 行为 |
| --- | --- |
| `auto` | 默认值；有 `compressed_routing.bin` 时使用压缩索引，否则使用可用结构规则 |
| `compressed-table-lookup` | 查询预先编译的 `compressed_routing.bin`，适用于三种生成方式 |
| `structured-rule-compute` | 运行时根据结构规则计算，要求存在可用结构规则 |

这些选项只改变候选端口的产生方式，不改变后续 HASH、round-robin、adaptive、packet spray
和一次绕路行为。

## Case 目录

结构化 case 的主要文件：

```text
network_attribute.txt       必需，完整运行参数快照
traffic.csv                 必需，通信任务
structured_topology.txt     topology build 生成
compressed_routing.bin      topology build 生成
structured_summary.json     topology build 生成
transport_channel.csv       可选
```

runner 不读取同目录中的 `node.csv`、`topology.csv` 或 `routing_table.csv`。

## 已有用例迁移

已有用例可以按需迁移，不要求一次改完所有输入：

- 已有 Python 建图脚本可增加无参数 `build_topology()`，返回 `NetworkSimulationGraph`，然后作为
  `ub_tools.py topology build` 的输入；写 CSV 的操作可以继续保留在脚本的 `main()` 中；
- 已有 CSV case 可以继续运行，也可以把目录传给 `topology build`，生成结构化拓扑和压缩路由；
- 已有 `network_attribute.txt` 和 `traffic.csv` 可以直接复用；
- 已有 runlog 可以直接使用 `ub_tools.py trace parse` 解析。

迁移 Python 脚本后，结构化路径和脚本直接执行路径可以共用同一个 builder，避免维护两份拓扑。

## CSV case 兼容路径

已有 CSV case 继续使用：

```bash
python3.12 ./ns3 run --no-build \
  'scratch/ub-quick-example --case-path=path/to/case'
```

新 CLI 也能生成 CSV 拓扑和完整路由工件：

```bash
python3 scratch/ns-3-ub-tools/ub_tools.py topology build topology.py \
  --output-dir path/to/case \
  --case-format csv
```

该命令固定使用 full 路由生成。运行前仍需添加 `network_attribute.txt` 和 `traffic.csv`。
`--export-csv` 只是结构化 case 的诊断快照，不是 runner 输入。

CSV case 的主要文件：

```text
network_attribute.txt
node.csv
topology.csv
routing_table.csv
traffic.csv
transport_channel.csv       可选
```

runner 会为每个 Host 创建绑定全部可编码 UB 端口的默认 Entity 1，并在流量注册前统一冻结。
Entity 不在 case CSV 中选择；多 Entity 只通过 controller/App 的程序化 API 保留能力。详细
CSV 字段、trace 字段和旧 case 行为保留在
[English reference](README_en.md) 。

## PFC 模式选择

`PFC_DYNAMIC_PAPER` 用于复现 DCQCN 论文
**“Congestion Control for Large-Scale RDMA Deployments”**（SIGCOMM 2015）中的动态 PFC 阈值，它与项目默认的 `PFC_DYNAMIC` 共享池模式
不同。只有明确复现或对照该论文时才选择 `PFC_DYNAMIC_PAPER`；普通动态 PFC 实验使用
`PFC_DYNAMIC`。完整参数和字段定义见 [English reference](README_en.md) 。

## 相关文档

- [ns-3-ub-tools 中文说明](./ns-3-ub-tools/README.md)
- [集合通信流量生成](./ns-3-ub-tools/traffic_maker/README.md)
- [English scratch reference](README_en.md)

## Atomic 参考用例

URMA/Jetty 的九种 ROI atomic 支持 RTP/CTP；参数、四种 CSV 表头与模型限制见
[Atomic 使用说明](../src/unified-bus/doc/atomic-support.md)。`dataSize(Byte)` 表示操作数宽度 N，
请求 Payload P 可以独立覆盖。两个用例各含 25 个任务，覆盖混合 Read/Write 和 phase 依赖：

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_atomic_rtp'
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_atomic_ctp'
```

## Transaction 扩展参考用例

`scratch/2nodes_transaction_extensions/` 提供六个可独立加载的两节点 case，覆盖 25 种 URMA
CSV 请求名称、本轮七种新操作和九种旧 atomic。`*-queue` 覆盖 0x1C/0x1D 与 N=1..64；
`*-writes` 覆盖 0x19/0x1A、三种 BE 写与五档长度；`*-existing` 覆盖七种旧请求与九种 atomic。
每例都有独立输入、`experiment-spec.md` 和运行时参数全量快照，串行执行：

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/rtp-queue'
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/ctp-queue'
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/rtp-writes'
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/ctp-writes'
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/rtp-existing'
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/ctp-existing'
```

各例的 CSV 操作集合和模型边界见
[transaction-extensions README](./2nodes_transaction_extensions/README.md) 与
[Atomic 使用说明](../src/unified-bus/doc/atomic-support.md)。
