# Experiment Spec — control

状态: success；结构有效，预测 matched

## Planning Mode

`experiment-group`，block `roi-counter`，role `control`。
共同约束和字段契约：[experiment-plan.md](../../experiment-plan.md)。

## Goal

测量 ROI 下 RO WRITE(data) + SO WRITE(post) 的 B 数据就绪延迟，作为组合操作的基线。

## Topology

A/B 两个端点，一条直连全双工链路，每方向 400 Gbps，传播延迟每方向 1 μs。

## Topology Realization

`custom-graph`，独立 scratch 驱动用生产节点、连接和链路 API 固定构造。
不生成 CSV 输入。无背景流、故障、交换机或其他端点。

## Workload

- 同一 Jetty，先提交 64 B WRITE、ORDER_RELAX、ROI，再提交 8 B WRITE、ORDER_STRONG、ROI。
- 两笔均在仿真派发前提交；应用完成回调只观察。TA 负责等待前一笔 RO 的 TAACK。
- 数据目标地址 `0x1000`；post 地址 `0x2000`，8 B payload 是大端 uint64 64。
- B 回调先收到数据，记录 `[0x1000, 0x1040)` 提交；收到 post 时从真实接收 payload 解码并存值。
- counter 初始 0，第一次等于 64 立即记录 ready，检查数据此前已提交。

## Routing Intent

手工固定直接路由，Jetty 绑定唯一 TP。实际合法路由属性沿用共同运行时配置并记录。
无路径选择变量，无 TPG。

## Network Overrides

- `UbPort::UbDataRate = 400Gbps`（双方）；`UbLink::Delay = 1us`。
- A `UbController::InitiatorProtocolDispatchDelay = 0ns`。
- A `UbController::InitiatorProtocolCompletionVisibilityDelay = 0ns`。
- B `UbTransaction::SetTargetExecutionDelay(0)`；检查返回值，不用未注册的 attribute。
- 时间分辨率 ps；其他实际值与 treatment 一致并导出 snapshot。

## Transport Channel Mode

预先通过 public API 建立一对 RTP 通道，等价于本用例的 precomputed；无需 transport_channel.csv。

## Observability

注册同一 `.cc` 内的 B executor，记录 events.csv；结合现有生产 trace 记录真实请求/TAACK。
console 和 resolved-parameters.json 保留。避免打开与本实验无关的全量 debug。

## Metrics And Validation

主指标 L_B：数据 WQE 提交 → B 第一次 counter=64；辅助 L_A：同一起点 → A post 完成。
有效性：64 B 为单数据包，B 执行 2 次，A 完成 2 次，ready 1 次，counter=64，范围和地址正确。
post 发出须在 data TAACK 接收之后；不要求应用完成回调与 post 派发具有额外先后关系。
过早就绪、超额计数、重复/缺失事件和超时均为错误，不延迟 ready 掩盖错误。

传播模型预测 B 约 3 μs、A 约 4 μs，实际加上序列化和其他测得开销；不是硬编码验收值。

## Startup Readiness

配置、构建与运行时参数查询已完成；使用独立驱动，不依赖 CSV 工具子模块。
实际参数见 [resolved-parameters.json](output/resolved-parameters.json)。

## Execution Record

- status：success；return code 0；独立进程串行运行，完整 UTC/HEAD/dirty/哈希见 [run-record.json](output/run-record.json)。
- 实际命令与历次归档见 [run-ledger.md](../../run-ledger.md)；固定命令见 [manifest](../../command-manifest.yaml)。
- output：events.csv、summary.json、resolved-parameters.json、console.log、run-record.json。
- [原始事件](output/events.csv)、[重算指标](output/summary.json)。

## Analysis Notes

结构有效，预测 matched；完整绝对差值、比例、包长及残差归因见 [comparison.md](../../output/comparison.md)。
此判断限于单包、单发送者、无故障模型，不包含目标软件轮询或多包/重传原子性。
