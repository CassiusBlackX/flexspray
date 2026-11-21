#!/usr/bin/env python3
"""Open-source AI workload benchmarks (Stage 2, Round 16):

- b_aicb_tp_ep   : Megatron/AICB TP+EP+DP training iteration DAG derived from
                   aliyun/aicb (TP ReduceScatter/AllGather + EP Dispatch/Combine
                   with DeepSeek FP8_FACTOR asymmetry + DP AllReduce).
- b_deepep_skew  : DeepSeek-V3 / DeepEP MoE AlltoAll with Zipf (alpha=1.0) hot-expert
                   routing skew and FP8 dispatch / BF16 combine size asymmetry.
- b_deepep_prefill: DeepSeek-V3 bimodal Prefill-to-Decode transition (heavy Prefill
                   phases followed by short Decode bursts in one serving batch).
- b_nccl_pipe    : NCCL chunked neighbor-pipelined bidirectional Ring AllReduce
                   (concurrent multi-chunk flights without global per-hop barriers).
- b_kv_skew      : Mooncake-style (FAST'25) KVCache hot-prefix SSD read fan-in +
                   bursted KV offload writes (supports --scale 0.5 / 1.0 / 2.0).
"""
import math
import random

import flexspray_case as C

GAP_COMP_NS = 10000
GAP_PIPE_NS = 2000
FP8_FACTOR = (1.0 + 4.0 / 128.0) / 2.0  # MockedDeepSeek.FP8_FACTOR (~0.515625)


def _npus(t, idxs=None):
    out = [h for h in t.hosts if h["type"] == "NPU" and (idxs is None or h["idx"] in idxs)]
    return sorted(out, key=lambda h: (h["server"], h["idx"]))


def _ctrl(t, devs, total_us, seed):
    return C.wl_ctrl(t, devs, horizon_ns=int(total_us * 1000), seed=seed)


def _eff_planes(t):
    # Scale payload cleanly to topology port count (7 on cm_pod, 8 on ub_pod, 10 on ub_mesh)
    npus = _npus(t)
    return min(len(h["planes"]) for h in npus)


def _norm_bytes(rate_gbps, target_us, scale):
    return max(4096, int(rate_gbps * 1e9 / 8 * (target_us * 1e-6) * scale))


def b_aicb_tp_ep(t, scale, seed=1):
    """AICB-inspired hybrid TP(intra-server ring) + EP(AlltoAll) + DP(inter-server ring)."""
    npus = _npus(t)
    pl = _eff_planes(t)
    n_srv = t.S
    by_srv = {s: sorted([h for h in npus if h["server"] == s], key=lambda h: h["idx"]) for s in range(n_srv)}
    ep_pool = _npus(t, set(range(6)))  # keeps 24 NPUs in EP + NPU6..7 active in TP & DP
    per_npu_bw = pl * t.rate * C.EFF_BIDIR

    # TP ring bytes (8 ranks/server, bidirectional ring steps in 2 pipelined chunks)
    tp_bytes = _norm_bytes(per_npu_bw / 2.0, 22.0, scale)
    # EP AlltoAll bytes (23 peers): combine BF16 full, dispatch FP8_FACTOR
    comb_flow = _norm_bytes(per_npu_bw / max(1, len(ep_pool) - 1), 42.0, scale)
    disp_flow = max(4096, int(comb_flow * FP8_FACTOR))
    # DP AllReduce ring across servers
    dp_bytes = _norm_bytes(per_npu_bw / 2.0, 48.0, scale)

    tasks = []
    ph = 1
    deps = []
    layers = 4
    for _ in range(layers):
        # 1. TP intra-server Attention AllGather (bidirectional ring neighbor exchanges)
        for s in range(n_srv):
            grp = by_srv[s]
            m = len(grp)
            for k in range(m):
                u, v = grp[k]["id"], grp[(k + 1) % m]["id"]
                tasks.append((u, v, tp_bytes, "URMA_WRITE", C.BULK_PRIO, GAP_PIPE_NS if ph > 1 else 0,
                              "aicb_tp_ag", ph, list(deps)))
                tasks.append((v, u, tp_bytes, "URMA_WRITE", C.BULK_PRIO, GAP_PIPE_NS if ph > 1 else 0,
                              "aicb_tp_ag", ph, list(deps)))
        deps = [ph]; ph += 1

        # 2. EP Dispatch (FP8)
        for u in ep_pool:
            for v in ep_pool:
                if u["id"] != v["id"]:
                    tasks.append((u["id"], v["id"], disp_flow, "URMA_WRITE", C.BULK_PRIO, GAP_COMP_NS,
                                  "aicb_ep_disp", ph, list(deps)))
        deps = [ph]; ph += 1

        # 3. EP Combine (BF16)
        for u in ep_pool:
            for v in ep_pool:
                if u["id"] != v["id"]:
                    tasks.append((u["id"], v["id"], comb_flow, "URMA_WRITE", C.BULK_PRIO, GAP_COMP_NS,
                                  "aicb_ep_comb", ph, list(deps)))
        deps = [ph]; ph += 1

        # 4. TP intra-server MLP ReduceScatter
        for s in range(n_srv):
            grp = by_srv[s]
            m = len(grp)
            for k in range(m):
                u, v = grp[k]["id"], grp[(k + 1) % m]["id"]
                tasks.append((u, v, tp_bytes, "URMA_WRITE", C.BULK_PRIO, GAP_PIPE_NS,
                              "aicb_tp_rs", ph, list(deps)))
                tasks.append((v, u, tp_bytes, "URMA_WRITE", C.BULK_PRIO, GAP_PIPE_NS,
                              "aicb_tp_rs", ph, list(deps)))
        deps = [ph]; ph += 1

    # 5. Final DP AllReduce across servers per rank idx
    for idx in range(len(by_srv[0])):
        for s in range(n_srv):
            u = by_srv[s][idx]["id"]
            v = by_srv[(s + 1) % n_srv][idx]["id"]
            tasks.append((u, v, dp_bytes, "URMA_WRITE", C.BULK_PRIO, GAP_COMP_NS, "aicb_dp_ar", ph, list(deps)))
            tasks.append((v, u, dp_bytes, "URMA_WRITE", C.BULK_PRIO, GAP_COMP_NS, "aicb_dp_ar", ph, list(deps)))

    return tasks + _ctrl(t, npus, 550.0, seed)


