状态: 已完成
阻塞于: 02

# 验证 allocator correctness 与 forced stop

## 目标

证明按端口 ownership 没有提前回收、漏取消、漏 residue 或改变调度结果。

## 验证矩阵

- RR、DWRR；allocator direct completion on/off。
- 单 retrigger、同端口多 retrigger、多个输出端口并行 pending。
- TP backing reclaim gate、VOQ destroy gate、callback-entry 查询。
- graceful completion、forced stop、pending 和 running callback。
- local 与 MTP owner-cleanup。

## 验收

- focused lifecycle/allocator/VOQ/TP reservation/CTP/retransmission suites 通过。
- forced stop 后没有 allocator callback 残留进程或悬挂访问。
- residue event count 与 active-work count 精确匹配预期。
- canonical event identity、timestamp 和任务输出与当前 PR 基线一致。

## 完成证据

- focused lifecycle、allocator/VOQ、TP reservation、CTP lifecycle、retransmission、parallel
  lifecycle 与 aggregate Unified Bus suites 全部通过。
- RR/DWRR、direct/scheduled、single/multiple retrigger、multi-outPort、TP reclaim gate、pending 与
  running forced stop、local/MTP cleanup 均有直接回归覆盖。
- reentrant forced stop 修复完成独立静态复审，无 P1/P2。
- correctness canary 状态为 `matched`：两侧各 `12,288` 个 deterministic task events，canonical
  文件字节一致，identity/timestamp 完全一致，runtime storage snapshot 完全一致。
- 验证与 canary 工件见
  `/Users/ytxing/workspace/ns-3-ub-architecture-integration/.scratch/allocator-event-ownership-fastpath/`
  和
  `/Users/ytxing/workspace/ns-3-ub-architecture-integration/scratch/20260725-output-port-local-ownership-canary/`。
