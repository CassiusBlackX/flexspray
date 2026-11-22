# Benchmark v1（第十二轮）：参数与出处

**适用范围**：负载生成器 `flexspray_bench.py`，场景 `b_ring` / `b_moe` / `b_kv` / `b_mix`；拓扑仍沿用 `flexspray_case.py` 的 4 台服务器。

**出处标注**：
- **原文**：本轮读过原始文献的相应章节；
- **二手**：只读到镜像、摘要或第三方转述；
- **推导**：由原文数字计算得到，写明公式；
- **假设**：没有公开来源，需要做敏感性分析。

**结论范围**：在本负载上得到的结论只适用于本负载，**不推广到"真实场景"**（CLAUDE.md §3）。

## 1. 拓扑（未改动，第十三轮以后再做 CloudMatrix 化）

| 参数 | 值 | 出处 |
|---|---|---|
| 服务器数 | 4 | 假设 |
| 每台服务器的设备 | 8 NPU（8 平面）、2 CPU（4 平面）、1 NIC（2 平面）、1 SSD（2 平面） | 假设：异构平面数是题目要求，具体取值没有来源 |
| 端口速率 | 400 Gbps，每端口 4 lane | 假设 |
| 每平面 L1 / spine，每对 L1-spine 的链路数 | 每服务器每平面一个 L1，4 个 spine，每对 3 条链路 | 假设 |
| 对照：CloudMatrix384 | 每节点 7 个 L1、7 个 L2 子平面等 | **未读原文**，本轮未采用（独立评审转述的数字不作依据） |

## 2. ring 集合通信（`b_ring`；`b_mix` 中的数据并行 ring）

| 参数 | 值 | 出处 |
|---|---|---|
| allreduce 的步数 | 2(n − 1)：reduce-scatter n − 1 步，allgather n − 1 步 | **推导**。nccl-tests `doc/PERFORMANCE.md`（"Bus Bandwidth"一节，原文）给出 AllReduce 需要 2(n − 1) 次数据传输、因子 2(n − 1)/n；ReduceScatter 与 AllGather 的因子为 (n − 1)/n。逐步的 ring 结构由此推出 |
| 每步每个 rank 的发送量 | S/n，发给环上的后继 | 推导（同上） |
| S（allreduce 大小） | 32 MiB；`b_mix` 中为 8 MiB | 假设；用 `--scale` 做敏感性分析 |
| rank 的顺序 | 按服务器优先：服务器内相邻，跨服务器 4 处 | 假设（与 NCCL 节点内优先的拓扑感知环一致，但 NCCL 的具体选环规则本轮未读） |
| 每步是一个全局屏障 | 所有 rank 的第 k 步完成后，才开始第 k + 1 步 | **假设**。NCCL 的实际实现按块流水、只依赖邻居，屏障比实际更严格 |

## 3. MoE dispatch / combine（`b_moe`；`b_mix` 中的 EP24）

| 参数 | 值 | 出处 |
|---|---|---|
| hidden | 7168 | **原文**：DeepSeek-V3 技术报告（arXiv 2412.19437v2）§4.2 |
| 每层的路由专家 / 共享专家 | 256 / 1 | 原文 §4.2 |
| 每 token 激活的专家数 | 8 | 原文 §4.2 |
| 每 token 最多到达的节点数 | 4 | 原文 §4.2、§3.2.2。本拓扑只有 4 台服务器，这条限制不起作用 |
| dispatch 精度 | FP8（每元素 1 B） | 原文 §3.3.3 |
| combine 精度 | BF16（每元素 2 B） | 原文 §3.3.3 |
| dispatch 的缩放因子 | 每 token 7168/128 × 4 B = 224 B | **二手 + 推导**：1×128 分块量化的说法见 §3.3 的相关内容，本轮只读了 §3.3.3 |
| 每 rank 的 token 数 | 128 | **二手**：DeepEP README 早期版本的低时延测试设置（第三方镜像中读到："128 tokens per batch"），当前上游 README 已不含此句。DeepSeek-V3 §3.4.2（原文）："the batch size per expert is relatively small (usually within 256 tokens)" |
| EP 规模 | 32（`b_mix` 中为 24） | 假设，取本拓扑的 NPU 数。原文的部署是 prefill EP32（§3.4.1）、decode EP320（§3.4.2） |
| 每 rank 的专家数 | 256 / EP | 推导 |
| 路由分布 | 每 token 均匀随机选 8 个不同专家，按种子 | 假设（真实负载有不均衡，DeepSeek-V3 用无辅助损失的均衡，本轮未建模） |
| 每 (token, 目的 rank) 只发一份 | 是 | 二手（DeepEP 按 rank 分发的描述来自转述） |
| 模拟的层数 | 6 层（原模型 58 个 MoE 层） | 假设：58 = 61 − 3，由原文 §4.2"除前三层外都是 MoE 层"推导 |
| 专家计算 / attention 计算时间 | 各 10 µs | 假设 |

