# Unified Bus 生命周期重构字节兼容性报告

## 1. 结论

本实验以 GitCode `main@ccf41b58b0ba3b9711efea595732a2908e3e07cf` 为控制组，以
`748708af3ad423582c83727f27f92a32495f271f` 为生命周期重构处理组。

- 20 个比较 block、40 个 control/treatment case 均已执行并形成结果。
- `m18` 另外覆盖 local-MTP、MPI-single 和 hybrid 三种运行模式，因此共有 22 份比较
  summary。
- 22 份比较的输入 manifest 均相同，比较器决策均为 `pass`。
- 共检查 503 个共享稳定工件实例，其中 384 个原始字节完全一致，119 个存在字节差异。
- 119 个差异全部落在预注册的 T05、T06、T14 原因范围内，并逐项匹配 artifact 路径、
  control/treatment SHA256 和首个差异字节偏移。
- 没有未解释、未审批或超出既有原因边界的字节差异。

因此，当前证据支持“行为字节兼容通过，存在 3 类已知受控差异”。这不等于性能验收通过：
`p01` 和 `p02` 的主机 wall time 仍超过 10% 性能门限。

## 2. 字节比较边界

“字节一致”只针对 control 和 treatment 都会产生、且在冻结规则中标记为共享稳定工件的输出：

- `exit-code.txt`
- `output/task_statistics.csv`
- `output/throughput.csv`
- `runlog/AllPacketTrace_*`
- `runlog/PacketTrace_*`
- `runlog/PortTrace_*`
- `runlog/QueueTrace_*`
- `runlog/TaskTrace_*`

以下内容不应误报为字节不兼容：

- `LifecycleTraceSchema.json` 和 `LifecycleTrace_node_*.tr` 是 treatment-only 工件，因为
  GitCode main 没有生命周期追踪能力。它们通过 production lifecycle parser 检查 generation、
  terminal、ordering 和 balance，而不是与 control 做字节比较。
- `stdout.txt` 和 `stderr.txt` 保留原文，但包含绝对工作区路径、wall-clock 时间等宿主机信息，
  因此只做路径归一化后的语义比较，不进入原始字节 gate。

## 3. 共享稳定工件完全字节一致的 block

下列 block 的所有共享稳定工件均为原始字节完全一致：

| Block | 功能边界 | 完全一致工件数 | 说明 |
|---|---|---:|---|
| `m07` | CTP single WRITE | 17 | task、packet、port 和完成结果均一致 |
| `m09` | CTP Entity key isolation | 17 | 两个 entity 的隔离行为与输出一致 |
| `m10` | LDST MEM_STORE | 9 | 请求、ACK 和完成输出一致 |
| `m11` | LDST MEM_LOAD | 9 | 请求、响应和完成输出一致 |
| `m15` | PFC pause/resume | 10 | pause/resume 与任务完成输出一致 |
| `m16` | route failure terminal | 7 | 共享稳定输出一致；诊断路径仅做归一化比较 |
| `m17` | fault drop without recovery | 12 | 共享稳定输出一致；诊断路径仅做归一化比较 |

`m16` 和 `m17` 的 treatment lifecycle summary 保留了各 4 个预注册的 unresolved operation
lease，并分别记录一次 route terminal 和一次 fault terminal。这是无恢复策略规定的可见状态，
不属于共享输出字节差异，也不是生命周期泄漏误判。

## 4. T05：相同时间戳记录顺序差异

### 差异位置

T05 涉及 21 个 `TaskTrace` 工件实例，其中 19 个是纯 T05，另外两个分别与 T06、T14
叠加：

- `m01`、`m02`、`m03`、`m04`：各 1 个 `TaskTrace_node_1.tr`
- `m05`：`TaskTrace_node_0.tr` 和 `TaskTrace_node_1.tr`
- `m06`：`TaskTrace_node_1.tr`，同时受 T06 影响
- `m12`：`TaskTrace_node_1.tr`，同时受 T14 影响
- `m13`：`TaskTrace_node_2.tr`
- `m14`：`TaskTrace_node_1.tr`
- `p02`：`TaskTrace_node_16.tr` 至 `TaskTrace_node_23.tr`
- `m18`：local-MTP、MPI-single、hybrid 各 1 个 `TaskTrace_node_1.tr`

