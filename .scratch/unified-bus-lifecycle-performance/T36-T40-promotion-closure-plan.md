# Unified Bus Lifecycle Promotion Closure Plan

Status: approved

## Goal

将 `codex/unified-bus-lifecycle-implementation` 收敛为唯一的 Unified Bus lifecycle future-
architecture integration branch，并形成可面向 `next@cf8ee683` 做 promotion review 的完整代码、
tracker、证据和验收闭环。

本计划不拆出长期 RTP、CTP、DCQCN 或 Function/Transaction 功能分支。所有 ticket 严格串行；
前一 ticket 的 implementation commit、closeout commit、focused validation 和 root review 完成前，
不得开始下一 ticket。

## Fixed Baselines

- Pre-refactor behavior baseline: `main@ccf41b58`
- Integration baseline: `next@cf8ee683`
- Current remote head: `63d3b5e`
- Current worktree: `63d3b5e` 加尚未提交的 T36 CTP one-packet-per-operation 固化
- Build/test policy: 单个 build/test 串行运行；普通 ticket 不跑全量 `./test.py`
- Byte policy: 普通 owner-only contraction 只跑 focused suites；T36 aggregate 修复和 T40 final
  promotion 才执行既定 frozen/differential comparisons

## Scope Boundary

本轮必须收口：

1. T36 aggregate manifest 的可移植 next-root 解析及一次完整 exit 0 结果。
2. T34/T35 的 tracker、报告、summary 和 T36 blocking edge。
3. T36 post-review CTP 规范固化的 source/test/closeout 提交。
4. DCQCN idle reset/continuity 的明确语义。
5. CTP Transaction-owned state 的类型所有权。
6. Transaction 到 Function/Jetty 的 WQE terminal/drain seam。
7. promotion tree 的 artifact 取舍和最终 acceptance report。

本轮明确不做：

- 自动 TP cleanup。
- 新的 packet builder/parser、ACK/RTO、queue、routing、flow-control 或 DCQCN 算法。
- 将 RTP TPG collection 从 `UbController` 迁入新 `UbRtpTransportService`；当前实现没有该实体，
  此项作为后续架构深化候选，不混入本轮收口。
- LDST 算法重构。T34 已测得的 LDST 性能结果必须进入最终报告；是否接受或另开优化 ticket
  由 T40 promotion gate 明确记录，不能静默忽略。

## Frontier 0: Finish T36 Before New Work

### T36-A: Make the aggregate evidence portable

改动范围：

- `scratch/20260720-ctp-fastpath-memory-scaling/scripts/run_experiment.py`
- `scratch/20260720-ctp-fastpath-memory-scaling/command-manifest.yaml`
- T36 task brief/report/summary

实现原则：

- 不在脚本中保存任何机器固定绝对路径。
- 将 next checkout 作为显式 `--next-root` 或 `T36_NEXT_ROOT` 输入；启动时验证
  `git rev-parse HEAD == cf8ee683`、binary/library 存在并记录哈希。
- final role manifest 必须使用本次实际运行的 resolved root，不能回退到已删除的
  `.worktrees/t36-next-baseline`。
- 不重跑已经不受代码差异影响的 2 GiB/8 GiB性能矩阵；只重跑生成完整 role manifest 和
  aggregate correctness result 所需的最小集合。如果脚本结构要求重跑大型矩阵，应先停下缩小
  finalization seam，而不是直接启动多个长仿真。

验收：

- aggregate command exit 0。
- role manifest 记录 `next@cf8ee683` 和当前 candidate 的真实 root、revision、binary/library hash。
- `ctp-win1`、`ctp-win4`、`ctp-win128` 的共享 artifacts 保持字节级一致。
- 不产生宽泛 exception。

### T36-B: Publish T34 and T35 in the local tracker

在 `tickets.md` 的 T33 与 T36 之间补入：

- Task 34: Lifecycle OFF performance root-cause calibration，blocked by T33，状态 closed。
- Task 35: Module-level lifecycle hotspot calibration，blocked by T34，状态 closed。
- T36 继续 blocked by T35。

只提交长期证据：T34/T35 report、summary、accepted manifest 和能够复现最终结论的 runner。
失败尝试、原始 Instruments trace、XML profile 和逐次运行目录只在 report 中记录结论与排除原因，
不进入 promotion tree。

### T36-C: Commit the post-review CTP invariant

保留当前已经验证的行为边界：

- CTP 一个 transaction segment 对应一个 TP Packet 和一个 TASSN。
- 删除 receiver-side partial assembly，不改变高层 Write/Read segmentation。
- 不改变 CTP builder/parser、TAACK、Jetty inflight、queue、routing、callback、timer 或 trace 顺序。

验收命令：

