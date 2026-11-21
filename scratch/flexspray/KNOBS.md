# FlexSpray 属性审计（第十一轮，2026-11-11）

**范围**：相对上游基点 6c28e38b，分支一共新增 56 个属性。第十一轮之前的完整代码保留在标签 `flexspray-r10-pre-audit`（68aeeb91）。

**三类**：
- **核心**：默认 v5.3 路径会用到，包括由 `flexspray_case.py` 的 `flex` 方案设置的属性，以及取默认值、属于已采纳设计的参数。
- **候选**：默认关闭，有证据支持，按原则归入下面 3 个组件之一，仍在评估中。
- **已否定**：已删除代码路径。

## 核心（34 个）

| 组件 | 属性 | 说明 |
|---|---|---|
| SprayCC（端侧速率 CC） | `AiFraction`（flex 设为 0.15）、`CapSlack`、`Coupled`、`EdgeOnly`、`Gain`、`KDelay`、`MinRateFraction`、`Period`、`WindowRttFactor` | 已采纳的 CC，取默认值 |
| SprayCC 的设计空间参数 | `Pacing`（默认 false）、`HyperIncrease`（默认 false） | v5.2 调参时探索过，结论是根因不在参数（`history/REPORT_round4_v5.2.md`）。它们是 CC 的可选模式，不是为某一失败行加的开关，所以保留 |
| TP 组分发（逐包 / 平面 / 端口分担） | `TpgDispatchPolicy`（MIN_DRAIN_TIME）、`TpgDispatchSlack`（0）、`DeliveryRateMinInterval`、`DeliveryRateGain`、`DeliveryRateAppLimitedGuard` | 功能 (1)–(3)、(5) |
| 异构平面的公平份额 | `TpGroupFairShare`、`TpGroupFairShareExponent`、`TpGroupFairShareUsableOnly` | 功能 (4) |
| READ 发送窗与 VL | `MaxOutstandingReadBytes`、`MaxActiveReadSegments`、`MaxActiveSendSegments`、`ReadQuotaFollowsUsablePlanes`、`ReadRequestVl`、`ResponseVl`、`CcExemptReadResponses`、`ReadIngressMarkWeight`（0.3） | v2–v5.3 |
| 故障检测与恢复 | `FailureDetection`、`FailureSuspectAfter`、`FailureProbeTimeout`、`FailureProbeRetries`、`ReplayFence` | 功能 (6) |
| 尾段切分 | `TailBytes`（默认 0）、`TailSegmentBytes` | 默认关闭，但有单测（`ub-test.cc`，尾段切分）和恢复回归（`flexspray_regress.py tail4k`）覆盖，所以保留 |

**故障模型参数**（GlobalValue，不是机制开关）：
- `UB_LINK_HEALTH_LOCAL_DETECT_NS`；
- `UB_LINK_HEALTH_REMOTE_NOTIFY_NS`；
- 第十一轮新增的 `UB_LINK_HEALTH_FABRIC_WITHDRAW_NS`（2 µs）：fabric 路由撤回的逐跳延迟，T20 修复使用。它的取值是**假设**，需要做敏感性分析，见 TODO。

## 候选（8 个属性，3 个原则性组件）

| 组件（一般原则） | 属性 | 证据 / 状态 |
|---|---|---|
| C1 计时器不能短于路径当前的 RTT | `RtoSrttFactor`、`RtoFloorFirstOnly` | 即 B1。消除多余超时（FEASIBILITY §7.2）。代价：同一包连续丢 8 次时比 B0 差约 20 µs |
| C2 承诺以时间而不是字节计 | `PortCommitDelay`、`ReadPortBudgetBytes` + `ReadBudgetScaleByRate` | 写方向与慢出口承诺的杠杆（FEASIBILITY §8.3–8.5）。READ 预算也属于 M |
| C3 统一的完成时间估计 | `ServiceTimeScore`、`ServiceResidual`、`ReadScoreReturnRate` | 写方向 1/4 通过；h2d −4 pp 来自残差（T19f，未验证） |

**原来的 M 与 S4**：两者都包含晚绑定，已失去候选资格（T19e）。现有的 M、S4 数据只作为历史对照，并标注"含活性缺陷"。

## 已否定（14 个，代码路径已删除）