### 为什么受控

这些文件中的事件集合和时间戳没有变化，只是同一个时间戳桶内的 `Sends`、`Completes`
记录先后顺序变化。审批证据逐文件验证了：

- control 和 treatment 的行数相同；
- 排序后的完整行 multiset 相同，没有新增或丢失事件；
- packet、ACK、port、task statistics、throughput 等其他共享工件仍保持字节一致；
- 每个差异文件都记录了固定的 control/treatment SHA256 和首差偏移。

18 个 T05 工件的首差偏移为 25；`m05` 两个文件分别为 527 和 319；`m12` 的组合差异
从偏移 1 开始。偏移不同没有被泛化放行，每个文件仍必须命中自己的审批哈希和偏移。

因此，T05 表示 trace 文本序列化顺序变化，不表示任务、包、ACK 或协议事件集合发生变化。

## 5. T06：累计 ACK 回收 canonical GBN descriptor

### 差异位置

T06 只出现在 `m06` 的 GBN 单包丢失恢复场景，共影响 15 个共享工件：

- `output/task_statistics.csv`：1 个
- `output/throughput.csv`：1 个
- `AllPacketTrace_ACK_node_*`：2 个
- `AllPacketTrace_PKT_node_*`：2 个
- `PacketTrace_node_*`：3 个
- `PortTrace_node_*`：4 个
- `TaskTrace_node_*`：2 个，其中 `TaskTrace_node_1.tr` 同时包含 T05 顺序差异

### 为什么受控

这不是无意义的文本变化，而是 T06 明确要求的协议行为修正：累计 ACK 到达后，处理组会回收
已经被累计确认的 descriptor 前缀，不再从这些 descriptor 重建冗余 GBN 重传包。

冻结例外证据表明：

- 恰好减少 3 次冗余 packet attempt；
- 丢包恢复和最终任务完成仍然成功；
- 完成时间提前 251 ns；
- goodput 从 73.2909 Gbps 变为 73.938 Gbps；
- 6 个 focused test 覆盖 descriptor prefix retirement、final ACK/timer、TP generation 和三类
  cleanup seam；
- 本次实验的 15 个差异逐项精确匹配既有 T06 exception 的路径、哈希和偏移。

因此，T06 是已审核的协议修复带来的预期输出变化。它改变的是冗余重传次数和派生时序，
没有改变“丢包后正确恢复并完成”的协议结果。

## 6. T14：packet spray 改为严格轮询

### 差异位置

T14 共涉及 85 个工件实例，其中 84 个是纯 T14，1 个同时包含 T05：

| Block | 差异数 / 比较数 | 主要受影响工件 |
|---|---:|---|
| `m08` | 32 / 37 | task statistics、throughput、PacketTrace、PortTrace、QueueTrace、TaskTrace |
| `m12` | 31 / 32 | task statistics、throughput、AllPacketTrace、PacketTrace、PortTrace、TaskTrace |
| `p01` | 22 / 23 | task statistics、throughput、AllPacketTrace、PacketTrace、PortTrace、TaskTrace |

### 为什么受控

旧实现的 packet-spray 选择从 `flowHashBase + 递增 salt` 起步；T14 改为只使用递增 salt，
使每次选择成为严格 round-robin。路径分配发生变化后，端口负载、队列轨迹、包时序、任务完成
时间和派生吞吐输出自然会变化。

受控证据不是仅凭原因说明放行，而是做了同场景反事实验证：

- `m08` 设置 `UsePacketSpray=false` 后，全部共享模型工件字节一致；
- `p01` 设置 `UsePacketSpray=false` 后，全部共享模型工件字节一致；
- `m12` 关闭 packet spray 后，T14 差异消失，只剩单独验证过的 T05 TaskTrace 顺序差异；
- 三个 block 的输入 manifest 原始字节一致；
- 每个 T14 差异都有 case-bound artifact 路径、SHA256 和首差偏移；
- task membership 和任务完成结果保持不变，生命周期 finite attempt 与 terminal balance 通过。

