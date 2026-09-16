# URMA atomic 使用说明

第一版支持 URMA/Jetty、ROI、RTP 和 CTP 的九种 atomic，模拟网络字节数、请求/响应时序和
任务完成。默认无需远端回调。一条 CSV 任务或一次 API 提交对应一个 WQE、一次操作、一个请求分段和一个请求包。

## 长度与操作

N 是操作数宽度，取值为 1/2/4/8/16/32/64 字节；它对应 `CreateWqe` 的 size 和 CSV 的
`dataSize(Byte)`。P 是请求 Payload，显式 `requestPayloadBytes` 优先，当前必须为 1..4096。
空值采用下表默认值；显式 0 被拒绝。S 是响应 Payload，不可独立覆盖。

| CSV opType | UbTransactionOpcode 后缀 | 默认 P | S |
| --- | --- | --- | --- |
| URMA_ATOMIC_COMPARE_SWAP | ATOMIC_CMP_SWAP | 2N | N |
| URMA_ATOMIC_SWAP | ATOMIC_SWAP | N | N |
| URMA_ATOMIC_STORE | ATOMIC_STORE | N | 0 |
| URMA_ATOMIC_LOAD | ATOMIC_LOAD | N | N |
| URMA_ATOMIC_FETCH_ADD | ATOMIC_FETCH_ADD | N | N |
| URMA_ATOMIC_FETCH_SUB | ATOMIC_FETCH_SUB | N | N |
| URMA_ATOMIC_FETCH_AND | ATOMIC_FETCH_AND | N | N |
| URMA_ATOMIC_FETCH_OR | ATOMIC_FETCH_OR | N | N |
| URMA_ATOMIC_FETCH_XOR | ATOMIC_FETCH_XOR | N | N |

Load/Store 必须指定子操作 ADD/CLR/EOR/SET/SMAX/SMIN/UMAX/UMIN，其余操作不接受子操作。
所有响应使用 0x13。Store 也实际发送响应包；内部非零 carrier 不增加 Payload 字节。
只有匹配的事务响应才能完成请求，RTP 请求的 TP ACK 不代表 atomic 完成。

## C++ API

复用已有 `UbFunction::CreateWqe`、`SubmitWqe` 和 `SubmitCtpWqe`，创建接口签名不变：

```cpp
auto wqe = function->CreateWqe(src, dst, 8, wqeId, UbTransactionOpcode::ATOMIC_FETCH_ADD);
wqe->SetAtomicOptions({.requestPayloadBytes = 16}); // N=8，P=16，S=8
auto result = function->SubmitWqe(wqe, jettyNum);
```

Load/Store 的选项示例：

```cpp
wqe->SetAtomicOptions({.subOperation = UbAtomicSubOperation::ADD,
                      .requestPayloadBytes = 16});
```

CTP 使用既有 Entity/端点配置及 `SubmitCtpWqe`。完整校验在 admission 前执行；非法 N/P、
缺失或错误子操作、非 ROI、非 atomic WQE 携带 atomic options 返回
`UbWqeSubmissionStatus::INVALID_ATOMIC_REQUEST`，不会先入队或分配 TASSN。
共享规则入口位于 `model/protocol/ub-transaction-rules.h`。

## traffic.csv

基础表头为：

```csv
taskId,sourceNode,destNode,dataSize(Byte),opType,priority,delay,phaseId,dependOnPhases
```

允许基础九列、基础 + `srcPortId,dstPortId`、基础 +
`atomicSubOperation,requestPayloadBytes`，以及基础 + 端口组 + atomic 组四种形式。
两种 11 列表头按名称区分；两组都存在时为 13 列，端口在前。字段名、顺序和行宽须准确，
旧 Entity-bearing 表头仍拒绝。非 atomic 行的两个 atomic 值均留空。

```csv
taskId,sourceNode,destNode,dataSize(Byte),opType,priority,delay,phaseId,dependOnPhases,atomicSubOperation,requestPayloadBytes
0,0,1,8,URMA_ATOMIC_FETCH_ADD,7,0ns,0,,,
1,0,1,8,URMA_ATOMIC_FETCH_ADD,7,0ns,0,,,16
2,0,1,8,URMA_ATOMIC_STORE,7,0ns,0,,ADD,
```

没有 atomic 参数列时，可用默认长度运行无需子操作的七种 atomic。CSV 转换后仍调用相同的
WQE options 和提交路径；混合任务沿用原有 phase、依赖、delay 与端点规则。

## 参考运行与证据

从仓库根目录复用已构建的 runner，串行执行：

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_atomic_rtp'
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_atomic_ctp'
```

每例复用两个端点、五交换机的现有拓扑，25 个任务覆盖九种 atomic、八子操作、七 N、
默认/显式 P，以及 Write → 23 个 atomic → Read 的 phase 依赖。配置为当前运行时参数全量
快照。具体输入和执行记录见各例 `experiment-spec.md`；原始验证日志与退出码在
[实施证据](../../../.scratch/atomic-support/evidence/)，API 测试为 `unified-bus-atomic`。

固定版本的 `ub_tools.py case check` 和 `trace parse` 接受这些 CSV，无需修改工具子模块。
collective 流量 CLI 仍只生成 Write。task statistics 使用 N 计算 atomic taskThroughput，
当 N 与 P/S 不同时它不代表线上吞吐；Payload 证据应使用生产包解析或对应 trace。
CTP 默认没有 AllPacketTrace，不能从其 `Last Packet ACKs` 的逻辑 N 推断响应 S。

## 模型边界

不计算操作数、旧值、CAS 成败或地址竞争；占位数据没有算术含义。子操作只在输入和本地
WQE 保留与校验，不编码到网络，也不在 Target 区分执行。默认不注册远端回调，自定义回调
处理 atomic 不属于本版验收范围。

沿用现有 RTP/CTP 封装，不新增网络头或 PacketTag 布局。CTP 仍保留 CFG=6 与精简事务头的
既有规范偏差；精确 N 复用现有 FlowTag，trace 关闭也可用，缺失或非法元数据明确拒绝。
不承诺接收无该元数据的外部 CTP atomic。未验证完整 MPI/MTP 配置矩阵和大规模性能。

## Knowledge Sources

- [已确认设计](../../../.scratch/atomic-support/design.md)：九操作、N/P/S、单包与兼容边界；
  其中记录了 UB 2.0.1 §7.4.2.3 与用户提供新版单操作数要求的不同来源。
- [领域模型](../../../CONTEXT.md)：URMA、ROI、操作数宽度与时序模型的统一术语。
