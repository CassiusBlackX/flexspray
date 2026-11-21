# FlexSpray (`ns-3-ub` Branch `feat/flexspray`)

FlexSpray is a lightweight endpoint-network co-designed packet-spray load balancing and congestion control architecture for heterogeneous multi-plane Unified Bus (UB) AI supernodes (and general lossy/lossless multi-plane interconnects).

Current Baseline: **FlexSpray v6** (Round 15–17, frozen build `r17cand2`, tag `flexspray-v6-r17`).

---

## Quick Start: Build, Test, and Reproduce

All commands run inside the repository root (`ns-3-ub/`):

```bash
# 1. Configure (if not already configured) and build release binaries
./ns3 build ub-quick-example unified-bus-test test-runner

# 2. Run FlexSpray unit test suite (11 tests, ~1.4 s)
./build/utils/ns3.44-test-runner --test-name=unified-bus-flexspray --verbose

# 3. Run full unified-bus unit test suite (50 suites; requires Python/venv for helper runners)
for s in $(./build/utils/ns3.44-test-runner --list | tr -s ' ' '\n' | grep '^unified-bus' | sort); do
  ./build/utils/ns3.44-test-runner --test-name="$s" > /dev/null && echo "PASS $s" || echo "FAIL $s"
done

# 4. Run system-level recovery & tail-segmentation regression suite
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 python3 scratch/flexspray/flexspray_regress.py \
  --out /tmp/flexspray_regress --bin ./build/scratch/ns3.44-ub-quick-example
```

> **Note**: All FlexSpray v6 attributes default to **off** in C++ `TypeId` definitions so that upstream Golden bit-exact checks (`25/25 SAME`) pass cleanly. Enabling FlexSpray v6 and comparator baselines (`mprdma`, `reps`, `adaptive+caqm`, `ecmp`, `hpn`) is handled uniformly in [flexspray_case.py](file:///home/cassius/ub/ns-3-ub/scratch/flexspray/flexspray_case.py).

---

## Key Topologies, Benchmarks, and Scripts (`scratch/flexspray/`)

- **Case & Fault Generator**: [`flexspray_case.py`](flexspray_case.py), [`flexspray_matrix.py`](flexspray_matrix.py)
- **Public Supernode Topologies (`cm_pod`, `cm_pod_osub`, `ub_mesh`)**: [`flexspray_topo_public.py`](flexspray_topo_public.py) (`arXiv:2506.12708`, `arXiv:2503.20377`)
- **Open-Source AI Workloads (`b_aicb_tp_ep`, `b_deepep_skew`, `b_deepep_prefill`, `b_nccl_pipe`, `b_kv_skew`)**: [`flexspray_aicb.py`](flexspray_aicb.py)
- **Evaluators & Physical Lower Bounds**:
  - Single-fault `< 1/N` across all 4 `N` & 3 windows: [`flexspray_lossmatrix.py`](flexspray_lossmatrix.py), [`flexspray_losstable.py`](flexspray_losstable.py)
  - Per-phase tail inflation (`one-off` vs `per-phase`): [`flexspray_phaseinfl.py`](flexspray_phaseinfl.py)
  - Latency- & fault-aware DAG LP bound: [`flexspray_dagbound.py`](flexspray_dagbound.py)
  - Recovery oracle & regression: [`flexspray_recovery_suite.py`](flexspray_recovery_suite.py), [`flexspray_regress.py`](flexspray_regress.py)

---

## Documentation & Archived Results

- **Primary Delivery Entry Point**: [`DELIVERY.md`](DELIVERY.md)
- **Technical Report & Research Findings**: [`REPORT.md`](REPORT.md), [`RESEARCH.md`](RESEARCH.md)
- **Per-Round Feasibility & Ablation Record**: [`FEASIBILITY.md`](FEASIBILITY.md)
- **Benchmark Sources & Evaluation Protocol**: [`BENCHMARK.md`](BENCHMARK.md), [`EVAL_PROTOCOL.md`](EVAL_PROTOCOL.md)
- **Attribute Audit & Open Boundaries**: [`KNOBS.md`](KNOBS.md), [`TODO.md`](TODO.md)
- **Problem Statement & Engineering Rules**: [`docs/REQUESTS.md`](docs/REQUESTS.md), [`docs/CLAUDE.md`](docs/CLAUDE.md)
- **Compact Round 17 Blind SOTA Results (`< 5 MB`)**: [`results/r17/`](results/r17/)
