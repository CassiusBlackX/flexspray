# FlexSpray v7 交付物与全量验收核验单（Round 19 / `r19nsdi`）

## 1. 冻结二进制与五道前置正确性门禁（`r19nsdi`）

`r19nsdi` 冻结构建自 `src/unified-bus/`（C++ `Release` 构建，含 `UbController::RecycleDeadTp` 响应唤醒及 `UbTransportChannel` 双向对等代际恢复增强），所有门禁均在末次 C++ 修改后 **5 / 5 全部通过**，且 C++ 默认属性保持关闭以保证原生 Golden 逐位一致：

| 门禁 | 执行命令 / 脚本 | 验证结果 |
|---|---|---|
| Release 构建与 SHA256 校验 | `./ns3 build ub-quick-example unified-bus-test test-runner && freeze.sh r19nsdi` | `libns3.44-unified-bus.so` & `libns3.44-unified-bus-test.so` SHA256 与 `build/lib/` 严格一致 |
| Gate 1–2：FlexSpray 专项单测 | `/home/cassius/ub/work/frozen/r19nsdi/testrunner --test-name=unified-bus-flexspray --verbose` | **17 / 17 PASS**（`3.498 s`；含 `UbFlexSprayHardDownRecycleTest` 双向 `0 <-> 1` 双周期 Flap 测得 `completed == 4u, RECYCLE gen=2`，`0.785 s`） |
| Gate 3：Golden 逐位对照 | `/home/cassius/ub/work/bin/golden.sh r19def3 .../r19nsdi/sim` + `cmpgold.sh s131 r19def3` | **25 / 25 SAME** |
| Gate 4：全量 UB 单元套件 | `/home/cassius/ub/work/bin/alltests_frozen.sh r19nsdi r19all3` | **50 / 50 suites PASS**（`0` non-PASS） |
| Gate 5：系统级回归脚本 | `flexspray_regress.py --bin /home/cassius/ub/work/frozen/r19nsdi/sim` | **exit 0**（`fence_on: 22 cases passing, failing: none`; `fence_off: P5=False, late_execs=9`; `tail4k: missing_tasks=0, eff=0.9404`） |

---

## 2. 未见盲测核心指标验收（Seeds `15, 16`，总计 `2,898` 次运行）

在全部 22 个盲测批次（E1–E5，共 `2,898` 次全仿真，`1,076` 次 `flex+spraycc` / `flex+spraycc+v6` 运行）中：**`flex+spraycc*` 实现 `rc=0` 且 `missing_tasks=0` 100.0% 零失败**，所有数值均来自 [results/r19/r19_summary.json](results/r19/r19_summary.json) 及同目录汇总表：

### 2.1 LossMatrix 规范达标口径（Round 19 全量 77 格扩展集 `e1_losstable_v7_{lt,le}.md` & E1 SOTA 矩阵 `e1_losstable_sota_{lt,le}.md`）

1. **Full v7 扩展矩阵（`< 1/N` vs `<= 1/N` 并列，保留 `n/a` 于分母，给出全 4 类 `N` 与 3 个窗口）**：

