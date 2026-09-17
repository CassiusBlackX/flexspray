# T28 最终验收报告：Unified Bus 生命周期性能与语义

## 最终结论

T28 的语义验收通过，性能验收失败。

- p01 packet-heavy 场景相对 GitCode main 的成对中位 wall 回退为 `+16.64%`，RSS 回退为
  `+14.86%`，两项均超过 10% 门限。
- p01 相对 next 为 `+7.25% wall / +8.42% RSS`，在门限内。
- p02 resource-heavy/OFF 相对 main 为 `+5.46% wall / +7.83% RSS`，相对 next 为
  `+2.04% wall / +5.85% RSS`，均在门限内。
- p01 的 T24 结果是 `+16.65% wall`；最终 HEAD 是 `+16.64%`。T25-T27 没有可测量地消除
  always-on packet lifecycle 的主要回退，不能宣称整个生命周期分支已通过性能接受。
- 所有 p01/p02 共享语义工件保持既定字节合同；复杂 CTP/RTP 没有新增差异路径。
- 自动 TP cleanup 未实现、未启用，也未计入任何收益。

## 冻结边界

| Role | Revision | Executable SHA-256 | Unified Bus dylib SHA-256 |
|---|---|---|---|
| main | `ccf41b58b0ba3b9711efea595732a2908e3e07cf` | `cf9deb34...149740` | `cf05dd2d...ed98` |
| current | `85c61e8bc4a6a47971f1e7f83e3eced72f47f531` | `7756e587...45a78` | `0968fced...32878` |
| next | `cf8ee683e00e5b6210661013e93cec7217d662e5` | `51129796...c387` | `5d090eef...14cc92` |

三方均使用 release、local-single、MPI OFF、Apple clang 21.0.0、arm64 macOS 26.5.1 和
`rng-run=1`。main 的测试构建开关为 OFF，current/next 为 ON；该开关不改变冻结的 release
quick-example/Unified Bus 运行路径。全部运行严格串行，没有重建冻结二进制。

完整 source root、输入 SHA-256、CMake、动态依赖、runtime、命令、旧复杂树 digest 和三份
raw summary digest 记录在 `T28-manifest.json`。

## p01：packet-heavy，spray OFF，lifecycle OFF

执行一次 warm-up 和五轮 main/current/next counterbalanced measured runs。

| Role | wall median | `/usr/bin/time` real | user | sys | RSS | faults | block I/O in/out |
|---|---:|---:|---:|---:|---:|---:|---:|
| main | 1.474950 s | 1.47 s | 1.38 s | 0.06 s | 19,791,872 B | 191 | 0 / 0 |
| current | 1.720335 s | 1.71 s | 1.63 s | 0.06 s | 22,806,528 B | 135 | 0 / 0 |
| next | 1.599620 s | 1.59 s | 1.50 s | 0.06 s | 21,086,208 B | 135 | 0 / 0 |

| 对比 | paired wall | paired RSS | 门限 |
|---|---:|---:|---|
| current vs main | `+16.64%` | `+14.86%` | **FAIL** |
| current vs next | `+7.25%` | `+8.42%` | PASS |

五轮 current/main wall 差异为 `+16.64%`, `+18.12%`, `+18.06%`, `+16.64%`, `+4.62%`。
第五轮 main 自身明显变慢，但成对中位数不受该单轮影响。四轮集中在 16%-18%，因此最终
回退不是单次噪声。

生产输出计数在三方一致：2 个完成 task、192 条 transport input resource、8 条 TaskTrace
event、512,640 条 PortTrace event、896,664 条共享 trace line、46,780,490 B 共享 trace。
五轮对 main 和 next 的 10 组共享 artifact 比较全部逐字节一致，无 exception。

## p02：resource-heavy，HASH-compatible，lifecycle OFF

同样执行一次 warm-up 和五轮三方 counterbalanced measured runs。

| Role | wall median | `/usr/bin/time` real | user | sys | RSS | faults | block I/O in/out |
|---|---:|---:|---:|---:|---:|---:|---:|
| main | 0.159608 s | 0.15 s | 0.12 s | 0.03 s | 58,802,176 B | 157 | 0 / 0 |
| current | 0.168380 s | 0.16 s | 0.12 s | 0.03 s | 63,422,464 B | 135 | 0 / 0 |
| next | 0.163624 s | 0.16 s | 0.12 s | 0.03 s | 59,916,288 B | 135 | 0 / 0 |

| 对比 | paired wall | paired RSS | 门限 |
|---|---:|---:|---|
| current vs main | `+5.46%` | `+7.83%` | PASS |
| current vs next | `+2.04%` | `+5.85%` | PASS |

