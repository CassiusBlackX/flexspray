# FlexSpray 关键可调参数字典与默认状态（Round 19 / v7）

所有 FlexSpray C++ 全局/对象属性均**默认关闭或保留原版安全值**，确保原生 Golden `25/25 SAME` 与默认全量 50 项单元套件零退化；仿真时由 [flexspray_case.py](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/flexspray_case.py) 按方案别名注入 `network_attribute.txt`。

---

## 1. `flex+spraycc`（v7 完整方案）启用的传输层、事务层与拥塞控制参数

| 类别 | C++ 属性全称 | `flex+spraycc` (v7) 值 | `+v6` 差异 | 机制与作用 |
|---|---|---|---|---|
| 多路径分发 | `UbApp/UbTransportChannel::RoutingType` | `PER_PACKET_SHORTEST_PATHS` | 同 | 启用跨平面逐包喷洒路由 |
| 分发策略 | `UbTransaction::TpgDispatchPolicy` | `MIN_DRAIN_TIME` | 同 | 基于预测最小完成时间选路（`O(P)` 归约） |
| **M1 时间量子** | `UbTransaction::TimeQuantumSeg` | `true` | 同 | 按成员交付速率分配时间量子段，防慢口吸走过量字节 |
| | `UbTransaction::ReadQuantumBytes` | `8192` | 同 | READ 请求 8 KB 小量子流水 |
| | `UbTransaction::QuantumTailBytes` | `49152` | 同 | 尾部阈值与仅尾部允许滞后（`SlackTailOnly=true`） |
| | `UbTransaction::DesyncTpgCursor` / `PrioritizeHeavyWqe` | `true` / `true` | 同 | 打散 TPG 起点碰撞与重型 WQE 优先（T29） |
| **M2 READ 池化** | `UbTransportChannel::ReadPortBudgetBytes` | `0` | `409600` | v7 由动态 `ReadPortCommitDelay` 驱动 |
| | `UbTransportChannel::ReadPortCommitDelay` | `8us` | `0ns` | v7 动态 READ 端口预承诺窗口 |
| | `UbTransaction::ReadScoreReturnRate` | `true` | 同 | 按返回路径交付率评分 |
| | `UbTransportChannel::ReadBudgetScaleByRate` | `true` | 同 | 降速口按物理速率等比缩限 READ 配额，杜绝隐性补贴 |
| | `UbTransportChannel::ReadBudgetRepoolDead` / `ReadBudgetWorkConserving` | `true` / `true` | 同 | 仅在 `HardDown`/隔离时工作守恒重池化死口配额 |
| | `UbTransportChannel::ReadRequestVl` / `CcExemptReadResponses` | `1` / `true` | 同 | READ 请求高优 VL 隔离 + 响应接收端驱动不受发送窗口卡死 |
| | `UbTransaction::ReadIngressMarkWeight` | `0.3` | 同 | READ 避让接收端 Edge ECN 拥塞平面 |
| **M3 残差与 RTO** | `UbTransaction::ServiceTimeScore` / `ServiceResidual` | `true` / `true` | 同 | 基于剩余服务时间的排空预测 |
| | `UbTransportChannel::ResidualSubtractOwn` | `true` | 同 | 扣除本端已提交在途字节，消除自排队误判 |
| | `UbTransportChannel::ResidualIdleHalfLife` | `150us` | 同 | 跨越 MoE/SimAI 计算间隙记忆通道异常并平稳半衰 |
| | `UbTransportChannel::DeliveryRateQueueAware` / `AppLimitedGuard` | `true` / `true` | 同 | 队列感知交付率与超分场景 app-limited 保护 |
| | `UbTransportChannel::PortCommitDelay` | `6us` | `6us` | v7 与 `CommitDelayRttFactor` 协同按 SRTT 自适应缩放 |
| **v7 新增维度** | `UbTransportChannel::CommitDelayRttFactor` | **`4.0`** | `0` | 无量纲 SRTT 比例提交窗口（适配 `1x–10x` RTT） |
| | `UbTransportChannel::ProbeTimeoutSrttFactor` | **`2.5`** | `0` | 无量纲探测超时倍率，替代死板固定 `25us` |
| | `UbTransportChannel::SilenceAwareResidual` | **`true`** | `false` | 静默窗口残差修正，防止空闲间隔误偏 |
| | `UbTransportChannel::{BypassCommitForHighPrio}` (`TXN` + `TP`) | **`true`** | `false` | 高优控制/同步报文绕过端口承诺滞留，`ctrl_p99` 降 `3.4x–13.2x` |
| | `UbTransportChannel::ResetRateOnPortUpgrade` | **`true`** | `false` | `RESTORE`/`UP` 自愈瞬时重置成员速率下界，快速吃回满血带宽 |
| | `UbHostSprayCc::RecoverFastOnUnmarked` | **`true`** | `false` | 无 ECN 标记 ACK 加速恢复 AI 窗口 |
| | `UbTransportChannel::RecycleAfterLinkUp` | **`true`** | `false` | `UP` 后触发代际升代回收 + 双向响应队列唤醒与 `m_ackQ` 排空 |
| **故障检测与重传** | `UbTransportChannel::FailureDetection` / `FailureRecycleInterval` | `true` / `25us` | 同 | 自主 suspect->probe->dead 状态机与快速重路由隔离 |
| | `UbTransportChannel::EnableRetrans` / `RetransmissionMode` | `true` / `GBN` | 同 | 硬件丢包后 `UbReplayFenceTag` 隔离保护重传 |
| | `UbTransportChannel::RtoSrttFactor` / `RtoFloorFirstOnly` | `8` / `true` | 同 | 自适应 RTO 与首跳保底 |
| **拥塞控制与仲裁** | `UB_CC_ALGO=SPRAYCC`, `UbHostSprayCc::AiFraction` | `0.15` | 同 | 边缘轻量 SprayCC（单边 `m_kDelay=8B`，无核网状态） |
| | `UbTransportChannel::CoalescePortTrigger` / `TpGroupFairShareUsableOnly` | `true` / `true` | 同 | 端口合批触发防风暴 + 仅按可用平面加权公平仲裁 |

---

## 2. 流控体制（E4）与基线别名说明

- **`+fc_pfc`**：`FlowControl=PFC_DYNAMIC`, `ReservePerQueueBytes=65536`, `SharedPoolBytes=2097152`, `HeadroomPerPortBytes=65536`, `AlphaShift=1`。
- **`+fc_none+sack+buf{128,256}`**：`FlowControl=NONE`, `RetransmissionMode=SELECTIVE`, `EnableFastRetrans=true`，配合交换机浅缓（`128 KB` / `256 KB`）。
- **SOTA 竞品参数来源**：仅在开发集 `seeds=1, 2`（`tune_s12` 504 次调优）锁定最优点后零修改盲测 `seeds=15, 16`，包括 `sird+spraycc`、`reps+spraycc`、`mprdma+spraycc`、`hpn+spraycc`、`adaptive+caqm`、`ecmp+dcqcn`。
- **历史淘汰探针记录**：`ReadQuantumOnFaultOnly` 因在稳态/过渡窗口损失流水线收益（Round 15 致 `a2aread F3far3` 退化 `11.44% -> 15.44%`）已从 `UbTransaction` 彻底移除。
