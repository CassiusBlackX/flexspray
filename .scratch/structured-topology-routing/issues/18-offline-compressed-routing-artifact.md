# 离线压缩路由工件

状态：已完成

## 目标

让 `ns-3-ub-tools` 在准备阶段直接生成版本化 `compressed_routing.bin`，仿真运行时直接加载
最终 `UbNextHopIndex`，不再输出或解析数百万条 `TABLE` 文本。

## 不变量

- 路由生成使用 `full`、`structured`、`mixed`；运行时使用
  `compressed-table-lookup`、`structured-rule-compute`，两组选择互不混用。
- 一个 route lookup item 只由一种表示负责，不合并不同来源的候选集合。
- shortest/non-shortest 候选、primary-address fallback、入端口过滤、RR/HASH/ADAPTIVE、
  packet spray 和一次绕路语义不变。
- 旧 CSV case 继续可直接运行；加载后使用压缩内存索引。诊断 CSV 只在显式请求时输出。
- 仿真运行时不调用 Python、NetworkX 或离线路由编译器。

## 验收

1. Python 与 C++ 对二进制格式的字段、字节序、checksum 和错误处理有固定 fixture 测试。
2. `full`、`structured`、`mixed` 在支持的组合下生成可复用 prepared case。
3. `--verify-routes` 对完整候选集合逐项验证，普通准备不生成全量验证基准。
4. UBX、1DPOD、N-D mesh 和挑战组合拓扑的候选及 `GetOutPort()` 结果与基线一致。
5. 多种 multipath selector 在相同输入和 RNG 状态下输出字节一致。
6. 与 main、旧全量表、压缩表和结构规则分别比较生成、工件、加载、RSS 和查询性能。
