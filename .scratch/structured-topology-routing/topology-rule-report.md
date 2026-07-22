# 拓扑规则直接生成报告

## 结论

完整验证且满足安全组合条件的结构化拓扑现在直接生成坐标/分组规则，不再为每个目的节点
构造最短路 DAG，也不生成完整路由表。当前直接规则覆盖：

- Ring、Mesh、Torus 和 ND FullMesh；
- UBX 的维度直连与端口扩展交换网络组合；
- 独立多平面两级交换网络；
- 1DPOD 中同一平面内“主机到 leaf、leaf 到本地 upper、leaf 到共享 upper”的分层网络；
- 经典 Dragonfly；
- 上述局部 FullMesh 与交换网络的已验证安全组合。

结构不完整、结构重叠可能产生更短混合路径，或含有未知连接时，`mixed` 不采用拓扑规则，
继续使用 exact/range 规则或表项。自定义 path finder 仍保留完整基准验证路径。

## 1024-NPU 1DPOD

实图包含 2,336 个节点和 19,968 条物理链路。识别结果为：

- 128 个组内 FullMesh；
- 8 个带本地和共享 upper 的分层交换网络；
- 136 条拓扑规则；
- 10,528 条节点到规则的成员映射；
- 物理链路零遗漏、零重复、零拒绝结构。

逐目的流式暴力求解与规则解释器完成全部查询对齐：

```text
brute-force lookups: 9,427,968
topology-rule lookups: 9,427,968
missing: 0
extra: 0
mismatched candidates or metrics: 0
```

流式比较一次只保留一个目的节点的旧算法结果，因此不会为了验证而常驻 940 万项字典。

## 离线生成

下表时间不包含 `tracemalloc`，均为同机冷进程单次结果。暴力路径生成全部查询项；1024-NPU
暴力结果逐目的释放，但协议字节按完整表精确累计。规则路径完成物理图加载、结构发现、验证
和规则编译，不展开查询项。

| 拓扑 | 暴力查询项 | 暴力生成 | 规则生成 | 加速 | 完整表协议体 | 规则协议体 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| UBX 16-NPU | 880 | 2.83 ms | 1.79 ms | 1.6x | 13,721 B | 3,030 B |
| 1DPOD 64-NPU | 36,288 | 411.23 ms | 22.51 ms | 18.3x | 611,134 B | 44,689 B |
| 1DPOD 1024-NPU | 9,427,968 | 17.98 s | 1.37 s | 13.1x | 162,121,150 B | 789,816 B |
| ND FullMesh 4x4x4 | 9,216 | 66.97 ms | 3.69 ms | 18.2x | 144,551 B | 9,100 B |
| Dragonfly a=4 h=1 | 780 | 3.36 ms | 1.15 ms | 2.9x | 12,805 B | 3,110 B |

1024-NPU 的完整输入协议体减少约 99.51%，离线生成快约 13.1 倍。带 `tracemalloc` 的规则
编译峰值为 32,594,289 B；暴力路径的 6,393,657 B 只是流式验证峰值，不能代表完整表常驻
内存。完整表仅协议文本已经是 162 MB，因此运行时路由内存收益是确定的，但仍应通过独立
仿真进程 RSS 或 C++ 分配统计给出最终常驻内存数字。

## 在线查找

C++ release 动态库、500 万次查询的单次微基准结果：

| 表示 | 单次查找 |
| --- | ---: |
| table | 4.50 ns |
| exact rule | 5.27 ns |
| range rule | 5.84 ns |
| Ring topology rule | 36.63 ns |
| Torus topology rule | 81.26 ns |
| UBX topology rule | 96.14 ns |
| 1DPOD topology rule | 139.73 ns |
| ND FullMesh topology rule | 148.13 ns |
| Dragonfly topology rule | 172.23 ns |

因此不能声称在线查找更快。拓扑规则在运行时计算共同结构、距离和合法物理下一跳，当前比
哈希表查找慢约 8 到 38 倍，但绝对值低于 0.2 微秒。主要收益是离线生成、输入体量和常驻
路由内存；后续在线优化应预计算节点的规则成员、角色、附着点和常用邻接索引，而不是恢复
逐目的表。

## 验证

- Python 工具测试 67 项通过；
- C++ `unified-bus-structured-routing` 通过，包含分层共享 upper 和 Dragonfly 规则测试；
- UBX 880/880、1DPOD 64-NPU 36,288/36,288、1DPOD 1024-NPU
  9,427,968/9,427,968、ND FullMesh 9,216/9,216、Dragonfly 780/780 全部逐项一致；
- C++ 微基准只用于比较数量级，不设置易受机器负载影响的性能断言。

原始机器可读结果见 `topology-rule-benchmark.json`，离线基准脚本见
`benchmark_topology_rules.py`；在线结果见 `rule-lookup-benchmark.json`，微基准源码见
`benchmark_rule_lookup.cc`。
