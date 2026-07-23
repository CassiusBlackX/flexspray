# ns-3-ub-tools v2 任务

| 编号 | 任务 | 状态 | 阻塞于 |
| --- | --- | --- | --- |
| 01 | 交付 AllReduce Ring 纵向闭环 | 已完成 | - |
| 02 | 扩展现有 collective 与 rank placement | 已完成 | 01 |
| 03 | 实现独立 ReduceScatter 和 AllGather | 已完成 | 02 |
| 04 | 接入通用 AllToAllV | 已完成 | 02 |
| 05 | 接入 SimAI traffic 转换 | 已完成 | 01 |
| 06 | 接入 topology build/check/plot | 已完成 | 01 |
| 07 | 接入 case show/check | 已完成 | 06 |
| 08 | 接入 trace parse | 已完成 | 01 |
| 09 | 完成文档、旧脚本回归与端到端验收 | 已完成 | 03, 04, 05, 06, 07, 08 |

任务 01 必须交付一个可运行的 `traffic allreduce --algorithm ring`，不能只提交空命令树。后续
每张票都增加一个可以单独演示和验证的用户能力。任务 03、04、05、06、08 在各自前置完成后
可以独立实施，但同一 tools 工作树遵守单一 writer。任务 09 只做整体收口，不能把前面任务
应有的单元测试和接口文档推迟到最后。