def b_deepep_skew(t, scale, seed=1, alpha=1.0):
    """DeepSeek-V3 / DeepEP Zipf-skewed MoE dispatch (FP8) + combine (BF16) across 8 layers."""
    rng = random.Random(seed)
    npus = _npus(t, set(range(6)))
    n = len(npus)
    pl = _eff_planes(t)
    per_npu_bw = pl * t.rate * C.EFF_BIDIR
    total_comb_per_src = _norm_bytes(per_npu_bw, 10.0, scale)

    tasks = []
    ph = 1
    deps = []
    # Keep total destination weights Zipf-distributed (hotspot expert fan-in imbalance)
    raw_zipf = [1.0 / ((r + 1) ** alpha) for r in range(n)]
    zsum = sum(raw_zipf)
    base_w = [w / zsum for w in raw_zipf]

    for layer in range(8):
        order = list(range(n))
        rng.shuffle(order)
        # Ensure NPU(1,0) experiences skewed hotspot / peer roles consistently on layers 0..1
        dst_w = {npus[idx]["id"]: base_w[rank] for rank, idx in enumerate(order)}
        # Dispatch phase (FP8 skewed fan-in)
        d_delay = GAP_COMP_NS if ph > 1 else 0
        disp_sizes = {}
        for u in npus:
            peers = [v for v in npus if v["id"] != u["id"]]
            w_sum = sum(dst_w[v["id"]] for v in peers)
            for v in peers:
                cb = max(4096, int(total_comb_per_src * (dst_w[v["id"]] / w_sum)))
                db = max(4096, int(cb * FP8_FACTOR))
                disp_sizes[(u["id"], v["id"])] = cb
                tasks.append((u["id"], v["id"], db, "URMA_WRITE", C.BULK_PRIO, d_delay,
                              "deepep_disp", ph, list(deps)))
        deps = [ph]; ph += 1
        # Combine phase (reverse direction, BF16 size)
        for u in npus:
            for v in npus:
                if u["id"] != v["id"]:
                    cb = disp_sizes[(v["id"], u["id"])]
                    tasks.append((u["id"], v["id"], cb, "URMA_WRITE", C.BULK_PRIO, GAP_COMP_NS,
                                  "deepep_comb", ph, list(deps)))
        deps = [ph]; ph += 1

    return tasks + _ctrl(t, npus, 550.0, seed)


def b_deepep_prefill(t, scale, seed=1):
    """DeepSeek-V3 / AICB bimodal Prefill (large seqlen) + Decode (small batch) MoE."""
    rng = random.Random(seed)
    npus = _npus(t, set(range(6)))
    n = len(npus)
    pl = _eff_planes(t)
    per_npu_bw = pl * t.rate * C.EFF_BIDIR

    # 2 Prefill layers (~90 us disp + 160 us comb? Size to fit ~520 us total: 65 us / 120 us x 2 + 4 decode)
    pf_comb = _norm_bytes(per_npu_bw / (n - 1), 115.0, scale)
    pf_disp = max(4096, int(pf_comb * FP8_FACTOR))
    dc_comb = _norm_bytes(per_npu_bw / (n - 1), 22.0, scale)
    dc_disp = max(4096, int(dc_comb * FP8_FACTOR))

    tasks = []
    ph = 1
    deps = []
    # Prefill layers (heavy sequential AlltoAll)
    for l in range(2):
        dly = GAP_COMP_NS if ph > 1 else 0
        for u in npus:
            for v in npus:
                if u["id"] != v["id"]:
                    sz = max(4096, int(pf_disp * rng.uniform(0.85, 1.15)))
                    tasks.append((u["id"], v["id"], sz, "URMA_WRITE", C.BULK_PRIO, dly,
                                  "prefill_disp", ph, list(deps)))
        deps = [ph]; ph += 1
        for u in npus:
            for v in npus:
                if u["id"] != v["id"]:
                    sz = max(4096, int(pf_comb * rng.uniform(0.85, 1.15)))
                    tasks.append((u["id"], v["id"], sz, "URMA_WRITE", C.BULK_PRIO, GAP_COMP_NS,
                                  "prefill_comb", ph, list(deps)))
        deps = [ph]; ph += 1

    # Decode burst (small latency-sensitive MoE phases sharing the degraded/recovered state)
    for l in range(4):
        for u in npus:
            for v in npus:
                if u["id"] != v["id"]:
                    sz = max(4096, int(dc_disp * rng.uniform(0.8, 1.2)))
                    tasks.append((u["id"], v["id"], sz, "URMA_WRITE", C.BULK_PRIO, GAP_COMP_NS,
                                  "decode_disp", ph, list(deps)))
        deps = [ph]; ph += 1
        for u in npus:
            for v in npus:
                if u["id"] != v["id"]:
                    sz = max(4096, int(dc_comb * rng.uniform(0.8, 1.2)))
                    tasks.append((u["id"], v["id"], sz, "URMA_WRITE", C.BULK_PRIO, GAP_COMP_NS,
                                  "decode_comb", ph, list(deps)))
        deps = [ph]; ph += 1

    return tasks + _ctrl(t, npus, 550.0, seed)