第三轮 current 出现 0.242489 s host outlier，对 main/next 分别是 `+51.93%` 和 `+47.19%`；
其余四轮 current/main 为 `+2.50%` 至 `+5.72%`。原始离群结果保留，最终采用预先约定的
五轮成对中位数，没有删除或平均隐藏。

生产输出计数在三方一致：8 个完成 task、992 条 transport input resource、464 条 TaskTrace
event、13,564 条 PortTrace event、25,676 条共享 trace line、1,799,131 B 共享 trace。

p02 的共享 artifact 除 `TaskTrace_node_16.tr` 至 `TaskTrace_node_23.tr` 外全部字节一致。这八个
文件属于既有 T05 边界：两侧完整行 multiset 相同，逐位置时间戳序列也相同，只在相同时间戳
内部交换记录顺序。双方完整 SHA-256 已绑定在 `T28-summary.json`，没有未知差异。

## p02 SUMMARY 的独立成本

current 的 OFF/SUMMARY 另做一次 warm-up 和五轮 counterbalanced pairs：

| Mode | wall median | RSS | lifecycle records | resources | lifecycle bytes |
|---|---:|---:|---:|---:|---:|
| OFF | 0.166396 s | 63,471,616 B | 0 | 0 | 0 |
| SUMMARY | 0.212819 s | 64,651,264 B | 68,816 | 3,128 | 5,641,986 B |

SUMMARY 相对 OFF 为 `+27.11% wall / +1.86% RSS`。五轮 wall 差异全部为正，范围
`+25.82%` 至 `+32.91%`。OFF 与 SUMMARY 的 task、throughput 和所有共享 trace 完全字节
一致；额外成本来自显式 lifecycle evidence。这个结果支持 T23 将 minimal 设为 OFF，但不应
被当成 always-on lifecycle 核心成本。

## 复杂 CTP/RTP

### CTP Jetty inflight/window

四个并发 1 MiB WRITE 共用一个 bound Jetty，window 1/4/128 的双方 task 完成时间如下：

| Window | task 0 | task 1 | task 2 | task 3 | current vs next |
|---:|---:|---:|---:|---:|---|
| 1 | 930.35344 us | 1846.53688 us | 2777.35832 us | 3667.86076 us | 16/16 shared files exact |
| 4 | 331.85448 us | 661.52440 us | 997.48524 us | 1318.99184 us | 16/16 shared files exact |
| 128 | 20.15192 us | 36.74032 us | 52.99816 us | 69.75184 us | 16/16 shared files exact |

三档响应明显不同，说明 Jetty window/backpressure seam 确实触发；每个精确完成时间和所有共享
工件均与 next 一致。

### RTP 已审核边界

| Case | 结果 | 差异边界 |
|---|---|---|
| shared TP inflight=1 | PASS | 仅 2 个 T05 同时间戳 TaskTrace 顺序文件 |
| GBN PSN 1/3 double loss | PASS with controlled difference | T06 累计 ACK descriptor-prefix retirement，15 个已绑定路径 |
| TPG concurrent loss/pinning | PASS with controlled difference | T14 resolved-generation attribution/pinning，5 个已绑定路径 |

next 重跑与旧 next 树逐文件一致；final current 重跑与旧已审核 lifecycle 树逐文件一致。因此
T05/T06/T14 的 changed path 集合及双方文件 SHA-256 没有扩大。所有 task 均完成，GBN 没有
新增重传异常，TPG 重传仍固定在 admitted TP generation。

current 显式 SUMMARY 由 production parser 验证：三个 CTP case 各 23,408 条记录；RTP shared、
GBN、TPG 分别为 506、682、1,452 条记录。旧包依赖 minimal/SUMMARY；T23 后 minimal 默认 OFF，
所以最终复验显式选择 SUMMARY。第一次 OFF 重跑也保留在 raw evidence 中，其共享语义工件
同样通过。

## 接受判断

- 语义与字节合同：**PASS**。
- CTP window/backpressure：**PASS**。
- RTP retransmission/selection：**PASS with bounded existing exceptions**。
- p02 host performance：**PASS**。
- p01 host performance against next：**PASS**。
- p01 host performance against main：**FAIL**。
- T28 总体：**FAIL (measured performance blocker)**。

当前分支的功能和协议行为证据已经收拢，但相对 main 的 packet-heavy host 开销仍未收拢。
如果要求“功能正确即可”，该分支具备明确证据；如果要求“性能回退不超过 10%”，现在不能
接受，需要另开针对 always-on packet owner/lifecycle mutation 的后续优化，而不是把 T28 标绿。