```bash
ninja -C cmake-cache -j2 unified-bus unified-bus-test test-runner
./build/utils/ns3.44-test-runner --test-name=unified-bus-ctp-transaction
./build/utils/ns3.44-test-runner --test-name=unified-bus-ctp-lifecycle
./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle-storage
./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle-trace
./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle
```

提交边界：

1. implementation/fix commit：source、test、portable aggregate harness。
2. closeout commit：T34/T35 tracker、T36 report/summary、成功 aggregate manifest。

当前用户已有的 `run_p01_detailed_diagnosis.py` 修改和无关未跟踪 evidence 不得混入。

## T37: Resolve DCQCN Idle Semantics

**Blocked by:** T36 完整 closeout。

推荐决策：恢复 `next@cf8ee683` 的完整 idle reset 语义；tracked-event cancellation 继续使用当前
lifecycle helper。若要保留 congestion continuity，必须在实现前升级为显式协议语义变更并由用户确认。

### Batch Reuse Map

Pre-refactor commit: `next@cf8ee683`，历史参考 `main@ccf41b58`

Semantic batch: sender transport idle 后的 DCQCN state reset。

Old active symbols:

- `UbHostDcqcn::OnSenderTransportIdle`
- `UbHostDcqcn::EnsureSenderStartState`
- `UbDcqcnIdleFlowCancelsRecoveryStateTest`

Continuous code blocks reused:

- 从 next 原样回收 `m_alpha`、`m_currentRate`、`m_targetRate`、time/byte recovery counters 和
  `m_bytesSinceLastIncrease` 的 reset block。
- 保留当前 `CancelTrackedEvent` 和 active-flow release 顺序。

Reuse mode: copy-and-adjust。

Required lifecycle adaptations: EventId cancel 改为 typed tracked-event cancel；无其他行为差量。

Preserved ordering and observable behavior: release active-flow → cancel timers/wakeup → reset sender
state/scalars → reset pacing time。下一次 `EnsureSenderStartState` 继续按 active-flow count 选择 line rate
或 initial rate。

Existing characterization: 现有 DCQCN idle、active-flow、pacing、CNP recovery fixtures。

Explicit behavior change: 当前分支的 partial continuity 被移除并恢复 next reset；必须在 closeout
中记录为 current-to-next alignment。

Excluded paths: ECN marking、CNP builder/parser、rate increase algorithm、pacing debt算法、TP destroy。

Focused validation:

```bash
ninja -C cmake-cache -j2 unified-bus unified-bus-test test-runner
./build/utils/ns3.44-test-runner-default --test-name=unified-bus-congestion-control
./build/utils/ns3.44-test-runner-default --test-name=unified-bus-dcqcn-marking
./build/utils/ns3.44-test-runner-default --test-name=unified-bus-lifecycle
```

另外使用现有 PFC/DCQCN 场景对 `next@cf8ee683` 做一次窄 differential；共享协议 artifacts 应字节级
一致。未知差异立即停止。

## T38: Move CTP Transaction State to Its Canonical Owner

**Blocked by:** T37。

目标是纯 code-motion contraction，不改变 CTP runtime 行为。

### Batch Reuse Map

Pre-refactor commit: `main@ccf41b58`；current behavior reference: T36 accepted candidate。

Semantic batch: CTP transaction identity/window/state 的类型所有权。

Old active symbols:

- `UbCtpEntityKey`
- `UbCtpTransactionContext`
- `UbCtpTransactionState`
- `UbTransaction::{GetOrCreate,Find,TryRetire}CtpTransactionState`
- `UbCtpTransportService::GetTransactionState`

Continuous code blocks reused: 类型定义、constructor/window 实现和所有 state mutation 原样移动；先让
Transaction old callers 使用移动后的定义并跑 focused test，再编译 CTP service callers。

Reuse mode: extract/move，不重写。

Required lifecycle adaptations: 无。只解除 `ub-transaction.h -> ub-ctp.h` 的 owner 反向依赖；CTP service
保留 Entity routing、queue、packetization 和 congestion state。

Preserved ordering and observable behavior: context lazy creation、ACK bitmap、pending/outstanding
containers、completion event count、send cursor retirement 全部保持原顺序。

Existing characterization: T36 的 CTP Transaction/lifecycle suites 和 frozen CTP window cases。

Explicit behavior changes: none。

Excluded paths: CTP protocol/header、one-packet invariant、queue retention、routing、Jetty、performance
storage representation。

Focused validation: targeted build、`unified-bus-ctp-transaction`、`unified-bus-ctp-lifecycle`、
`unified-bus-lifecycle`、`git diff --check`。普通 contraction 不重跑大型 baseline。

停止条件：需要新公共 domain type、改变 include 方向以外的 API、或出现任何 packet/task byte difference。