def b_nccl_pipe(t, scale, seed=1):
    """NCCL pipelined chunked Ring AllReduce: continuous multi-chunk neighbor pipelines
    without global step-by-step barrier bubbles (bidirectional dual-ring)."""
    npus = _npus(t)
    n = len(npus)
    pl = _eff_planes(t)
    # Two opposite rings share full port bandwidth (fwd TX/RX + rev TX/RX -> full BIDIR per ring = bw/2)
    per_ring_bw = pl * t.rate * C.EFF_BIDIR / 2.0
    # 6 macro pipeline stages x ~65 us each = ~400 us continuous streaming per iteration
    stage_bytes = _norm_bytes(per_ring_bw, 65.0, scale)

    tasks = []
    ph = 1
    deps = []
    for st in range(6):
        dly = GAP_PIPE_NS if ph > 1 else 0
        # 2 concurrent multi-chunk slices per neighbor direction mimic NCCL multi-channel double buffering
        chunk = max(4096, stage_bytes // 2)
        for _slice in range(2):
            for k in range(n):
                u = npus[k]["id"]
                vf = npus[(k + 1) % n]["id"]
                vr = npus[(k - 1) % n]["id"]
                cls = "nccl_rs_pipe" if st < 3 else "nccl_ag_pipe"
                tasks.append((u, vf, chunk, "URMA_WRITE", C.BULK_PRIO, dly, cls, ph, list(deps)))
                tasks.append((u, vr, chunk, "URMA_WRITE", C.BULK_PRIO, dly, cls, ph, list(deps)))
        deps = [ph]; ph += 1

    return tasks + _ctrl(t, npus, 420.0, seed)


def b_kv_skew(t, scale, seed=1):
    """Mooncake-style (FAST'25) KVCache skewed hot-prefix read pulls + async KV evictions."""
    rng = random.Random(seed)
    npus = _npus(t, {6, 7})
    ssds = sorted([h for h in t.hosts if h["type"] == "SSD"], key=lambda h: h["server"])
    ssd_pl = len(ssds[0]["planes"])
    # Each SSD has ssd_pl (2 ports * 400G) line rate
    ssd_bw = ssd_pl * t.rate * C.EFF_UNI
    # Hot SSD0 absorbs 60% of bursty requests, SSD1..3 absorb 40%; 4 rounds x 80 us + gaps
    hot_ssd = ssds[0]
    other_ssds = ssds[1:]

    tasks = []
    ph = 1
    deps = []
    # Split total read/write per round across all npus
    read_round_bytes = _norm_bytes(ssd_bw, 70.0, scale)
    write_round_bytes = _norm_bytes(ssd_bw, 25.0, scale)

    for rnd in range(4):
        dly = 20000 if ph > 1 else 0
        hot_readers = rng.sample(npus, max(4, len(npus) // 2))
        per_hot = max(4096, read_round_bytes // len(hot_readers))
        for h in hot_readers:
            tasks.append((h["id"], hot_ssd["id"], per_hot, "URMA_READ", C.BULK_PRIO, dly,
                          "kv_read_hot", ph, list(deps)))
        for h in npus:
            dst = rng.choice(other_ssds) if other_ssds else hot_ssd
            tasks.append((h["id"], dst["id"], max(4096, write_round_bytes // max(1, len(npus) // max(1, len(other_ssds)))),
                          "URMA_WRITE", C.BULK_PRIO, dly, "kv_write_offload", ph, list(deps)))
        deps = [ph]; ph += 1

    return tasks + _ctrl(t, npus, 450.0, seed)


AICB_BENCH = {
    "b_aicb_tp_ep": b_aicb_tp_ep,
    "b_deepep_skew": b_deepep_skew,
    "b_deepep_prefill": b_deepep_prefill,
    "b_nccl_pipe": b_nccl_pipe,
    "b_kv_skew": b_kv_skew,
}