这组反事实实验把差异精确隔离到了“启用 packet spray 时的起始索引算法”，因此 T14 是有意的
调度语义变化，不是生命周期重构随机改变了 packet builder、parser 或协议状态机。

## 7. 全部 block 的字节比较汇总

| Block / 模式 | 字节差异数 / 比较数 | 判定 |
|---|---:|---|
| `m01` | 1 / 13 | T05 受控 |
| `m02` | 1 / 22 | T05 受控 |
| `m03` | 1 / 13 | T05 受控 |
| `m04` | 1 / 22 | T05 受控 |
| `m05` | 2 / 19 | T05 受控 |
| `m06` | 15 / 26 | T06，含 1 个 T05 组合差异 |
| `m07` | 0 / 17 | 完全字节一致 |
| `m08` | 32 / 37 | T14 受控 |
| `m09` | 0 / 17 | 完全字节一致 |
| `m10` | 0 / 9 | 完全字节一致 |
| `m11` | 0 / 9 | 完全字节一致 |
| `m12` | 31 / 32 | T14，含 1 个 T05 组合差异 |
| `m13` | 1 / 22 | T05 受控 |
| `m14` | 1 / 13 | T05 受控 |
| `m15` | 0 / 10 | 完全字节一致 |
| `m16` | 0 / 7 | 完全字节一致 |
| `m17` | 0 / 12 | 完全字节一致 |
| `m18` local-MTP | 1 / 13 | T05 受控 |
| `m18` MPI-single | 1 / 13 | T05 受控 |
| `m18` hybrid | 1 / 13 | T05 受控 |
| `p01` | 22 / 23 | T14 受控 |
| `p02` | 8 / 141 | T05 受控 |
| **合计** | **119 / 503** | **无未解释差异** |

## 8. 受控差异的准入标准

本报告将差异判为受控，必须同时满足以下条件：

1. 原因属于实验前冻结的 T05、T06、T14 范围，不能在看到结果后新增宽泛原因。
2. control 和 treatment 输入 manifest 字节一致。
3. 每个差异都绑定精确 artifact 路径、control SHA256、treatment SHA256 和首差偏移。
4. T05 必须证明完整行 multiset 不变；T06 必须命中既有 descriptor-GBN exception；T14
   必须通过关闭 packet spray 的同场景反事实验证。
5. 所有未列入审批的共享稳定工件必须保持原始字节一致。
6. treatment-only lifecycle evidence 必须通过 finite attempt、typed terminal、generation、
   owner ordering 和 balance 检查。

当前 119 个差异全部满足这些条件。如果以后任何 artifact 出现新路径、新哈希、新首差偏移，
即使原因名称看起来相似，也必须重新调查，不能自动继承本次审批。

## 9. T06 的 UB 2.0 协议原文依据

T06 在 RTP/GBN 层符合规范，但准确表述应是：收到累计 `TPACK` 后，将
`PSN <= acknowledged PSN` 的 TP Packet 更新为已正确接收；GBN 只重传仍未确认的范围。
`descriptor` 是本实现的内部存储形式，不是规范术语。

UB Base Specification 2.0 英文版 §6.4.2.2.2，第 181 页明确写道：

> Because the TPACK (PSN=m+3) is able to confirm that the TP Packet whose PSN is less than or
> equal to m+3 is correctly received, the TP sender updates the delivery status to indicate that
> all TP Packets with PSNs <= m+3 have been correctly received. The TP sender cancels the RTO timer.

中文版 §6.4.2.2.2，第 172 页对应为：

> 因为 TPACK(PSN=m+3)可以累计确认 PSN<=m+3 的 TP Packet 被正确接收，所以 TP Sender
> 会更新发送状态为 PSN<=m+3 的 TP Packet 均被正确接收，TP Sender 取消超时重传计时。

