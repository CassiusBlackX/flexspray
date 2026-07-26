状态: 可执行
阻塞于: 06, 07

# 完成独立 review 与父级任务包交接

## 目标

在稳定 commit snapshot 上完成 Standards 与 Spec 两轴 review，并把结果接回 architecture
integration PR 的最终 compatibility review。

## 验收

- review 固定 base/head、diff hash、生产文件和证据工件。
- design 中的 invariants、正确性和性能 gate 逐项有证据或明确未完成。
- 诊断开关、实验输出和本机绝对路径不进入生产接口或默认配置。
- 父级任务 05 读取本任务包的最终报告后才能宣布 PR ready。

## 讨论

本任务不负责合并 PR；它只给出 ready、needs-fix 或 inconclusive 结论。