## 4. KVCache（`b_kv`；`b_mix` 中由 NPU6、NPU7 预填充）

| 参数 | 值 | 出处 |
|---|---|---|
| 请求序列 | Mooncake 公开 trace `FAST25-release/arxiv-trace/mooncake_trace.jsonl`（sha256 前缀 b434f1816a707f4b），取前 48 条 | **原文**：Mooncake（FAST'25，arXiv 2407.00079）§4：23 608 条，字段为 timestamp / input_length / output_length / hash_ids。本地核对：23 608 条，平均输入 8590、输出 182，时间 0–3 600 000 ms，与 §4.1–4.2 一致 |
| 块大小 | 512 tokens | 原文 §4.1 |
| 命中判定 | hash_id 在 trace 中更早出现过，即视为已缓存（无限容量） | 推导。原文 §4.2 表 1：无限容量的 LRU 命中率为 0.55 |
| 存储热点 | 块 0 被 10 938 / 23 608 条请求引用；前 48 条中命中的块按 id mod 4 放在 4 个 SSD 上，因此块 0 所在的 SSD 是热点 | 推导（trace 统计）。放置规则是假设 |
| 每块字节数 | 64 KiB | **假设，缩放**。LLaMA2-70B 每 token 的 KV 约 320 KiB（80 层 × 8 KV 头 × 128 维 × 2（K、V）× 2 B），每块约 160 MiB：模型结构来自转述（二手），在本仿真的时间尺度上不可行，所以等比缩放 |
| 时间压缩 | trace 中一批请求（3052 ms）对应 100 µs | 假设：前 48 条只出现在 0 / 3052 / 6105 ms 三个时刻 |
| 预填充计算时间 | 20 µs | 假设 |
| 读写方向 | 命中的块：NPU 从 SSD READ；新块：预填充后 WRITE 到 SSD | 推导（原文 §3 的 KVCache 池化语义），具体映射是假设 |
| 测试床带宽（参考） | 节点间 RDMA 最高 800 Gbps | 原文 §8.1，本拓扑未采用 |

## 5. 控制小包

| 参数 | 值 | 出处 |
|---|---|---|
| 大小 | 64 / 256 / 1024 B | 假设 |
| 频率 | 每节点 16 个，每 10 µs 一个，带随机偏移 | 假设 |
| 优先级 | 控制优先级（VL 2） | 假设 |

## 6. 故障

- 故障时刻 50 µs：在各场景的第 3–12 个阶段之间，属于阶段中途。
- 降 lane 档位 x2 / x1，近端为 NPU(0, 0)，远端为 NPU(1, 0)，平面 3。
- 定义沿用 `flexspray_case.py`。

## 7. 需要做的敏感性分析（列入 TODO）

- ring 的 S、MoE 的每 rank token 数、计算时间、KV 每块字节数与时间压缩：分别扫描 ×0.5 / ×2；
- ring 的全局屏障与按邻居依赖两种模式的对比。

## 8. 第十三轮补充

- **跨服务器计算设备 ↔ IO 设备的 P2P**（`flexspray_case.py` 的 d2io / d2ioread）：服务器 s 的 NPU0 ↔ 服务器 s+1 的 SSD，NPU1 ↔ NIC；每个 IO 设备独占一对，需求取 IO 设备 2 个平面的线速；写方向数据为 NPU → IO，读方向为 IO → NPU。**假设**（覆盖要求 1 中"计算设备与网络或存储设备间"的 P2P）。
- **基础时延 α**（用于计入时延的上界）：本仿真拓扑上实测（服务器内 0.37 µs、跨服务器 1.35 µs），属于**推导**自本模型的参数（20 ns 主机链路、100 ns fabric 链路、100 ns 交换转发），这些链路与转发参数本身是**假设**。
- **SSD / NIC 端口的 lane 档位**：x3 / x2 / x1，与 NPU 端口相同（**假设**：4-lane 端口）。