| 属性 | 否定理由 | 证据 |
|---|---|---|
| `ResponseLateBinding`、`ResponseRebindAtSend`、`ResponseRebindRatio` | **T19e**：响应被晚绑定到另一平面后，该平面硬断，而请求所在 TP 仍然存活，响应便无法恢复：请求方不重放，响应方不重发。比率 1.25 时复现为挂死，比率 4 时潜在存在。修复需要响应方重放和去重，本轮不做 | `work/exp/round10/tr4`；FEASIBILITY §8.5 |
| `ReadBacklogFeedback`、`ReadBacklogThreshold` | SIRD 式的逐 TP AIMD 没有收益 | FEASIBILITY §5（第七轮） |
| `ReadQuotaDelayTarget`、`ReadQuotaGain`、`ReadQuotaMinBytes` | 时延目标配额在第六轮被否定 | FEASIBILITY §4.2 |
| `DispatchQueueDelayWeight` | SRTT − minRTT 混入了反向时延，并会永久弃用慢平面（h2d −5 pp）；被 C3 的残差取代 | FEASIBILITY §7.7 |
| `MaxActiveReadResponseSegments` | 是 mixedread F4 / H2up / H4 回退的来源 | FEASIBILITY §7.4–7.5 |
| `ReadBudgetScope`、`ReadBudgetStrict`、`ReadBudgetWorkConserving` | 第八轮的预算变体，没有一个优于每端口预算 | FEASIBILITY §6 |
| `RtoRttvarK` | Jacobson 式（SRTT + K·RTTVAR）在排队增长时低估，留下 13–25 次多余超时 | FEASIBILITY §7.2 |

同时删除了：
- RTTVAR 估计（只被 `RtoRttvarK` 使用）；
- `UbRespOriginTag`、`UbRespBacklogTag` 两个包标签，以及重传描述符里的 `respOriginTpn`；
- `GetCcQueueDelayNs`；
- 第十轮为晚绑定加的 500 ns 重试。

**删除后的验证**：见 DELIVERY.md。要求 golden 逐位相同、单测通过，并且默认与 B1 的 FlexSpray 运行逐位相同。

## 第十五轮补充：FlexSpray v6 核心属性合入与否定项（2026-11-21）

自第十五轮起，`flexspray_case.py` 中的默认 `flex+spraycc` 已升级为 **FlexSpray v6**（全部 C++ 默认值仍保持关闭以维持 Golden 25/25 `SAME`；历史对照提供 `+v53` 与 `+b1` 别名）。

### v6 新增并采纳的核心属性（共 20 项配置由 `flex+spraycc` 开启）

| 支柱 | 属性（v6 取值） | 作用说明 |
|---|---|---|
| 自适应 RTO（B1） | `RtoSrttFactor=8`、`RtoFloorFirstOnly=true` | 按路径 SRTT 自适应设置首包超时下界，避免降速队列误触发 GBN 重传 |
| M1 时间量子分段与窗口 | `TimeQuantumSeg=true`、`ReadQuantumBytes=8192`、`QuantumTailBytes=49152`、`SlackTailOnly=true` | 依成员相对容量（第 75 百分位实测/控制速率）切分 WRITE/READ 量子、仅在近尾段启用延迟滞后分发，并修复非 CTP `UbJetty::SetOooAckThreshold` 窗口扩容与 `m_taSsnCnt` 维护及 `ServiceTimeNs` 遗漏 `ReadIngressMarkWeight` |
| M2 死口重池化与工作守恒 READ 预算 | `ReadPortBudgetBytes=409600`、`ReadScoreReturnRate=true`、`ReadBudgetScaleByRate=true`、`ReadBudgetRepoolDead=true`、`ReadBudgetWorkConserving=true` | 修复 `PortReadAdd` 有效预算与唤醒粒度（`4096`）；仅针对 `HardDown`/本地隔离端口按健康口比例重池化预算（绝不给降 Lane 慢口扩容），未饱和时允许活跃 READ 流水线工作守恒推进 |
| M3 去自排队残差与队列感知速率 | `PortCommitDelay=6us`、`ServiceTimeScore=true`、`ServiceResidual=true`、`ResidualSubtractOwn=true`、`ResidualIdleHalfLife=150us`、`DeliveryRateQueueAware=true` | 缩短写预承诺时间窗；扣除本 TP 在途自身串行化残差并保留 150 µs 空闲半衰期跨 MoE 间隙记忆；远端排队时避免 `AppLimited` 误判 |
| T29 调度协同 | `DesyncTpgCursor=true`、`PrioritizeHeavyWqe=true`、`CoalescePortTrigger=true` | 按 `BuildTpgPhaseHash` 初始化 TPG 游标去同步、优先派发同拍剩余字节最多的大 WQE、在 `UbPort` 侧合并同拍 `TriggerTransmit` 调用（`UbPort::m_coalescedTrigger`，`DoDispose` 清理） |

### 已否定（第十五轮，代码已删除）

| 属性 | 否定理由 | 证据 |
|---|---|---|
| `ReadQuantumOnFaultOnly` | 仅在故障后启用 READ 小量子会在正常/微扰动切换窗口错失小段流水化收益，导致 `a2aread F3far3` 退化（`11.44% -> 15.44%`） | Round 15 开发探针；已从 `UbTransaction` 删除 |
