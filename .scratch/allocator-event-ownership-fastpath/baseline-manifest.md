# Allocator event ownership 基线 manifest

## 目标与 provenance

- 目标分支：`codex/unified-bus-architecture-integration`
- 目标 commit：`2bf02c8c082841381d599a10d6a1e461d4c24ea6`
- 性能工件来源 worktree：
  `/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run`
- 性能工件根目录：
  `/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution`
- provenance：
  `/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution/results/provenance.json`
- source diff SHA-256：
  `7ae2605f29a00475024e703de3dc30279a6c4dd1c17390f6b54c56049498baf1`

诊断 source diff 只用于性能成本上界。`NS3_UB_DIAGNOSTIC_DISABLE_ALLOCATOR_EVENT_OWNERSHIP`
和 `NS3_UB_DIAGNOSTIC_DISABLE_INPORT_EVENT_OWNERSHIP` 会绕过 forced-stop cancellation/residue
所需的 ownership，不是生产配置、默认值或 Ticket 02 的候选实现。

## 五个比较状态

| 中文实验名 | 角色 | 本基线状态 | 工件 |
| --- | --- | --- | --- |
| 主分支基线行为 | baseline | 未测；现有归因包未构建 main | 无，不得推断 |
| next 基线行为 | baseline | 未测；现有归因包未构建 next | 无，不得推断 |
| 当前 PR 完整事件所有权 | control | 已测，3 个 paired samples | `control-complete-event-ownership` |
| 当前 PR 关闭分配器事件所有权 | diagnostic ceiling | 已测，3 个 paired samples | `without-allocator-event-ownership` |
| 当前 PR 输出端口本地事件所有权 | production candidate | 未实现、未测；由 Ticket 02 提供 | 无，不得用诊断 treatment 代替 |

“关闭交换机入口事件所有权”是额外 attribution control，不属于上述五个必选状态；其结果仍保留在
原始工件中，用于证明该 ledger 没有稳定的 `0.5%` 以上 instruction 效应。

## 构建配置与复现命令

原始构建配置：

```text
python3.12 ./ns3 configure --enable-modules=unified-bus --disable-examples --enable-tests \
  --disable-mpi --enable-mtp --disable-werror -d release -G Ninja
python3.12 ./ns3 build -j 8 ub-quick-example
ninja -C cmake-cache test-runner
```

归因 runner 的 cwd：

```text
/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution
```

可重复命令：

```text
python3.12 scripts/run_experiment.py validate
python3.12 scripts/run_experiment.py build-and-test
python3.12 scripts/run_experiment.py run-canaries
python3.12 scripts/run_experiment.py run-measured
```

执行约束为单 build、所有 case 串行、已有输出拒绝覆盖。原始命令和退出状态见：

```text
/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution/command-manifest.yaml
/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution/results/build/focused-tests.json
/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution/results/run-ledger-events.jsonl
```

## 样本顺序

canary 顺序固定为：完整事件所有权、关闭分配器事件所有权、关闭交换机入口事件所有权。

三轮 measured 使用 counterbalanced 顺序：

1. 完整事件所有权、关闭分配器事件所有权、关闭交换机入口事件所有权。
2. 关闭交换机入口事件所有权、关闭分配器事件所有权、完整事件所有权。
3. 关闭分配器事件所有权、完整事件所有权、关闭交换机入口事件所有权。

主 workload 的 control wall time 为 `28.18 / 31.01 / 33.42 s`；diagnostic allocator
treatment 为 `24.27 / 26.74 / 23.90 s`。该 workload 满足 20-30 秒主测量目标的主体区间，
第三个 control sample 为 `33.42 s`，必须保留可见，不得删作 outlier。

## 指标基线

| 中文实验名 | retired instructions | peak RSS bytes | wall seconds |
| --- | --- | --- | --- |
| 当前 PR 完整事件所有权 | `450821979924 / 449879258334 / 450591249594` | `467402752 / 466960384 / 467599360` | `28.18 / 31.01 / 33.42` |
| 当前 PR 关闭分配器事件所有权 | `349164755237 / 348414525866 / 348911774553` | `467288064 / 466665472 / 466485248` | `24.27 / 26.74 / 23.90` |

关闭 allocator ownership 相对完整 ownership 的 paired retired-instruction delta 为
`-22.5493% / -22.5538% / -22.5658%`，中位数 `-22.5538%`。RSS paired delta 为
`-0.0245% / -0.0632% / -0.2383%`。wall 仅报告，不覆盖 instruction 证据。

完整逐轮 cycles、`Simulator::Run()`、user time 和 bootstrap interval 见：

```text
/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution/results/summary.json
```

retired instructions 与 cycles 由 macOS `/usr/bin/time -l` 采集；RSS 同时保留
`maximum resident set size` 和 runner 的 sampled peak。逐样本 `time-l.txt`、`result.json` 位于：

```text
/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution/results/<case-id>/measured-<1|2|3>/
```

## Canonical gate 与失败可见性

canonical gate 使用 canary 的 12,288 个事件，对比 identity multiset 和 timestamp。三个已测
状态均满足：事件数 `12288`、identity multiset 相同、changed timestamp count 为 `0`、所有
timestamp delta 为 `0 ns`。权威结果位于：

```text
/Users/ytxing/workspace/ns-3-ub-pr50-event-ownership-run/scratch/20260725-event-ownership-attribution/results/canonical-comparison.json
```

任何以下情况必须作为失败保留在 run ledger 和报告中，不得跳过、覆盖或只汇报成功样本：

- 非零 return code、signal、timeout、canary 或 measured 缺样本；
- canonical event count、identity multiset 或 timestamp 不一致且没有预声明解释；
- `stop_reason` 不是 `completed`，wall/RSS safety stop，或残留仿真进程；
- 相同 RSS 增长连续超过完整 ownership 的 `2%`；
- allocator-dense workload 的 candidate 未恢复完整与 disabled gap 的至少 `80%`；
- control workload 相对完整 ownership 出现稳定超过 `2%` 的 instruction 回归。

本基线的 canary、9 个 measured runs、build 和三个 focused suites 均为 return code `0`，
`stop_reason=completed`；没有 wall/RSS safety stop 或残留进程。未来 main、next 和输出端口本地
candidate 的缺失样本当前保持“未测”，不计为通过。

## 行为 oracle

focused suite `unified-bus-switch-voq` 锁定以下 ownership：

- RR 和 DWRR 对同一 outPort 的两个 pending retrigger 分别保留 handle；任一个存在时，
  `HasPendingOrActiveWork()` 必须阻塞该 outPort 的 reclaim。
- RR 初始化还持有一个 deadlock event，因此该场景 pending/residue 为 `3`；DWRR 当前初始化不调度
  deadlock event，因此 pending/residue 为 `2`。该差异是现有 oracle，不在 Ticket 01 中修正。
- callback 入口先清理当前 resolved handle，但同一 outPort 的 pending peer 仍阻塞 reclaim；
  最后一个 callback 进入后不再残留 scheduling-work gate。
- compatibility mode 的 scheduled port notification 必须有独立 ownership，完成后被 prune，
  且它本身不属于 reclaim-blocking scheduling work。
- forced stop 精确报告并取消 allocation、两个 retrigger、port notification 和 deadlock 共
  5 个 allocator events；取消后 pending count 为 0。

上述精确计数和 pending-peer 断言会在删除 retrigger、notification、allocation 或 deadlock 的
必要 ownership 时失败。诊断开关同时绕过多种 ownership，未在当前生产 worktree 中制造负例，
因此不声称执行过 mutation test；证据限于断言结构和完整 ownership 的 focused PASS。
