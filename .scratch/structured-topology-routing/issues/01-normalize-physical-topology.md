状态: 已完成

# 规范化物理拓扑图

在 `build_graph_config()` 完成端口分配后，生成保留节点、独立链路、两端端口、并行链路和
路由相关属性的内部物理拓扑图。不得改变现有 `NetworkSimulationGraph` 公开 API。

验收：相同拓扑在打乱节点 ID 和边顺序后仍保留正确物理映射；并行链路不会折叠。

## 验证结果

- `test_physical_topology.py`: 2 个测试通过。
- `python -m unittest discover -s scratch/ns-3-ub-tools -p 'test_*.py'`: 10 个测试通过。
- 并行压缩路由的多进程 pickle 路径已验证，规范化对象不会破坏现有 worker。
- OpenUSim 文档测试中与本任务相关的引用通过；唯一失败来自未跟踪的
  `openurma-path-latency-calibration.md` 缺少 Contents，不属于本任务改动。
