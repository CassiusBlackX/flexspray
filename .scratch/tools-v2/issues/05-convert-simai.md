状态: 已完成
阻塞于: 01

# 接入 SimAI traffic 转换

## 目标

通过 `traffic convert-simai` 接入现有 SimAI flow 转换能力，并复用 v2 的输出、错误和帮助
约定。

## 范围

- 输入 SimAI flow JSONL、可选 rank placement 文件和输出目录。
- 复用现有 converter API，不通过 subprocess 包装旧脚本。
- 保留 collective overlap、操作类型和时间字段的现有转换语义。
- 文档明确转换是导入已有 flow，不是重新选择 collective algorithm。

## 不变量

- 旧 `simai_flow_to_traffic.py` 继续独立可用。
- 不从 stdout 猜测输入或输出路径。
- 不改变 SimAI flow 的依赖关系。

## 验收

- 代表性 JSONL 在 v1 脚本和 v2 中产生语义等价的 traffic.csv。
- rank placement、非法节点和 overlap 选项有测试。
- 输入、输出路径在帮助中明确标识。

## 验证结果

- tools commit: `f630ffd feat(cli): add SimAI flow conversion`。
- v2 直接调用现有 converter API；代表性 JSONL 与 v1 输出逐字节一致。
- rank map、host 边界和 collective overlap 有测试。
