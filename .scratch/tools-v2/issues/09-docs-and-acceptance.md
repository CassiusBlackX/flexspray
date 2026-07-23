状态: 已完成
阻塞于: 03, 04, 05, 06, 07, 08

# 完成文档、旧脚本回归与端到端验收

## 目标

完成 tools v2 用户文档、迁移说明、旧脚本回归和跨命令端到端验收，确认统一入口可以替代
结构化开发期间的中间 CLI。

## 范围

- 更新 tools README、traffic README 和 scratch 用户文档。
- 更新 workload-options、topology-options、throughput-evidence 和 spec-to-toolchain 等 reference。
- 文档分别给出 v2 主路径和“需要旧行为时直接运行旧脚本”的路径。
- 验证 topology build -> traffic generation -> case check 的准备闭环。
- 验证 prepared case 仍由独立 scratch runner 执行。

## 不变量

- 不删除 v1 脚本。
- 不增加 v1 参数兼容层。
- 不把 tools 与 ns-3 合成一次性启动命令。

## 验收

- 所有 CLI 和算法测试通过。
- v1 代表性脚本独立 smoke 通过。
- structured 和 CSV case 的准备、检查及运行 smoke 通过。
- README 的命令在干净环境中实际执行过。
- CLI 参数或输出 schema 变化与 OpenUSim reference 在同一提交更新。
- 记录已执行命令、结果、已知限制和后续扩展边界。

## 验证结果

- tools final commit: `72d426b fix(cli): align v2 workflow with specification`。
- tools `unittest discover`: 135 tests passed; v1 representative traffic and trace fixtures pass.
- parent `tests.openusim_skills.test_skill_docs`: 19 tests passed。
- Real prepared flow passed: `topology check` -> `topology build` -> `traffic allreduce` -> `case check`。
- The flow uses files between tools and scratch; no stdin protocol, Python runtime import, or one-shot
  simulator start was added.
- Known boundary: v2 topology build writes structured artifacts by default; explicit CSV generation and
  v1 timestamped scripts remain available as separate paths.
