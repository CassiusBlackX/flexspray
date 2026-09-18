# Quick Start

**Language**: [English](QUICK_START_en.md) | [中文](QUICK_START.md)

> This project is built on ns-3.44 and verified on Linux and Windows WSL. For detailed platform support, installation steps, system requirements, and build options, see [ns-3.44 Documentation](https://www.nsnam.org/releases/ns-3-44/documentation/), [Installation Guide](https://www.nsnam.org/docs/release/3.44/installation/singlehtml/), and [ns-3.44 Source](https://gitlab.com/nsnam/ns-3-dev/-/tree/ns-3.44?ref_type=tags).

## Prerequisites

The core build depends on the following tools. Download can be via either Git or source archive (via a web browser, wget, or curl).

| Purpose       | Tool                         | Minimum version        |
| ------------- | ---------------------------- | ---------------------- |
| Download      | git (for Git download) <br/>or: tar and bunzip2 (Web)      | No minimum version     |
| Compiler      | g++<br/>or: clang++          | >= 10<br/>>= 11        |
| Configuration | python3                      | 3.12                   |
| Build system  | cmake plus one system-provided build tool | cmake >= 3.13<br/>Platform-dependent |

The `./ns3` launcher does not work on Python 3.14: its argparse no longer accepts positional arguments that consume no arguments, so startup fails with `ValueError: action 'store_true' is not valid for positional arguments`. Use Python 3.12; the commands below use `python3.12` throughout, and any 3.9 - 3.13 interpreter can be substituted.

If using Conda/virtualenv, please ensure that the `python3` used later matches the interpreter used to install dependencies.

### Check versions quickly

From the command line, you can check the versions as follows:

| Tool    | Version check command |
| ------- | --------------------- |
| g++     | `g++ --version`       |
| clang++ | `clang++ --version`   |
| python3 | `python3 -V`          |
| cmake   | `cmake --version`     |
| make    | `make --version`      |

## Get the Code

```bash
# Clone the project (the default branch is main; use -b to select another branch or tag)
git clone https://gitcode.com/open-usim/ns-3-ub.git
cd ns-3-ub

# Initialize and update submodules (includes Python analysis tools)
git submodule update --init --recursive

# If the above command fails, you can clone manually:
# git clone https://gitcode.com/open-usim/ns-3-ub-tools.git scratch/ns-3-ub-tools

# Verify submodule status
git submodule status
```

To automatically trigger trace analysis after simulation completion, please configure the tool path in the corresponding use case's `network_attribute.txt`, for example:

```
global UB_PYTHON_SCRIPT_PATH "scratch/ns-3-ub-tools/trace_analysis/parse_trace.py"
```

## Python Tools & Dependencies

The project's Python toolset is located in `scratch/ns-3-ub-tools/` ([open-usim/ns-3-ub-tools](https://gitcode.com/open-usim/ns-3-ub-tools)):

- Topology/Visualization: `net_sim_builder.py`, `topo_plot.py`, `user_topo_*.py`
- Traffic Generation: `traffic_maker/*`
- Trace Analysis: `trace_analysis/parse_trace.py`

It is recommended to install dependencies using the `requirements.txt` in the project:

```bash
python3 -m pip install --user -r scratch/ns-3-ub-tools/requirements.txt
# You may encounter `externally-managed-environment` restrictions, in which case please try using a virtual environment
# Use domestic mirror sources to accelerate downloads
pip install -i https://pypi.tuna.tsinghua.edu.cn/simple -r scratch/ns-3-ub-tools/requirements.txt
# Or use conda:
conda install pandas matplotlib seaborn
# Or manually install the above dependencies
```

Note: These packages are mainly for the topology / traffic generation and visualization tools (`net_sim_builder.py`, `topo_plot.py`, `traffic_maker/*`). The trace post-processing scripts under `trace_analysis/` depend only on the Python standard library and need no installation.

Tip: If you use an agent that supports repo-local skills, this repository includes OpenUSim skills under ` .agents/skills/ `. They check startup facts from this page and `README_en.md`, then help plan experiments, generate cases, run simulations, and analyze results through `openusim-welcome`, `openusim-plan-experiment`, `openusim-run-experiment`, and `openusim-analyze-results`. They cover smoke runs, old-case reproduction, one-off debugging, A/B comparisons, parameter sweeps, and controlled-variable studies. For comparisons, the plan records the baseline, compared configurations, fixed controls, prediction, and evidence source before cases are generated. After analysis confirms a root cause or a reusable judgment, `openusim-capture-insights` can write it as a knowledge card with user approval. These skills depend on the current `ns-3-ub` working tree and `scratch/ns-3-ub-tools/`, and are maintained with this repository.

## Build

```bash
# This page uses python3.12 throughout (see Prerequisites).
# One build may use -j for speed; do not start multiple build/test tasks at once.
BUILD_JOBS=${BUILD_JOBS:-$(python3.12 -c 'import os; print(os.cpu_count() or 1)')}

# Configure only modules required by UB simulations
python3.12 ./ns3 configure --enable-modules=unified-bus --disable-werror -d release

# Build the UB simulation entry and its dependencies
python3.12 ./ns3 build -j "$BUILD_JOBS" ub-quick-example
```

When `-G` is omitted, `./ns3` automatically selects an available CMake build tool.
Users do not need to select or install a specific tool; if no build tool is
available, CMake reports the missing generator during configuration.

If you need to develop or validate other ns-3 modules, use a full configuration without `--enable-modules`.

### (Optional) Enable Unison multi-threaded simulation

To enable Unison for ns-3 multi-threaded parallel simulation (MTP), add `--enable-mtp` during configuration (you may also enable examples):

```bash
python3.12 ./ns3 configure --enable-modules=unified-bus --enable-mtp --disable-werror -d release
python3.12 ./ns3 build -j "$BUILD_JOBS" ub-quick-example

# Use --mtp-threads to enable multi-threading at runtime (must be >= 2)
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_single-tp --mtp-threads=8'
```

See [UNISON_README.md](UNISON_README.md) for additional MTP usage, behavioral boundaries, and diagnostic options.

## Run a Minimal Example

```bash
# If using Conda, ensure its bin is in PATH first (or activate environment first)
# export PATH=<your-conda-path>/bin:$PATH

# Run small example and trigger trace analysis
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_single-tp'

# Verify output
ls scratch/2nodes_single-tp/output/
# Expected to contain: task_statistics.csv  throughput.csv
```

This example needs no third-party packages: the post-processing scripts `trace_analysis/parse_trace.py` and the `task_statistics.py` / `cal_throughput.py` it invokes depend only on the Python standard library. The packages listed under "Python Tools & Dependencies" are needed only when you use the topology / traffic generation and visualization tools (`net_sim_builder.py`, `topo_plot.py`, `user_topo_*.py`, `traffic_maker/*`). If you then encounter `ModuleNotFoundError: No module named 'pandas'`, it means the runtime `python3` is inconsistent with the interpreter used to install dependencies; please check PATH, or install the dependencies again under that interpreter.

## Examples under scratch

The following are the available use case directories and corresponding run commands currently provided in the repository:

- 2 nodes (single TP):
  ```bash
  python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_single-tp'
  ```

- 2 nodes (multiple TP):
  ```bash
  python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_multiple-tp'
  ```

- 2 nodes (packet spray):
  ```bash
  python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_packet-spray'
  ```

- 2D FullMesh 4x4 (multipath All-to-All):
  ```bash
  python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2dfm4x4-multipath_a2a'
  # Enable multi-threading acceleration (requires --enable-mtp compilation)
  python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2dfm4x4-multipath_a2a --mtp-threads=8'
  ```

- 2D FullMesh 4x4 (hierarchical broadcast):
  ```bash
  python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2dfm4x4-hierarchical_broadcast'
  ```
  Task dependency notifications use a 100ns latency by default. Override it with
  `--dependency-visibility-delay=<Time>` when the modeled control path differs.

- Clos (32 hosts / 4 leafs / 8 spines, pod2pod):
  ```bash
  python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/clos_32hosts-4leafs-8spines_pod2pod'
  # Multi-threading recommended for large topologies
  python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/clos_32hosts-4leafs-8spines_pod2pod --mtp-threads=16'
  ```

Note: Some large-scale use cases take a long time to run. Please use `--mtp-threads=8` to enable multi-threading (requires `--enable-mtp` compilation).

## Full Workflow Example

```bash
# Run complete example, including Python post-processing
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2dfm4x4-multipath_a2a'

# Expected output (timestamps and absolute paths vary by environment):
[19:14:22] [case] Run case: <repo>/scratch/2dfm4x4-multipath_a2a
[19:14:22] [setup] Load network_attribute.txt
[19:14:22] [setup] Load node.csv
[19:14:22] [setup] Load topology.csv
[19:14:22] [setup] Load routing_table.csv
[19:14:22] [setup] Load transport_channel.csv
[19:14:22] [traffic] Load traffic.csv
[19:14:23] [run] Simulation finished.
[19:14:23] [trace] Parse runlog into output artifacts.
Stats: Processed 2 tasks.
Saved traffic tasks throughput results to <repo>/scratch/2dfm4x4-multipath_a2a/output/task_statistics.csv
Stats: Generated throughput metrics for 32 port-directions.
Saved per-port throughput results to <repo>/scratch/2dfm4x4-multipath_a2a/output/throughput.csv
[19:14:23] [summary] Program finished.

# View generated result files
ls scratch/2dfm4x4-multipath_a2a/output/
# task_statistics.csv  throughput.csv
```

## Configuration Files

Each use case directory typically contains the following files (format can refer to existing examples):

- `network_attribute.txt` - Network global parameters (can configure `UB_PYTHON_SCRIPT_PATH` for automatic post-processing)
- `node.csv` - Node definitions
- `topology.csv` - Topology connections
- `routing_table.csv` - Routing table
- `transport_channel.csv` - Optional explicit RTP channels; omitted channels are reserved while traffic loads
- `traffic.csv` - Traffic definitions

For the `ub-quick-example` entry, configuration semantics, and commands, see [scratch/README.md](scratch/README.md).

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [README_en.md](README_en.md) | Project overview: UB components, repo layout, and key concepts |
| [scratch/README.md](scratch/README.md) | Config-driven entry: configuration semantics, commands, case execution, and parameter reference |
| [open-usim/ns-3-ub-tools](https://gitcode.com/open-usim/ns-3-ub-tools) | Toolchain (submodule): case generation and trace post-processing/analysis |
