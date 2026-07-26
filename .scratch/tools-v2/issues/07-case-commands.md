状态: 已完成
阻塞于: 06

# 接入 case show/check

## 目标

让用户在不启动仿真的情况下查看 case 摘要并发现输入错误。

## 范围

- case show 汇总格式、节点、链路、traffic 和路由工件信息。
- case check 校验必需文件、版本、引用和可加载性。
- CSV case 和 structured case 使用各自 loader，但返回统一检查结果。
- 提供人类可读输出，并预留统一 JSON schema。

## 不变量

- check 不运行 ns-3 或重新编译结构路由。
- 缺少可选 runlog 不属于 case 输入格式错误。
- 检查结果不修改 case 文件。

## 验收

- CSV case 和 structured case 均可 show/check。
- 损坏版本、缺失引用和非法工件给出文件及修复建议。
- 只读检查前后 case 内容哈希不变。

## 验证结果

- tools commit: `460b44d feat(cli): add case show and check`。
- CSV 与 structured case 均可 show/check；structured binary 校验 fingerprint 和 checksum。
- 检查前后 case 文件逐字节不变。
