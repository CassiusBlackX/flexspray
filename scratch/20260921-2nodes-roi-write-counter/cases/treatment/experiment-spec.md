# Experiment Spec — treatment

状态: success；结构有效，预测 matched

## Planning Mode

`experiment-group`，block `roi-counter`，role `treatment`。
共同约束和字段契约：[experiment-plan.md](../../experiment-plan.md)。

## Goal

测量 Write with atomic store add 的 B 数据就绪延迟，与 control 比较是否节约约 1 RTT。

## Topology

A/B 两个端点，一条直连全双工链路，每方向 400 Gbps，传播延迟每方向 1 μs，与 control 相同。

## Topology Realization

`custom-graph`，复用 control 的同一独立 scratch 驱动和生产构造入口。
无 CSV 输入、背景流、故障或其他端点。

## Workload

- 唯一一笔 64 B WRITE_WITH_ATOMIC_STORE_ADD（`0x19`），ORDER_RELAX，ROI，同一 Jetty。
- 数据目标地址 `0x1000`，NTFETAH.address=`0x2000`，NTFETAH.data=64。
- B 使用实际解码请求：先记录 `[0x1000, 0x1040)` 提交，再 counter += notify.data。
- counter 初始 0；第一次等于 64 立即记录 ready，并核验数据已提交。
- 成功后调用标准 completion；不另外构造 post 请求或自制 ACK。

## Routing Intent

与 control 相同的固定直接路由和唯一 TP 绑定；无 TPG、多路径或路由选择变量。

## Network Overrides

- `UbPort::UbDataRate = 400Gbps`（双方）；`UbLink::Delay = 1us`。
- A `UbController::InitiatorProtocolDispatchDelay = 0ns`。
- A `UbController::InitiatorProtocolCompletionVisibilityDelay = 0ns`。
- B `UbTransaction::SetTargetExecutionDelay(0)`；检查返回值。
- 时间分辨率 ps；其他实际值与 control 一致，不能改变默认参数获得预测结果。

## Transport Channel Mode

与 control 相同，预先用 public API 建立一对 RTP 通道；不计建连时间。

## Observability

同一 B executor 和事件格式；计数器更新来自收到的字段，网络事件来自现有真实 trace。
独立进程和输出目录，console、summary、参数快照齐备。

## Metrics And Validation

L_B：数据 WQE 提交 → B 第一次 counter=64；L_A：同一起点 → A 唯一操作完成。
有效性：一个数据包、B 执行 1 次、ready 1 次、A 完成 1 次、counter=64、范围/地址正确。
data_commit 的事件序号必须先于 counter_update 和 counter_ready，即使时间戳相同。
早就绪、超额、重复/缺事件、溢出均使运行无效；不使用常量增量替代接收字段。

传播模型预测 B 约 1 μs、A 约 2 μs；相对 control 的 B 延迟缩短约 2 μs。
实际测量与预测分开保存，0 或负的收益也应进入报告。

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