---

## 9. 第十六轮补充：公开超节点拓扑与开源 AI 工作负载套件（Stage 2）

为使评估脱离单一合成 Pod 假设、对齐顶级会议审稿标准，Round 16（提交 `f2676403`）新增 **3 套公开文献超节点拓扑**（[flexspray_topo_public.py](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/flexspray_topo_public.py)）与 **5 套开源 AI 基准负载**（[flexspray_aicb.py](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/flexspray_aicb.py)）：

### 9.1 公开超节点拓扑（`400 Gbps × 4 lanes`，`host_link_ns=20`，`switch_fwd_ns=100`）

| 拓扑标识 | 文献来源 | 节点配置（每 Pod） | 平面 / 交换层结构 | 建模假设与局限 |
|---|---|---|---|---|
| `cm_pod` | `arXiv:2506.12708`（CloudMatrix384 §3.3，1/12 缩比） | `4` 服务器 × (`8` Ascend 910 NPU + `4` CPU + `1` NIC + `1` SSD) = `56` 主机（`32` NPU） | `7` 独立 UB 平面（NPU 接 `0..6`，CPU 接 `0..3`，NIC `0..1`，SSD `5..6`）；`K=4` Spine × `J=4` 上行链路/对（`16 up >= 13 down`，**1:1 无阻塞**）；fabric 延迟 `100 ns` | 规模缩比为 1/12（48→4 节点），保留 7 平面与 NPU:CPU 2:1 异构比例；未显式建模节点内 PCIe/HCCS 层级差异 |
| `cm_pod_osub` | 同上超分变体 | 同上（`56` 主机） | `K=3` Spine × `J=2` 上行链路/对（`6 up vs 13 down`，**~2.17:1 上行超分**） | 用于检验超分骨干拥塞与故障重路由叠加下的表现 |
| `ub_mesh` | `arXiv:2503.20377`（UB-Mesh §3.2–3.3） | `4` 主板 × (`8` NPU + `2` CPU + `1` NIC + `1` SSD) = `48` 主机（`32` NPU） | 每 NPU `10` 口（`4.0 Tbps`）：**`6` 条 1-hop 直连边**（3 板内 4-NPU 全连接 `20 ns` + 3 跨板同秩全连接 `40 ns`）+**`4` 条 LRS 低基数交换轨道**（板间 LRS 直连 `lrs_links=3 × 400G`，`100 ns`，无 L2 Spine） | 按节点内/间直连边静态编号为 1-hop 平面 `p>=4`，单跳直连边受损时全局 `a2a` 对特定远端对缺乏全对称多平面绕行（`lm_r17` 含 `phys=4` 物理不可行） |

### 9.2 开源 AI 基准工作负载（`flexspray_aicb.py`）

