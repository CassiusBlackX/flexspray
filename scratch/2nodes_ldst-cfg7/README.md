# LDST CFG7 四任务回归

两个主机通过 Switch 2，双向 LOAD/STORE，各 4096 B；task 0/1 不指定端口，
task 2/3 显式指定源、目的端口 0。链路 400 Gbps、20 ns，CBFC，一个 LDST thread，
LOAD/STORE outstanding 各 4。完整输入和参数快照已交付，可以直接重跑。

两个 `HeaderConfig` 均显式设为 7；各自默认 9，只接受 7/9。CFG7 使用 CNA24/NLP4，
沿用 CFG9 compact 事务头和负载语义，走 TP bypass。NLP4 是模拟器自定义扩展。
属性只控制主动请求，响应（包括延迟响应）沿用收到的请求格式。

## 依赖与构建

以下命令均在仓库根目录执行，构建、测试、仿真依次运行。单个构建可以 `-j4`。
需要 Python 3.12、CMake、Ninja、C++ 工具链和 tools 子模块的 Python 依赖。

```bash
git submodule update --init --recursive
uv venv --python python3.12 .venv-ldst-tools
uv pip install --python .venv-ldst-tools/bin/python \
  -r scratch/ns-3-ub-tools/requirements.txt
source .venv-ldst-tools/bin/activate
python3.12 ./ns3 configure --enable-modules=unified-bus --disable-examples \
  --enable-tests --enable-logs --disable-mpi --disable-mtp --disable-werror -d release -G Ninja
python3.12 ./ns3 build -j 4
```

已有环境和构建产物时只需激活环境。也可用系统 `python3.12 -m venv` 创建环境并用
其 pip 安装依赖；本次机器的系统 ensurepip 不可用，实际使用了 uv。
激活环境保证 runner 调用的 `python3` 能导入 pandas 等依赖。

## 运行与校验

```bash
source .venv-ldst-tools/bin/activate
python3 scratch/2nodes_ldst-cfg7/prepare_run.py
NS_LOG=UbHeader=level_warn python3.12 ./ns3 run --no-build \
  'scratch/ub-quick-example --case-path=scratch/2nodes_ldst-cfg7' \
  > scratch/2nodes_ldst-cfg7/runner.log 2>&1
ldst_case_status=$?
printf '%s\n' "$ldst_case_status" > scratch/2nodes_ldst-cfg7/runner.exit
python3 scratch/2nodes_ldst-cfg7/verify_results.py --case-path scratch/2nodes_ldst-cfg7
python3 scratch/2nodes_ldst-cfg7/test_verify_results.py
```

`prepare_run.py` 将旧 runlog/output、runner 证据和起始记录移入唯一 history 子目录，
随后记录本次起始时间和五个输入文件的 SHA256。校验器只读取当前输出；不要跳过准备
步骤，也不要在运行后重新生成输入。成功证据副本原先存放在
`.scratch/ldst-cfg7/evidence/scratch-success/`，该目录已不再随仓库保留（可从 Git 历史恢复）。

校验器要求固定集合 `{0,1,2,3}` 各有一次开始、一次完成；检查原始时间及 parser 摘要
一致、首包/末 ACK 的因果顺序、Switch 2 两个端口均有收发、输入摘要及输出新鲜度、
runner 退出 0 和成功阶段标记、没有错误或丢包信息。本用例禁用重传，现有 runner
遇到运行时丢包会中止并返回非零。校验失败退出 1，不以 `Simulation finished` 单句
作为通过依据，也不对重复记录去重。

`test_verify_results.py` 复制真实成功结果到临时目录，验证缺文件、缺任务、缺完成、
重复完成、失败退出、丢包、过期证据、输入变化、缺摘要任务、非法时间及缺交换机 Rx
均被拒绝；它保留原始成功结果。

## 重新生成与入口覆盖

```bash
source .venv-ldst-tools/bin/activate
python3 scratch/2nodes_ldst-cfg7/generate_case.py
# cfg9-stable 基线不再随仓库保留，需先从 Git 历史恢复并把下面的值指向该目录
UB_LDST_COMPARE_BASELINE=<restored-cfg9-stable-dir> \
  ./build/utils/ns3.44-test-runner --test-name=unified-bus-ldst-cfg --verbose
```

生成器复用 NetworkSimulationGraph 和运行时属性目录，生成完整 132 项快照（数量随
后续版本变化）；本次解析来源见 `attribute-resolution.json`。目录中的两个属性默认 9，
配置覆盖为 7。生成器只生成输入，运行另行执行；TP bypass 不需要 transport_channel.csv。

CSV MEM 任务走 Api/Instance/Thread。Endpoint 主动收发、两入口独立属性、异配和延迟
响应由 `unified-bus-ldst-cfg` 的 32 个模块测试覆盖；该 suite 通过生产 Header 解析验证
CFG/NLP 和包长。本用例没有从缺少 CFG 字段的 trace 推断线格式，也没有吞吐性能结论。
CFG9 完整字节对比使用“只初始化 CCI、尚未提取代码”的稳定基线，原始未初始化 CCI
导致的失败另存于特性证据目录。

## 实际结果

四任务各完成一次，开始时间均为 0.01 us，完成时间依次为
0.25752、0.30144、0.30416、0.3144 us。Switch 2 的两个端口各 Rx 32、Tx 32。
真实运行退出 0，严格校验 PASS，12 个校验器测试通过（1 个成功副本、11 个拒绝场景）。
首次单线程运行崩溃和随后重复 trace 的失败证据均保留于特性证据目录，未记为通过。