同一规范的 §6.4.2.2 还规定：超时后重传所有未被确认的 TP Packet；收到 TPNAK 后，从其
PSN 所标识的报文开始重传其后的已发送报文。因此，已经被累计 TPACK 覆盖的 descriptor
不能继续留在可重传前缀中。T06 回收该前缀是协议状态的实现，不是把 GBN 改成选择重传。

边界也必须保留：上述结论只适用于 RTP transport ACK/GBN；数据链路 Retry Buffer 按 flit
和 ACK 粒度释放，不能直接套用 TP descriptor；CTP 没有 transport-layer ACK 和端到端重传。

规范来源：

- `dev-knowledge/architecture/UB-Base-Specification-2.0-en.pdf`
- `dev-knowledge/specification/UB-Base-Specification-2.0-zh.pdf`

## 10. 与 GitCode `next` 的实现和字节校准

生命周期分支已在实现提交 `5abb058` 合并并校准到
`next@cf8ee683e00e5b6210661013e93cec7217d662e5` 的 canonical routing model：

- `RoutingType` 独立表达 per-flow/per-packet 与 all-paths/shortest-paths；
- `MultipathSelector` 独立表达 HASH64、CRC32、TOEPLITZ、ROUND_ROBIN、ADAPTIVE 和
  INGRESS_PORT_STRIPE；
- packet spray 不再被隐式等同为 strict RR；只有显式 `ROUND_ROBIN` 才使用 `next` 的
  candidate-set cursor；
- route storage 仍由生命周期分支的 `UbRoutingTable` owner 持有，未复制第二套路由表；
- RTP descriptor PSN、packet-attempt 注册和 CTP transaction retirement 接缝保持不变。

字节校准使用 `next` 的真实 `t-ctp-packet-rr` case。两边从同一组输入文件复制运行，输入
SHA256 相同。`diff -rq` 的结果是：所有共享输入、task statistics、throughput、packet、ACK、
port、queue 和 task trace 文件完全字节一致；生命周期分支只多出预期的
`LifecycleTraceSchema.json` 与 7 个 `LifecycleTrace_node_*.tr`。production lifecycle parser
成功验证 6072 条 lifecycle records。

因此，此处不再沿用原报告的“生命周期分支自定义 strict RR 与 main 的 T14 受控差异”作为
未来目标语义。合并 `next` 后，canonical routing 的 RR 行为和共享输出已经与 GitCode `next`
字节级一致；相对旧 GitCode main 的差异应按新 routing model 重新解释。

## 11. `p01` 性能结论

是的，`p01` 表明当前生命周期版本在宿主机执行时间上更慢：原始中位数从 1.531603 s
增加到约 1.8182 s，wall time 为 `+18.71%`；交错复测仍为 `+17.03%`，5 对 paired delta
全部为正，中位 paired delta 为 `+18.37%`。这不是统计噪声，也不能标记为性能通过。

但它不表示模拟网络性能变差。这里测量的是运行仿真的 CPU/宿主机 wall time；网络 goodput、
任务完成和仿真时间属于另一组模型输出。RSS 基本持平：原始比较 `-0.62%`，交错复测
`+0.14%`。

诊断进一步排除了两个主要混淆因素：关闭 lifecycle trace 只减少约 `0.6%-1.3%`；关闭
packet spray 且关闭 trace 后，treatment 相对对应 control 仍慢 `20.23%`。所以当前证据把
主要开销定位到 per-packet lifecycle bookkeeping，而不是 trace 文本输出或 T14 选路差异。
该性能回归仍需后续优化，不能因为字节兼容通过而放行性能门限。

## 12. 证据位置

- 实验总体结论：`experiment-plan.md`
- 执行与判定记录：`run-ledger.md`
- 冻结比较规则：`comparison-rules.json`
- 每个 block 的原始结果：`comparisons/<block>/summary.json`
- 每个受控差异的窄审批：`comparisons/<block>/approved-differences.json`
- T14 反事实结果：`.openusim/gitcode-main-lifecycle-differential/diagnostics/*-no-spray/summary.json`
- 既有里程碑例外：`tests/unified_bus_lifecycle/exceptions/*.json`