## T39: Narrow the Transaction-to-Function WQE Terminal Seam

**Blocked by:** T38。

该项先做 characterization gate。现有 seam 若已经是最窄且能证明两级权威正确，可通过文档固化而不强行
新增 interface；不得为了“更干净”引入第二套 WQE state machine。

### Batch Reuse Map

Pre-refactor commit: `main@ccf41b58`；architecture contract reference: lifecycle design 的 two-authority
WQE completion section。

Semantic batch: Transaction 产生 typed outcome，Function/JettyManager 执行 WQE terminal 和 upper
completion。

Old active symbols:

- `UbTransaction::WqeFinish`
- `UbFunction::ApplyWqeOutcomeFromTransaction`
- `UbJettyManager::ApplyWqeOutcome`
- `ShouldContinueAcceptedWqes`
- `ShouldForceDrainAcceptedWqes`

Continuous code blocks reused: 原 `WqeFinish` 的 segment terminal、lease release、operation erase、ordered
successor wakeup 和 callback 顺序全部保留。

Reuse mode: direct first；只有 characterization 证明 API 过宽时，才最小抽取一个 typed terminal-policy
snapshot 或 typed callback。

Required lifecycle adaptations: Transaction 只提交 `{jettyNum, wqeId, identity, outcome}`；
JettyManager 验证 generation、删除 WQE record、更新 drain 并最后通知 UbApp。

Preserved ordering and observable behavior: child terminal → operation/WQE ledger terminal → operation
erase → ordered successor scheduling → Function/Jetty terminal → upper callback。任何调整此顺序的方案均停止。

Existing characterization: transaction URMA、same-Jetty progress、ordered WQE、forced drain、reentrant
cleanup 和 lifecycle suites。

Explicit behavior changes: none。

Excluded paths: segmentation、TP cancel algorithm、RTP/CTP completion semantics、Jetty scheduling policy。

Focused validation:

```bash
ninja -C cmake-cache -j2 unified-bus unified-bus-test test-runner
./build/utils/ns3.44-test-runner-default --test-name=unified-bus-transaction-urma
./build/utils/ns3.44-test-runner-default --test-name=unified-bus-transport-pipeline
./build/utils/ns3.44-test-runner-default --test-name=unified-bus-lifecycle
```

## T40: Curate the Promotion Tree and Publish Final Acceptance

**Blocked by:** T37、T38、T39。

### Artifact policy

保留：

- PRD、architecture design、tickets、task brief、report、summary。
- frozen case inputs、comparison rules、exception manifests、最终 aggregate manifest。
- 能复现最终结果的 runner 和最小 command manifest。
- 受控差异的 old/new hash、offset、cause 和 impact summary。

不进入 promotion tree：

- `stdout.txt`、`stderr.txt`、逐次 `exit-code.txt`、`time-l.txt`。
- Instruments `.trace`、`.xml`、`.plist`、bulkstore、index 和压缩 run archive。
- 重复复制的 next/current case run trees。
- 已确认无效、受环境污染或仅用于一次 ablation 的中间结果。

删除前先生成一份 tracked/untracked inventory，逐项标注 keep / summarize / discard；不对无法归属的用户
文件执行删除。

### Final acceptance

更新 `docs/superpowers/specs/2026-07-16-unified-bus-lifecycle-acceptance.md`，绑定最终 HEAD，至少包含：

- targeted build 与 ticket-defined focused suites。
- RTP、CTP、LDST、DCQCN、flow-control、parallel lifecycle 的最终状态。
- 对 `next@cf8ee683` 的 complex CTP/RTP byte result。
- 对 `main@ccf41b58` 的 exact/controlled-difference 分类。
- p01/p02、CTP 2 GiB/8 GiB 和 T34 LDST 性能结果；不得用聚合平均隐藏单模块回归。
- macOS umbrella runner residual 的准确边界，不把未运行或 runner crash 写成 PASS。
- automatic TP cleanup 仍为 excluded/not implemented。

最终 gate：

1. 无未解释的协议 artifact difference。
2. 所有受控差异都绑定 revision/runtime/case/path/hash 和明确原因。
3. lifecycle OFF 不产生 trace evidence，但 correctness ledger 保持有效。
4. 当前 source/test/docs/tracker 均已提交；worktree 中只允许明确排除的本地 evidence。
5. root 完成相对 `next@cf8ee683` 和 `main@ccf41b58` 的最终独立 review。

提交边界：T40 promotion artifact commit + final acceptance closeout commit。

## Approval Boundary

批准本计划后，从 Frontier 0 的 T36 aggregate/root 修复开始。T36 source/test/closeout 完成前不开始
T37；任何未解释字节差异、协议算法变化、公共 owner 模型变化或连续两次同类失败都立即停止并报告。