| 负载标识 | 开源 / 学术来源 | DAG 阶段结构与规模 | 关键参数（代码常量） | 建模声明 |
|---|---|---|---|---|
| `b_aicb_tp_ep` | `aliyun/aicb`（Megatron + DeepSeek 混合并行） | 4 层 × (板内 TP Ring AllGather → `ep_pool` 24-NPU EP Dispatch → EP Combine → TP ReduceScatter) + 跨板 DP Ring AllReduce | `GAP_PIPE_NS=2000`, `GAP_COMP_NS=10000`, `FP8_FACTOR=(1+4/128)/2=0.515625` | 用 `ep_pool=NPU0..5`（24 NPU）与 NPU6..7 共存制造平面竞争；不建模 GPU kernel 计算内部细节 |
| `b_deepep_skew` | DeepSeek-V3 / DeepEP MoE | 8 层交替 Dispatch (`FP8_FACTOR`) + 反向 Combine (`BF16`)，共 16 屏障阶段 | 目标专家权重按 **Zipf `alpha=1.0`** 排序分配，每层按 `seed` 洗牌 | 用固定 Zipf(1.0) 近似真实 token 路由器偏斜 |
| `b_deepep_prefill` | DeepSeek-V3 / AICB 双模态服务 | 2 层重流量 Prefill (`~115 us` Combine) + 4 层短爆发 Decode (`~22 us` Combine) | 消息尺寸 ±15%（Prefill）/ ±20%（Decode）随机抖动 | 检验大流量排队与后续微秒级 Decode 低时延需求在同一故障残留下的切换 |
| `b_nccl_pipe` | NCCL 双环多块流水线 Ring AllReduce | 6 个宏观流水阶段 × 2 并发切片 × 双向双环邻居交换 | `GAP_PIPE_NS=2000`, 单阶段目标 `65 us`, 约 `400 us` 连续流 | 去除全局每步强屏障，聚焦持续流控与尾段拖延 |
| `b_kv_skew` | Mooncake (`FAST'25`) KVCache 读写混合 | 4 轮并发（`20 us` 轮间隔）：随机 `>=4` 个 `NPU6..7` 从 `SSD0` 热读扇入 + 全部 `NPU6..7` 向其余 SSD 异步写卸载 | 读阶段目标 `70 us`（`SSD0` 吸收 60% 读请求）、写阶段目标 `25 us`，支持 `--scale 0.5 / 1.0 / 2.0` | 通过 **SSD 平面 `4,5`**（在 `ub_pod` 中对应 port `4,5`）收发，故故障测试必须使用 `F3ssd3 / H1ssd`（`r17_kv_io`），`F3far3 / H1far` 不穿过 SSD 口 |

---

## 10. Round 19 新增资产：真实 SimAI Collective Trace 与动态/流控/规模盲测扩展

为进一步检验从学术真实工作负载 Trace、规模、流控体制到参数鲁棒性的外推泛化能力，Round 19 在原有合成/AICB 套件上新增：

1. **真实公开 AI 集群集合通信 Trace（`b_realtrace`）**：
   - 原始来源：Alibaba [`aliyun/SimAI`](https://github.com/aliyun/SimAI)（Apache-2.0，commit `a57d765c`，文件 `example/workload_analytical.txt` -> [simai_workload_analytical.txt](traces/simai_workload_analytical.txt)）。
   - 转换规则：由 [convert_trace.py](traces/convert_trace.py) 提取完整 Hybrid Transformer + MoE 前向/反向块与 Embedding AllReduce（共 **18 个带 `deps` 依赖的集合通信阶段**，覆盖 `tp` `allgather`/`reducescatter`/`alltoall`、`ep` `alltoall`、`all` `allgather`/`reducescatter` 及 `dp` `allreduce`），按 `1/128` 保持相对尺寸比例写入 [real_collective_trace.csv](traces/real_collective_trace.csv)，并在 `ub_pod` 与 `cm_pod` 上覆盖 `F0 / F1far / F3far / H1far / F3heal / F3multi` 六类故障盲测（E5 `192` runs）。
2. **E2 动态复合故障扩展（`flexspray_case.py`）**：
   - 新增 `F3heal`（`50 us` 降速 `+50 us RESTORE` 自愈）、`H1heal`（硬断 `+40 us UP` 物理恢复与代际升代回收）、`F3flap`（`DOWN -> UP -> DOWN -> UP` 连续闪烁）、`F3multi`（`t_f` 远端 `1/4` 降速叠加 `+30 us` 近端异平面 `DOWN`）与 `F3up3`（核心上行 3 链路 `1/4` 降速）。
3. **E3 大规模超节点扩展**：
   - `cm_pod` 扩展至 **`S=8`（`64` NPU，--scale `0.5`）** 与 **`S=16`（`128` NPU，`d2d, b_kv` 使用 `--scale 0.5`，`b_ring` 使用 `--scale 0.25`）**，验证 128 NPU、180 次盲测零失败与无显著效率塌缩。
4. **E4 流控与有损体制扩展**：
   - 在 `a2a / incast / b_moe / mixed` 上统一对照 `CBFC`、`PFC_DYNAMIC`（`+fc_pfc`）与关闭硬件流控的 `NONE + SELECTIVE SACK + EnableFastRetrans`（`buf128` / `buf256` 浅缓冲，`192` runs）。
