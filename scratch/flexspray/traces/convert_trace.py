#!/usr/bin/env python3
"""Convert Alibaba SimAI analytical Hybrid-Transformer+MoE collective trace into FlexSpray CSV.

Reads `simai_workload_analytical.txt` (from https://github.com/aliyun/SimAI, Apache-2.0), extracts
one representative forward+backward Transformer+MoE block plus gradient AllReduce, and scales
collective byte counts proportionally (preserving exact relative TP/EP/DP collective sizes) to
fit a ~350-450 us ns-3-ub simulation window at 400 Gbps.
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "simai_workload_analytical.txt")
OUT = os.path.join(HERE, "real_collective_trace.csv")

# Preserve exact byte ratios across collective ops (scale factor = 1/32 relative to 9216-GPU trace;
# individual simulation runs can scale further via --scale).
BYTE_DIVISOR = 32


def map_op_and_group(layer_name, comm_type):
    ct = comm_type.strip().upper()
    if ct == "ALLTOALL_EP":
        return "alltoall", "ep"
    if ct == "ALLTOALL":
        return "alltoall", "tp"
    if ct == "ALLREDUCE":
        return "allreduce", "dp"
    if ct == "ALLGATHER":
        grp = "tp" if "attention" in layer_name else "all"
        return "allgather", grp
    if ct == "REDUCESCATTER":
        grp = "tp" if "attention" in layer_name else "all"
        return "reducescatter", grp
    return None, None


def main():
    with open(SRC) as f:
        lines = [l.strip().split("\t") for l in f if l.strip()]
    rows = lines[2:]
    # Extract rows 7..16 (attention_column, attention_row, and full 7-step mlp_moelayer block)
    # plus row 6 (embedding_layer ALLREDUCE) as representative real-trace collective sequence.
    selected = [rows[i] for i in (7, 8, 9, 10, 11, 12, 13, 14, 15, 6)]
    out_rows = []
    ph = 1
    for r in selected:
        name = r[0]
        # Each SimAI row has forward (cols 2,3,4) and igrad (cols 5,6,7)
        for comp_col, comm_col, size_col in ((2, 3, 4), (5, 6, 7)):
            op, grp = map_op_and_group(name, r[comm_col])
            raw_bytes = int(r[size_col])
            raw_comp = int(r[comp_col])
            if op is None or raw_bytes <= 0:
                continue
            scaled_bytes = max(4096, ((raw_bytes // BYTE_DIVISOR + 4095) // 4096) * 4096)
            comp_ns = min(10000, max(2000, (raw_comp // 1000) * 1000)) if raw_comp > 1 else 2000
            out_rows.append((ph, op, scaled_bytes, grp, comp_ns, name, r[comm_col], raw_bytes))
            ph += 1

    with open(OUT, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["phase", "op", "size_bytes", "group", "compute_ns",
                    "simai_layer", "simai_comm_type", "raw_size_bytes"])
        for row in out_rows:
            w.writerow(row)
    print(f"Wrote {len(out_rows)} phases to {OUT}")


if __name__ == "__main__":
    main()
