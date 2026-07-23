状态: 已完成
阻塞于: -

# 交付 AllReduce Ring 纵向闭环

## 目标

交付第一个真实可用的 v2 命令：

```bash
python3 ub_tools.py traffic allreduce \
  --host-count 8 \
  --data-size 1GB \
  --algorithm ring \
  --output-dir ./output
```

## 范围

- 建立 `ub_tools.py`、版本输出、traffic 命令和最小 registry。
- 实现 host-count、group-size、data-size、contiguous group-layout、phase-delay 和 output-dir。
- 复用 Ring Schedule，通过独立 writer 直接生成 `output/traffic.csv`。
- 帮助显示参数含义、默认值、示例和错误修复建议。
- 公共请求对象不依赖 argparse Namespace。

## 不变量

- 不修改旧脚本的参数和默认目录。
- 不通过 subprocess 转发旧脚本。
- data-size 是完整 AllReduce 数据对象，不是单 phase 或累计网络流量。
- tools 不启动 ns-3。

## 验收

- 示例命令生成可由现有 case loader 读取的 traffic.csv。
- 逐 phase、逐 source/destination/bytes 与现有 Ring 算法对齐。
- 单位解析、分组、依赖和错误信息有测试。
- 顶层、traffic 和 allreduce 的 `--help` 可运行。

## 验证结果

- tools commit: `645a10c feat(cli): add AllReduce Ring v2 entrypoint`。
- 新增 CLI 测试 11 项通过。
- tools 全量 `unittest discover` 91 项通过，NetworkX 使用 requirements 锁定的 `3.6.1`。
- 真实 4-host、2-rank group、Ring、`1KB`、`5us` CLI smoke 生成可读 `traffic.csv`。
- Standards/Spec 双轴 review 完成；已修复非正计数、首 phase delay、缺失命令帮助、命名和
  README 同步问题。
