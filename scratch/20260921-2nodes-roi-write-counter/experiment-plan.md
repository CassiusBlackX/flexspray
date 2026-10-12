# Experiment Plan — ROI write completion counter

状态: 已完成固定矩阵；两 case 结构有效，预测 matched
日期: 2026-09-21

## Planning Mode

`experiment-group`。工程依据为 tracker 的 `design.md`；本文件、matrix、manifest、ledger
和两份 case spec 是后续物化到 `scratch/20260921-2nodes-roi-write-counter/` 的完整计划蓝本。
物化完成后，该 scratch 目录成为运行与分析的稳定 package root（P）。

## Claim

在单包、无背景流的直连 RTP/ROI 条件下，WRITE_WITH_ATOMIC_STORE_ADD 能在数据提交后立即
更新 B 端计数器，相比 RO WRITE(data) + SO WRITE(post)，减少约一个传播 RTT 的 B 端就绪延迟。

## Simulator Boundary

模拟真实 WQE、编码/解码、RTP 传输、TA 执行和响应，B 小回调记录数据范围和计数器数值。
数据内容不逐字节建模；post 的 8 B 必须真实发送并解码，notify 字段也必须从接收请求取得。
不模拟 B 软件轮询，不证明硬件访存、cache 一致性或并发原子性。
回调按完整 TA segment 执行，所以严格限于一个 64 B 数据包；不推广至多包/重传。

## Control And Treatments

- block ID：`roi-counter`，唯一 changed_variable：完成指示的操作组合。
- `control`：同一 Jetty 预先顺序提交 64 B RO WRITE(data) + 8 B SO WRITE(post)。
- `treatment`：同一 Jetty 提交一笔 64 B RO WRITE_WITH_ATOMIC_STORE_ADD，Notify_Data=64。
- counter 每次运行从 0 开始，B 第一次恰好到 64 时 ready，两个变体都要求数据先提交。
- 数据地址 `0x1000`，计数器地址 `0x2000`，无重叠。control post 是大端 uint64 64。
- 操作组合所需的 post packet、header 和 TAACK 数量变化是该变量的固有代价，照实计量。

固定控制条件：两个端点、一条全双工直连、每方向 400 Gbps、单向传播 1 μs、ROI、单 RTP pair、
无 TPG/多路径、背景流或故障。预先用 public API 建立通道（precomputed 的用例内等价配置），
不计连接建立耗时。采用固定直接路由，路由类型/selector 的实际值在实现时按支持项绑定并记录，
两变体完全相同；单一路径没有路由选择的性能变量。

A dispatch、B target execution、A completion visibility 均为 0。其余运行时参数两者一致并保存
完整实际快照，包括 MTU、帧间隔、TP/端口/链路延迟、流控/拥塞控制、trace 开关、随机种子和
时间分辨率。先以当前运行时参数目录发现默认值，再施加上述已确认覆盖项，不能手写“默认值表”。
独立驱动在进程内建拓扑和 workload，不消费 CSV，故使用 resolved-parameters.json 作为该驱动
的配置证据；不能声称已生成常规 runner 的 network_attribute.txt。

## Prediction And Falsification

定义 `L_B = t_counter_ready - t_submit`；`L_A = t_a_complete - t_submit`。
`t_submit` 为 A 数据 WQE 的提交时间，`t_a_complete` 为 control post 或 treatment 唯一操作的完成时间。

理想传播模型：

| case | B ready 延迟 | A 最终完成延迟 |
| --- | --- | --- |
| control | 3 μs | 4 μs |
| treatment | 1 μs | 2 μs |

预测 `Δ_B = L_B(control) - L_B(treatment) ≈ 2 μs`，对应节省一次数据 TAACK 等待。
序列化、header 长度和其他内部时延按实际事件说明。传播 RTT 是 case-derived；L_B/L_A 是
trace-derived。不预设硬件 RTT，也不宣称精确到 2 μs。

证伪信号：有效结果中 treatment 没有更快；control 未呈现预期 TA 等待；或测得差值无法由
少一次传播往返及观察到的其他时延解释。前两者分别进入预测不符或结构无效判断，不能混淆。
必须报告 `Δ_B - 2 μs` 残差。未经归因的近似数字不足以独自确认机制，证据不足记 inconclusive。
不事后改预测、调输入或用固定 speedup 阈值判功能 PASS。

## Checkpoint Policy

- `checkpoint_policy: continue_full_matrix`
- `manual_approval_gate: false`
- `control-evidence`：检查 control 产物和结构，记录观察；时序不符不暂停另一 case。
- `treatment-evidence`：同上，随后做整组比较。
- 一行失败仍保留 failed；另一行可独立运行时继续。公共环境故障导致不可运行时标记后续 skipped
  并说明原因，不能生成假 summary。需要改变矩阵/协议算法时按工程流程报告。
- 实施授权已由用户明确提供；此前票内运行见 implementation-notes.md 和 run-ledger.md。

## Artifact Contract

### 输入与命令

P 包含单一 `.cc` 驱动、只读比较助手、计划、矩阵、manifest、ledger、两份 case spec。
case 用 `--variant` 选择，固定 workload 在驱动中通过生产 API 提交；不新增 CSV schema。
具体命令见 [command-manifest.yaml](command-manifest.yaml)。不新增 sweep 行或故障网络变体。