| 语义 | 比较符 | 窗口 | `N_dev` | `4N` | `N_scope` | 物理 / 实现归因明细（`N_dev`） |
|---|---|---|---|---|---|---|
| `graceful` | `<` | `tdev` | **47 / 51** | 13 / 51 | **47 / 51** | `phys 4, impl 0, miss 0` |
| `graceful` | `<=` | `tdev` | **47 / 51** | 13 / 51 | **47 / 51** | `phys 4, impl 0, miss 0` |
| `graceful` | `<` / `<=` | `steady` | 36 / 51 | 11 / 51 | 36 / 51 | `phys* 4, impl 0, n/a 11` |
| `graceful` | `<` / `<=` | `trans` | 45 / 51 | 18 / 51 | 46 / 51 | `<`: `phys* 6`; `<=`: `phys* 4, impl 2` |
| `hard` | `<` | `tdev` | **42 / 49** | 13 / 49 | **42 / 49** | `phys 4, impl 3, miss 0` |
| `hard` | `<=` | `tdev` | **42 / 49** | 13 / 49 | **42 / 49** | `phys 4, impl 3, miss 0` |
| `hard` | `<` / `<=` | `steady` | 22 / 49 / 23 / 49 | 11 / 49 | 22 / 49 / 23 / 49 | `<`: `phys* 18, n/a 9`; `<=`: `phys* 3, impl 14, n/a 9` |
| `hard` | `<` / `<=` | `trans` | 28 / 49 / 31 / 49 | 12 / 49 | 28 / 49 / 32 / 49 | `<`: `phys* 21`; `<=`: `phys* 2, impl 16` |
| `lane` | `<` | `tdev` | **76 / 77** | **34 / 77** | **77 / 77** | `N_dev`: `phys 0, impl 1`; `N_scope`: **`0 phys, 0 impl`** |
| `lane` | `<=` | `tdev` | **76 / 77** | **34 / 77** | **77 / 77** | 同上（唯一 `N_dev` 未达为 `h2d F3far3` `13.6% > 12.5%`，`N_scope` 达标） |
| `lane` | `<` / `<=` | `steady` | 60 / 77 | 6 / 77 | 61 / 77 | `<`: `phys* 1, n/a 16`; `<=`: `impl 1, n/a 16` |
| `lane` | `<` / `<=` | `trans` | 72 / 77 | 12 / 77 | 73 / 77 | `<`: `phys* 1, impl 4`; `<=`: `impl 5` |

2. **E1 分拓扑分层 SOTA 对照（`tdev < 1/N_dev` & `<= 1/N_dev` 同值）**：
   - `ub_pod`（各 `11` 格）：`flex+spraycc` **`11/11` `graceful`**、**`10/11` `hard`**、**`11/11` `lane`**（且 `11/11` 通过 `1/(4N)`！）；对比 `ecmp` `6/11, 0/11 (miss=10), 3/11`、`reps` `10/11, 6/11, 9/11`、`sird` `11/11, 9/11, 10/11`。
   - 四拓扑分层汇总（`graceful=16`, `hard=25`, `lane=25`）：`flex+spraycc` **`lane` `25 / 25`（100.0%，`0 phys, 0 impl`）**、**`graceful` `15 / 16`**、**`hard` `19 / 25`（`phys=1, impl=5, miss=0`）**。
   - 权威归并脚本 `e1_losstable_sota_{lt,le}.md`（按 `(scenario, fault)` 跨拓扑取最差）：`flex+spraycc` **`lane` `11 / 11`（`0 phys, 0 impl`）**、**`graceful` `15 / 16`**、**`hard` `8 / 11`**。

### 2.2 阶段尾膨胀（`PhaseInfl`）与关键路径物理界（`DAGBound`）

- **`F3far` 阶段尾膨胀（每方案 `34` 例；`mprdma` `28`，`flex+v6` `22`，`adaptive` `14`）**：`flex+spraycc` **`34 / 34` `one-off`（`0` 每阶段拖动）**，中位超额均值 **`1.62%`**，最大 **`6.75%`**；`flex+spraycc+v6` `22/22 one-off`（均值 `1.34%`，最大 `3.51%`）；全部 SOTA 基线均发生多例每阶段膨胀：`sird` `13/34`（最大 `28.46%`）、`reps` `12/34`（最大 `28.46%`）、`ecmp` `11/34`（最大 `49.84%`）、`mprdma` `8/28`（最大 `26.35%`）、`adaptive` `2/14`（最大 `22.32%`）。
- **Clean E1 Fault-Aware DAGBound（`66` 多阶段作业，丢任务记 `0.0`）**：`flex+spraycc` **`0.9319`**（`F0 0.9579 / F3far 0.9428 / H1far 0.8950`）领先 `reps` (`0.8815`)、`sird` (`0.8674`)、`mprdma` (`0.8290`)、`adaptive` (`0.5822, 22/30`)、`ecmp` (`0.5330, 46/66`)、`hpn` (`0.2491, 54/66`)。

