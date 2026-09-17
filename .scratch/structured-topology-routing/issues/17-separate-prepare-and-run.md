状态: 已完成
阻塞于: 16

# 分离结构化 case 准备与运行

`ns-3-ub-tools` 只负责构造物理图、识别验证结构、编译路由并原子写入 case 目录。
`scratch/run_structured_topo_case.py` 只负责读取已准备的 case 目录并启动 ns-3。

## 工件

- `structured_topology.txt`：带版本的正式拓扑与路由输入。
- `structured_summary.json`：编译器版本、协议版本、缓存键、覆盖率和条目计数。
- topology/routing CSV 仅在显式诊断导出时生成，runner 不读取这些 CSV。

## 行为边界

- tools 不启动 ns-3，不持有运行中的子进程，也不通过 stdin 传递完整配置。
- runner 不导入 NetworkX，不识别结构，不编译路由。
- Unified Bus 仿真运行时仍为纯 C++；只把初始化来源从 stdin 改为显式文件。
- 现有纯 CSV case 保持独立路径。
- exact 目的端口查询不隐式回退；只有原本允许主地址回退的候选查询路径才回退。

## 验收

- 准备命令退出后，目录可以被独立重复运行。
- Python 按行写协议文件，不构造完整 TABLE 文本字符串。
- 准备过程不覆盖已有 `traffic.csv` 和 `network_attribute.txt`。
- 两阶段 smoke、Python 单元测试和 C++ structured-routing 测试通过。

## 验证结果

- Python 全量工具测试：71 项通过；Python builder 和 YAML 准备入口均有直接测试。
- C++ `unified-bus-structured-routing` 专项测试通过。
- 两阶段 smoke 通过：prepare 写入目录后，由独立 runner 完成 4 个任务，退出码为 0。
- `git diff --check` 通过。
