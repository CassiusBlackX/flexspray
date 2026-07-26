状态: 已完成
阻塞于: 01

# 扩展现有 collective 与 rank placement

## 目标

在第一个纵向闭环上接入现有 AllReduce 和 AllToAll 算法，并支持交错分组和显式 rank
placement。

## 范围

- AllReduce: nhr、recursive-halving-doubling。
- AllToAll: pairwise、scatter。
- 实现 `group-layout=interleaved` 和 `placement-file`。
- scatter 使用 `peers-per-phase`，不在 v2 暴露 `scatter-k`。
- 旧 `build_traffic.py` 与 v2 复用内部 builder 和 writer API。

## 不变量

- `group-layout` 与 `placement-file` 互斥。
- v2 使用 `group-size` 和 `groupId`，不借用 UB domain 表示 collective 分组。
- `peers-per-phase` 只改变 phase 并发，不改变逻辑字节总量。
- 不把旧 `-n/-c/-b/-a` 参数加到 v2。

## 验收

- 自动分组和显式 placement 均有正反测试。
- 所有新增算法逐 phase、逐 transfer 与既有实现对齐。
- 不可整除的数据切分保持字节守恒。
- 旧 build_traffic.py 的代表性命令输出行为通过回归测试。

## 验证结果

- tools commit: `f69a67c feat(cli): add existing collective schedules`。
- 新增/累计 CLI 测试 25 项通过。
- tools 全量 `unittest discover` 105 项通过。
- 旧 `build_traffic.py` 对 `3 ranks / 7B` 的 pairwise、scatter、RHD CSV 逐字节回归通过。
- review 后修复：v1 非整除与 inactive 负索引行为、单一 registry、placement 跨组 node 复用和
  unique host-count 语义。