### 2.3 动态故障（E2）、规模扩展（E3）、流控体制（E4）、敏感性与真实 Trace（E5）

- **E2 动态复合故障**：`e2_dyn_ubpod`（`180` runs）`flex+spraycc` **`0.8383`**（`v6` `0.8256`、`mprdma` `0.5980`、`sird` `0.5440`、`reps` `0.5102`、`hpn` `0.2525`），并发故障 `F3multi` 达 **`0.8250`**（`4.37x` over `reps 0.1887`）；`e2_dyn_cmpod`（`64` runs）`flex+spraycc` **`0.8128`**（`H1heal 0.8236 vs v6 0.7478` **`+7.58 pp`**；`F3flap 0.8055 vs v6 0.7562` **`+4.93 pp`**）。
- **E3 `cm_pod` 规模扩展（32 -> 64 -> 128 NPU）**：`S=8`（`--scale 0.5`, 90 runs）`flex+spraycc` `18/18`（均效 **`0.7314`**, `b_ring` DAGBound `0.8545`）；`S=16 d2d,b_kv`（`--scale 0.5`, 60 runs）`12/12`（**`0.9343`**）；`S=16 b_ring`（`--scale 0.25`, 30 runs）`6/6`（DAGBound **`0.8340`** vs `reps 0.7688`, `hpn 0.5387`, `ecmp 0.3623`）。
- **E4 流控体制（`192` runs）**：`flex+spraycc` 在 `CBFC`（**`0.8923`**）、`PFC`（**`0.8923`** vs `sird+pfc 0.7883` / `ecmp+pfc 0.5493`）、`Lossy+SACK 256KB`（**`0.8001`** vs `reps 0.7119` / `ecmp 0.5426`）与 `Lossy+SACK 128KB`（**`0.7694`**）下全部 `24/24` 零丢任务完成。
- **E5 敏感性与真实 SimAI Trace**：
  - 零重调参跨 `100G / 400G / 800G` 与 `1x / 5x / 10x` RTT：`100G` **`0.8895`**（`18/18` vs `reps/sird 17/18`, `ecmp 16/18`）、`800G` **`0.8097`**、`5x RTT` **`0.8441`**、`10x RTT` **`0.7671`**（领先定长计时 `v6` `0.7463` **`+2.08 pp`**）；`H1far` 下高优控制 `ctrl_p99` 从 `v6` 的 `13.8–21.7 us` 压降至 **`1.47–5.45 us`**（**`3.4x–13.2x` 降幅**）。
  - Alibaba SimAI `b_realtrace`（`192` runs）：`ub_pod` DAGBound **`0.7831`** / 原始均效 `0.2575`（`12/12`）、`cm_pod` DAGBound **`0.7949`** / 原始均效 `0.2847`（`12/12`），而 `adaptive` 与 `ecmp` 在 `H1far` 与 `F3multi` 全军覆没（各自 `8/12`，丢失 `2,743–3,079` 任务）。

---

## 3. 代码、文档与 Git 状态清单

- **核心交付目录**：`src/unified-bus/`（FlexSpray v7 C++ 实现与 17 项单测）、`README.md`、`scratch/flexspray/{THEORY,RESEARCH,REPORT,DELIVERY,BENCHMARK,EVAL_PROTOCOL,KNOBS,FEASIBILITY,TODO,README}.md`、`scratch/flexspray/traces/`、`scratch/flexspray/results/r19/`（共 `1.2 MB` `< 4 MB`）、`scratch/flexspray/fig/nsdi/`（6 组矢量 PDF + 300-DPI PNG 共 `2.3 MB`）。
- **大小与清理约束检查**：`scratch/2nodes_single-tp/test/` 保持未跟踪状态；`scratch/flexspray/` 内**零文件大于 5 MB**。
