# GitCode next 与生命周期分支复杂 CTP/RTP 差异测试报告

## 结论

本轮没有发现无法解释的 CTP/RTP 行为回归。

- CTP Jetty inflight/window 行为已被明确触发，并在 `next@cf8ee683` 与当前生命周期分支间
  完整复现：三档窗口的所有共享工件逐字节一致。
- RTP 的共享 TP 小窗口、公平推进、GBN 双丢包恢复、TPG 并发与成员重传都完成；发现的三类
  差异都能收敛到已有、明确的生命周期语义，不存在 task 丢失、重复完成、永久阻塞、错误
  重传迁移或未知 wire-level 差异。
- 这个结论只覆盖本报告的冻结矩阵，不外推到尚未执行的 PFC/DCQCN 与 RTO 同时边界。

## 比较边界

- control：`next@cf8ee683e00e5b6210661013e93cec7217d662e5`
- treatment：`824b87a0503133a6ca4f43f8bd8ce9bbe2631341`
- 两侧复用各自已构建的 release `ub-quick-example`，未重新 build。
- 每个 paired case 的输入文件 SHA-256 完全相同，固定 `rng-run=1`，12 次仿真严格串行。
- 字节比较覆盖双方共有的 `output/` 与 `runlog/`；生命周期 schema/trace 只在 treatment
  出现，单独由 production lifecycle parser 校验。

## CTP：Jetty inflight/window 已复原

CTP workload 不是单包 smoke：4 个同时释放的 1 MiB `URMA_WRITE` 使用相同 source、
destination、Entity 和 VL，因此复用同一个 bound Jetty。每个 WQE 继续拆成多个 TA segment，
只改变 `ns3::UbJetty::UbJettyInflightMax`。

| Window | task 0 完成 | task 1 完成 | task 2 完成 | task 3 完成 | next vs lifecycle |
|---:|---:|---:|---:|---:|---|
| 1 | 930.35344 us | 1846.53688 us | 2777.35832 us | 3667.86076 us | 16/16 共享工件字节一致 |
| 4 | 331.85448 us | 661.52440 us | 997.48524 us | 1318.99184 us | 16/16 共享工件字节一致 |
| 128 | 20.15192 us | 36.74032 us | 52.99816 us | 69.75184 us | 16/16 共享工件字节一致 |

窗口 1 到 4，最后一个 task 的完成时间缩短约 2.78 倍；窗口 4 到 128 又缩短约 18.91 倍。
所以这里不是“参数写了但没触发”：Jetty segment window 确实形成 backpressure，ACK/TAACK
释放窗口后继续推进。两版本的完成顺序、每个精确时间、task/packet/port trace 和 parser
输出均一致。

判定：`matched`，没有 CTP exception。

## RTP：共享 TP 的 inflight=1

场景复用 `medium-shared-tp-concurrency` 的 4 个双向 READ/WRITE task，把
`DefaultMaxInflightPacketSize` 收紧为 1。四个 task 在两版本的开始时间、首次发包、完成时间、
吞吐以及 packet/ACK/port evidence 完全一致。

仅 `TaskTrace_node_0.tr` 和 `TaskTrace_node_1.tr` 在同一时间戳内把 `Segment Sends` 与
`Segment Completes` 的输出顺序互换。对完整行排序后，两侧 multiset 字节一致；没有增加、
遗漏或改变事件时间。

判定：`partially matched`，属于已知 T05 same-timestamp trace ordering，不是调度结果变化。

## RTP：GBN 双丢包与累计 ACK

场景在一个 256 KiB WRITE 中确定性地各丢一次 PSN 1 和 PSN 3，GBN window 为 8。两版本
都完成，覆盖相同的逻辑 PSN 0..31，最终没有未完成 task。

差异是生命周期分支少发了 1 个 DATA attempt：sender raw packet record 从 140 行减为 138
行（同一 attempt 在该 trace 中记录两行）。`next` 对 PSN 8 多做一次重传；当前实现收到累计
TPACK 后，通过 `UbGbnRetransStrategy::AcknowledgePrefix()` 移除已经确认的 descriptor，
因此后续从 PSN 9 继续。

结果：

- `next` 完成时间：28.62410 us；
- lifecycle 完成时间：28.54062 us；
- lifecycle 提前 83.48 ns，约 0.292%；
- task throughput 从 73.2909 Gbps 变为 73.5053 Gbps，这是少一次冗余 wire attempt 的直接
  结果，不是仿真宿主机性能指标。

该差异符合 UB Base Specification 2.0 §6.4.2.2.2：TPACK 累计确认所有
`PSN <= acknowledged PSN` 的 TP Packet，发送端更新其交付状态并取消 RTO。已累计确认的
descriptor 不应继续留在 GBN 可重传前缀中。

判定：`partially matched`，属于已审核的 T06 协议修正，差异范围比 `next` 更窄且可接受。

## RTP：TPG 并发、丢包与成员 pinning

两个同时释放的 1 MiB WRITE 绑定同一个三成员 TPG，transport window 为 4；task 0 在
member 0 的 PSN 1 丢包一次，同时 task 1 保持 runnable。

协议结果一致：

- 两个 task 的完成时间逐字节一致：27.28718 us、28.62410 us；
- 所有 PortTrace 字节一致，交换节点 2/3/4 的 PacketTrace 字节一致；
- `throughput.csv` 字节一致；
- task 0 的 RTO/retransmission 仍在原 member 0，没有迁移到其他 TP。

差异只发生在 host 侧 task attribution。`next` 会同时调度 Jetty 绑定的多个 TP，让它们从
共享 Jetty 拉 segment；生命周期实现先把每个 admitted segment 固定到一个
`resolvedTpIdentity`，再提交 TPG cursor。因此 task 1 在生命周期分支可从 member 1 于
0.011 us 发出，而 `next` 的对应 `firstPacketSends` 是 1.2662 us。最终 wire timing、端口
字节和完成时间没有变化。

这不是一套新的 TPG 算法，而是生命周期所有权要求的 attribution/pinning：重传必须回到
最初 admitted 的 TP generation，不能推进另一个成员的 PSN。对应 invariant 已由
`UbLifecycleTpg...` 单测固定。

判定：`partially matched`，协议 wire behavior 一致，host task-to-TP 归属是明确的受控差异。

## 生命周期完整性

production parser 校验结果：

| Case | lifecycle records |
|---|---:|
| CTP window 1 | 23,408 |
| CTP window 4 | 23,408 |
| CTP window 128 | 23,408 |
| RTP shared window 1 | 506 |
| RTP GBN double drop | 682 |
| RTP TPG GBN | 1,452 |

全部通过 schema、identity、owner sequence、ordering 和 finite balance 校验。

## 最终判断

CTP window/backpressure 可以确认与 GitCode `next` 行为一致，而且不是简单 case 的假阳性。
RTP 三个最高风险 seam 均完成：共享窗口未饥饿、GBN 双丢包可恢复、TPG 重传未迁移。RTP
不满足“所有 trace 逐字节一致”，但差异分别是同时间戳记录顺序、规范要求的累计 ACK 前缀
回收、以及 generation pinning 的 task attribution；没有未知差异需要修改实现。