`record-run.py` 仅包装 manifest 中的同一命令、保留旧证据并记录来源，不生成网络输入。不存在独立生成脚本。实施者物化文档并实现驱动后，先核验 manifest；运行阶段不得即兴生成新
case/命令/输出目录。输出已存在时先记录归档位置和重试原因，不静默覆盖旧证据。

### 每个 case 的输出

固定目录 `P/cases/<case-id>/output/`：

| 文件 | 内容与证据类别 |
| --- | --- |
| events.csv | 原始 measured 事件、时间、顺序、接收值和已有 trace 的网络事件 |
| summary.json | 由事件计算的 trace-derived 时间/次数/有效性，保留错误原因 |
| resolved-parameters.json | 实际应用参数、拓扑/通道、程序版本、时间分辨率、初始值和 variant |
| console.log | stdout/stderr，由执行器捕获，log-derived；启动失败也应保留 |
| run-record.json | record-run.py 捕获的命令、UTC 起止、HEAD/dirty、源码/二进制哈希、退出码和归档关联 |

schema version 为 1。events.csv 固定列：

```text
seq,time_ps,node,event,task_id,opcode,address,byte_count,value,packet_bytes
```

- `seq` 单调递增且唯一，`time_ps` 为非负整数仿真时间；同时间戳仍按 seq 排序。
- `node` 为 A/B；opcode 用整数；address 用带 `0x` 的十六进制字符串。
- `value` 为无符号 64 位十进制整数。无关字段留空，不以 0 冒充缺失。
- 事件至少包含 `counter_init`、`wqe_submit`、`request_tx`、`request_rx`、`target_execute`、
  `data_commit`、`counter_update`、`counter_ready`、`taack_tx`、`taack_rx`、`a_complete`。
- control 另记录 `post_value_decoded`，treatment 另记录 `notify_value_decoded`：address/value
  为实际接收并解码的目标地址/数值，在计数更新前记录，作为输入来源的证据。
- counter_update/ready 的 address 为计数器地址，value 为实际更新后值；data_commit 的 address
  和 byte_count 来自接收数据请求。台账不能从 variant 预填事件。
- 网络事件从现有 trace 和生产解析边界取得；明确 trace 对应传输开始还是结束。
  packet_bytes 记录该边界可见的实际包大小，并注明是否含链路头、是否为完整 wire 大小。
  不将 TPACK 当 TAACK。不具备足够 trace 归因时明确记录证据缺口，不造时间戳。

summary.json 至少包含：

```text
schema_version, case_id, variant, valid, errors,
t_submit_ps, t_data_commit_ps, t_counter_ready_ps, t_a_complete_ps,
b_ready_latency_ps, a_completion_latency_ps,
counter_address, counter_final, expected_total, committed_ranges,
target_execution_count, readiness_count, a_completion_count, data_packet_count
```

缺少事件对应值为 null，valid=false 并给出 errors；不能用零值代替缺失。
有效运行的计数：control execution=2、completion=2；treatment 均为 1；两者 readiness=1、
data_packet_count=1、counter_final=64，提交范围相同。所有可从事件得到的 summary 值必须可复核。

### 整组输出与判断

`P/output/comparison.json`、`P/output/comparison.md`，引用两行原始产物和 ledger。
比较器仅使用 Python 标准库即可，不引入新的实验服务/框架。

- 从原始事件重新计算 L_B/L_A、Δ_B、相对缩短 `100 * Δ_B / L_B(control)`，标明单位。
- 从参数求传播 RTT `2 * 1 μs`，列出 header/序列化差异和可观察等待，并报告未解释残差。
- 校验参数匹配时，只允许 case ID、variant、路径/运行元数据及操作组合派生项不同。
  参数不一致的组不能作为受控对照。
- 分开保存运行状态、结构有效性、预测状态。预测状态为 matched、partially_matched、mismatched
  或 inconclusive；没有完整归因证据时由分析记录缺口，比较器不靠任意时间容差自动断言 matched。
- 结构有效但未加速时保留有效测量，假设标为 mismatched，比较器可正常退出。
  缺失/无效证据返回非零，仍输出可读诊断报告；不能静默丢弃失败行。
- 记录 packet trace 的可见边界；不能把相关性观察写成真实硬件因果或统计显著性。

## Startup Readiness

2026-09-21 文档勘察：ns3 launcher 和默认 smoke 目录存在；工具子模块目录为空，
requirements 与关键 Python 工具脚本缺失；build 与 cmake-cache 不存在。
当时当时尚无运行时参数目录，也未构建独立驱动。网络数值覆盖项依据当前 C++ API 核实，默认值仍需
构建后发现并记录。不得把 manifest 作为可立即运行的命令清单。

## Analysis Notes

最终报告见 [output/comparison.md](output/comparison.md)，实际过程见 [run-ledger.md](run-ledger.md)。B 延迟分别为 3030800 / 1008320 ps，差值 2022480 ps；比传播 RTT 多出的 22480 ps 已由观测等待与包长差异解释。原始预测未修改。

实施阶段已完成必要 release 构建和 41 个组件的运行时查询；当前基线为 output/runtime-catalog。工具子模块不参与该独立驱动，未作无关安装。
