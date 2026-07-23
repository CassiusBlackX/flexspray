状态: 已完成
阻塞于: 01

# 接入 topology build/check/plot

## 目标

用同一个 source loader 接入 Python builder、JSON/YAML 和现有 CSV case，并在其上实现 build、
check 和 plot。

## 范围

- 支持 `path.py` 和 `path.py:function`。
- 强制 Python builder 返回带类型边界的 `NetworkSimulationGraph`。
- 复用 structured_case、case_topology_loader 和现有 plot 能力。
- structured 为默认 case-format，CSV 只在显式请求时导出。

## 不变量

- tools 只准备 case，不启动 ns-3。
- check 和 plot 不复制输入解析或结构发现。
- 默认路径不展开大型 CSV 中间文件。
- 已准备工件继续由 scratch runner 独立读取。

## 验收

- 四类 SOURCE 均有加载和错误测试。
- build 生成可重复运行的 prepared structured case。
- check 不写正式 case 工件。
- plot 使用与 build 相同的规范化物理图。

## 验证结果

- tools commit: `da5f095 feat(cli): add topology build check and plot`。
- Python、JSON、YAML、CSV case 四种 source 汇总为同一个 `NetworkSimulationGraph` 边界。
- build/check/plot 共用 loader；check 不写 case，plot 不生成 CSV 中间文件。
