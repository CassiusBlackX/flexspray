状态: 已完成
阻塞于: 01

# 接入 trace parse

## 目标

通过 `trace parse` 接入现有 trace parser，同时保持统计定义和旧脚本行为不变。

## 范围

- 把 parser 主流程提取成可调用 API。
- v2 显式区分 case/runlog 输入和分析输出目录。
- 复用统一帮助、错误和可选 JSON 结果约定。
- 旧 `trace_analysis/parse_trace.py` 继续独立解析自己的参数。

## 不变量

- 不改变 trace 字段、时间单位和统计公式。
- 不通过 subprocess 包装旧脚本。
- 不要求结构化 topology 才能解析 trace。

## 验收

- 同一 fixture 的 v1 与 v2 解析结果逐字段一致。
- 缺失 trace、损坏行和输出冲突给出具体错误。
- CLI 参数变化同步更新 trace reference 文档。

## 验证结果

- tools commit: `3a7a1c2 feat(cli): add trace parse command`。
- v2 直接调用 task/port trace API，与旧 parser 的两个输出逐字节一致。
- 缺失输入和输出冲突有明确错误，`--overwrite` 才允许替换结果。
