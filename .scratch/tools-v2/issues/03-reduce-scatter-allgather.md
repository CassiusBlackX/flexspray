状态: 已完成
阻塞于: 02

# 实现独立 ReduceScatter 和 AllGather

## 目标

把现有 AllReduce 中的 ReduceScatter 和 AllGather 阶段提取为正式独立 Schedule builder，再
开放对应 CLI。

## 范围

- ReduceScatter: ring、nhr、recursive-halving。
- AllGather: ring、nhr、recursive-doubling。
- AllGather 的 data-size 是完整聚合结果，不是单 rank 输入 shard。
- AllReduce 可组合复用这两个阶段，但不得改变现有算法行为。

## 不变量

- 不能通过截取打印文本或 CSV 行冒充独立 builder。
- 不可整除时保持字节守恒和稳定切分。
- 组合后的 AllReduce Schedule 与任务 02 的基准一致。

## 验收

- 独立 builder 对 1、2、非 2 次幂和不可整除 data-size 有测试。
- ReduceScatter 输出分片总和与输入完整数据一致。
- AllGather 输入分片最终组成 data-size 指定的完整结果。
- AllReduce 组合回归逐 phase 对齐。

## 验证结果

- tools commit: `6b5d7f3 feat(cli): add reduce-scatter and allgather`。
- 三种 ReduceScatter 与三种 AllGather 逐 phase 对齐对应 AllReduce 分段。
- 新算法仅由 v2 registry 暴露，v1 CLI 拒绝 `rs_*`/`ag_*` 名称。
