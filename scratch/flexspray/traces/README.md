# Public AI Collective Workload Trace Provenance

- **File**: `simai_workload_analytical.txt`
- **Upstream Repository**: `https://github.com/aliyun/SimAI`
- **Source Path**: `example/workload_analytical.txt`
- **Upstream License**: Apache License 2.0
- **Commit SHA**: `a57d765c5478f319c5f5ab6fdc53f9cc7f461743` (`2026-09-20T07:55:42Z`)
- **File SHA256**: `8c2e255aa7397d8c5c7e01daa04d4b3c97a6b015a27bd425770ef8e7e5cbe0a1`
- **Size**: `140,398` bytes (`1,791` lines)
- **Workload Description**: Hybrid Transformer + MoE forward/backward analytical collective trace (`HYBRID_TRANSFORMER_FWD_IN_BCKWD`, `ep=16, pp=12, vpp=8, ga=24`).
- **Conversion Script**: `convert_trace.py` extracts one complete Hybrid Attention + MoE block plus embedding gradient AllReduce (16 collective phases covering TP AllGather/ReduceScatter, EP AlltoAll, global AllGather/ReduceScatter, and DP AllReduce), preserving exact relative message-size ratios scaled by `1/128` into `real_collective_trace.csv` for sub-millisecond pod-scale UB simulation via `--scenario b_realtrace`.
