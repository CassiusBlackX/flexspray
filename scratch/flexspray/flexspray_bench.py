#!/usr/bin/env python3
"""Benchmark v1 (round 12): multi-phase workloads with dependencies on the existing topology.

Every parameter and its source is listed in BENCHMARK.md (paper / report + section, "secondary"
if the original was not read, "assumption" otherwise). Tasks are 9-tuples
(src, dst, bytes, op, prio, delay_ns, cls, phase, deps): `phase` >= 1 is released when every task
of every phase in `deps` has completed, `delay_ns` after that (the simulator's traffic DAG,
UbTrafficGen phaseId / dependOnPhases); phase 0 is background traffic released at `delay_ns`.

Scenarios (all seeded, `scale` multiplies message sizes):
  b_ring  ring allreduce over the 32 NPUs = reduce-scatter (n-1 steps) + allgather (n-1 steps);
          each step is one phase in which every rank sends S/n to its ring successor.
  b_moe   DeepSeek-V3-style MoE decode layers over the 32 NPUs (EP32): per layer a dispatch phase
          (FP8 activations, one copy per (token, destination rank)), expert compute, a combine
          phase (BF16, the reverse pairs), attention compute, next layer.
  b_kv    Mooncake-trace-driven KVCache traffic: requests of the public trace are assigned to
          prefill NPUs; prefix blocks already seen in the trace are READ from the SSD that holds
          them, the request's new blocks are WRITTEN back after the prefill compute.
  b_mix   b_moe on NPU0-5 (EP24) + data-parallel ring allreduce on NPU6-7 + b_kv + control.
"""
import json
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
BULK_PRIO = 7
CTRL_PRIO = 2

# ---- parameters (BENCHMARK.md) ----
RING_BYTES = 32 << 20            # allreduce size S (assumption, sensitivity via scale)
MOE_HIDDEN = 7168                # DeepSeek-V3 §4.2
MOE_TOPK = 8                     # DeepSeek-V3 §4.2
MOE_EXPERTS = 256                # DeepSeek-V3 §4.2 (routed experts per MoE layer)
MOE_TOKENS_PER_RANK = 128        # DeepEP low-latency setting (secondary); DeepSeek-V3 §3.4.2: <= 256
MOE_LAYERS = 6                   # assumption (simulated window of the 58 MoE layers)
DISPATCH_BYTES_PER_ELEM = 1      # FP8 dispatch, DeepSeek-V3 §3.3.3
DISPATCH_SCALE_BYTES = MOE_HIDDEN // 128 * 4   # 1x128 tile scales, fp32 (secondary, see BENCHMARK)
COMBINE_BYTES_PER_ELEM = 2       # BF16 combine, DeepSeek-V3 §3.3.3
EXPERT_COMPUTE_NS = 10000        # assumption
ATTN_COMPUTE_NS = 10000          # assumption
KV_BLOCK_TOKENS = 512            # Mooncake §4.1
KV_BLOCK_BYTES = 64 << 10        # assumption: scaled from ~160 MB (LLaMA2-70B, derived) to fit
KV_TIME_SCALE_NS_PER_MS = 100000 / 3052  # assumption: one 3052 ms request batch of the trace -> 100 us
KV_REQUESTS = 48                 # assumption: first requests of the trace
KV_PREFILL_NS = 20000            # assumption
TRACE = os.environ.get("FLEX_MOONCAKE_TRACE",
                       "/home/cassius/ub/work/bench_src/mooncake/mooncake_trace.jsonl")


def _npus(t, idx=None):
    return [h for h in t.hosts if h["type"] == "NPU" and (idx is None or h["idx"] in idx)]


def ring_allreduce(t, scale, ranks, phase0, deps0=(), delay0=0, cls="ring"):
    """2(n-1) steps; step k depends on step k-1 (global barrier per step: an assumption, NCCL
    pipelines chunks per neighbour). Phases: phase0 .. phase0 + 2(n-1) - 1."""
    n = len(ranks)
    chunk = max(4096, int(RING_BYTES * scale / n))
    tasks = []
    for k in range(2 * (n - 1)):
        ph = phase0 + k
        deps = list(deps0) if k == 0 else [ph - 1]
        kind = "rs" if k < n - 1 else "ag"
        for r in range(n):
            tasks.append((ranks[r]["id"], ranks[(r + 1) % n]["id"], chunk, "URMA_WRITE", BULK_PRIO,
                          delay0 if k == 0 else 0, f"{cls}_{kind}", ph, deps))
    return tasks, phase0 + 2 * (n - 1)


