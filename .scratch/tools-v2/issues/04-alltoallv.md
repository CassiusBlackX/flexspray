状态: 已完成
阻塞于: 02

# 接入通用 AllToAllV

## 目标

把现有 pairwisev Schedule 接入 v2，并提供 matrix、uniform 和 random 三种明确、确定的流量
分布输入。

## 范围

- matrix 使用按字节计数的方阵。
- uniform 按稳定规则分配 data-size 和余数字节。
- random 使用显式 seed 和固定算法生成矩阵。
- 从 byte matrix 推导 counts、displacements 和 remote offsets。
- `include-local` 默认关闭。

## 不变量

- matrix 模式不要求也不使用 data-size。
- uniform/random 每个源 rank 的行和严格等于 data-size。
- 不把旧 MoE maker 的模型参数混入通用分布。

## 验收

- 矩阵维度、负值、非整数和不一致收发关系有失败测试。
- 相同 seed 产生字节完全一致的结果。
- Schedule offset 和传输字节与输入矩阵逐项对齐。
- v2 输出可由现有 case loader 读取。

## 验证结果

- tools commit: `12e34c4 feat(cli): add alltoallv distributions`。
- matrix、uniform、random 三种输入通过；random 相同 seed 字节一致。
- 非整除字节、矩阵校验、send/recv displacement 与 remote offset 有逐项测试。