def moe_layers(t, scale, ranks, phase0, seed, layers=MOE_LAYERS, cls="moe"):
    """Per layer: dispatch phase, combine phase. Routing: each token picks MOE_TOPK distinct experts
    uniformly (assumption), experts are placed MOE_EXPERTS / len(ranks) per rank; one copy per
    (token, destination rank) (DeepEP dispatches per rank, secondary)."""
    rng = random.Random(seed)
    n = len(ranks)
    per_rank_experts = MOE_EXPERTS // n
    tasks = []
    ph = phase0
    prev = []
    for layer in range(layers):
        pairs = {}
        for s in range(n):
            for _tok in range(MOE_TOKENS_PER_RANK):
                dsts = {e // per_rank_experts for e in rng.sample(range(per_rank_experts * n), MOE_TOPK)}
                for d in dsts:
                    if d != s:
                        pairs[(s, d)] = pairs.get((s, d), 0) + 1
        disp, comb = ph, ph + 1
        for (s, d), ntok in sorted(pairs.items()):
            b_disp = int(ntok * (MOE_HIDDEN * DISPATCH_BYTES_PER_ELEM + DISPATCH_SCALE_BYTES) * scale)
            b_comb = int(ntok * MOE_HIDDEN * COMBINE_BYTES_PER_ELEM * scale)
            tasks.append((ranks[s]["id"], ranks[d]["id"], b_disp, "URMA_WRITE", BULK_PRIO,
                          ATTN_COMPUTE_NS if layer else 0, f"{cls}_dispatch", disp, prev))
            tasks.append((ranks[d]["id"], ranks[s]["id"], b_comb, "URMA_WRITE", BULK_PRIO,
                          EXPERT_COMPUTE_NS, f"{cls}_combine", comb, [disp]))
        prev = [comb]
        ph += 2
    return tasks, ph


def kv_trace(t, scale, prefill, phase0, requests=KV_REQUESTS, cls="kv"):
    """Mooncake trace: request j on prefill[j % len]; prefix blocks seen before in the trace are
    read from SSD(block mod servers) (READ, data SSD -> NPU), new blocks written after prefill."""
    rows = [json.loads(l) for l in open(TRACE)][:requests]
    ssds = [h for h in t.hosts if h["type"] == "SSD"]
    seen = set()
    tasks = []
    ph = phase0
    blk = max(4096, int(KV_BLOCK_BYTES * scale))
    for j, r in enumerate(rows):
        npu = prefill[j % len(prefill)]
        rel = int(round(r["timestamp"] * KV_TIME_SCALE_NS_PER_MS))
        hits = [b for b in r["hash_ids"] if b in seen]
        new = [b for b in r["hash_ids"] if b not in seen]
        seen.update(r["hash_ids"])
        rd, wr = ph, ph + 1
        by_ssd_r, by_ssd_w = {}, {}
        for b in hits:
            by_ssd_r[b % len(ssds)] = by_ssd_r.get(b % len(ssds), 0) + 1
        for b in new:
            by_ssd_w[b % len(ssds)] = by_ssd_w.get(b % len(ssds), 0) + 1
        if by_ssd_r:
            for s, nb in sorted(by_ssd_r.items()):
                tasks.append((npu["id"], ssds[s]["id"], nb * blk, "URMA_READ", BULK_PRIO, rel,
                              f"{cls}_read", rd, []))
            wdeps, wdelay = [rd], KV_PREFILL_NS
        else:
            wdeps, wdelay = [], rel + KV_PREFILL_NS
        for s, nb in sorted(by_ssd_w.items()):
            tasks.append((npu["id"], ssds[s]["id"], nb * blk, "URMA_WRITE", BULK_PRIO, wdelay,
                          f"{cls}_write", wr, wdeps))
        ph += 2
    return tasks, ph


def ctrl_msgs(t, nodes, seed, count=16, period_ns=10000):
    """Small control writes (64 B - 1 KB, assumption) on the control priority, background phase 0."""
    rng = random.Random(seed + 7)
    tasks = []
    for a in nodes:
        for k in range(count):
            b = rng.choice([x for x in nodes if x["id"] != a["id"]])
            size = rng.choice([64, 256, 1024])
            tasks.append((a["id"], b["id"], size, "URMA_WRITE", CTRL_PRIO,
                          rng.randrange(period_ns) + k * period_ns, "ctrl", 0, []))
    return tasks


def b_ring(t, scale, seed=1):
    ranks = sorted(_npus(t), key=lambda h: (h["server"], h["idx"]))
    tasks, _ = ring_allreduce(t, scale, ranks, 1)
    return tasks + ctrl_msgs(t, ranks, seed)


def b_moe(t, scale, seed=1):
    ranks = sorted(_npus(t), key=lambda h: (h["server"], h["idx"]))
    tasks, _ = moe_layers(t, scale, ranks, 1, seed)
    return tasks + ctrl_msgs(t, ranks, seed)


def b_kv(t, scale, seed=1):
    prefill = sorted(_npus(t, {6, 7}), key=lambda h: (h["server"], h["idx"]))
    tasks, _ = kv_trace(t, scale, prefill, 1)
    return tasks + ctrl_msgs(t, prefill, seed)


def b_mix(t, scale, seed=1):
    ep = sorted(_npus(t, set(range(6))), key=lambda h: (h["server"], h["idx"]))
    dp = sorted(_npus(t, {6, 7}), key=lambda h: (h["server"], h["idx"]))
    tasks, ph = moe_layers(t, scale, ep, 1, seed)
    rt, ph = ring_allreduce(t, scale * 0.25, dp, ph)
    kt, ph = kv_trace(t, scale, dp, ph)
    return tasks + rt + kt + ctrl_msgs(t, ep + dp, seed)


BENCH = {"b_ring": b_ring, "b_moe": b_moe, "b_kv": b_kv, "b_mix": b_mix}
