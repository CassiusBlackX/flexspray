#!/usr/bin/env python3
"""Public supernode topologies for FlexSpray Stage 2 evaluation:

1. cm_pod / cm_pod_osub: CloudMatrix384-style 7-plane heterogeneous UB supernode
   (arXiv:2506.12708 §3.3). Published full supernode has 48 compute nodes
   (384 Ascend 910 NPUs + 192 Kunpeng CPUs) across 7 independent UB sub-planes
   with 16 L2 switches per sub-plane (1:1 non-blocking L1->L2). Here scaled by
   1/12 in node count (S=4 nodes = 32 NPUs + 16 CPUs + 4 NIC/DPU + 4 SSD)
   while preserving exact 7-plane NPU attachment, 2:1 NPU:CPU node ratio, and
   heterogeneous plane participation:
     * cm_pod     : K=4 L2 spines, J=4 links/pair (16 up >= 13 down, 1:1 non-blocking)
     * cm_pod_osub: K=3 L2 spines, J=2 links/pair (6 up vs 13 down, ~2.17:1 oversubscribed)

2. ub_mesh: UB-Mesh direct-connect 2D-FullMesh + Low-Radix Switch (LRS) hybrid
   architecture (arXiv:2503.20377 §3.2-3.3). S=4 boards x 8 NPUs = 32 NPUs
   (+ 8 CPUs + 4 NICs + 4 SSDs). Each NPU has 6 direct 1-hop mesh links
   (3 intra-board 4-NPU group cables + 3 cross-board same-rank cables) plus
   4 LRS switch rails (10 ports total = 4.0 Tbps/NPU, matching >3.2 Tbps spec).
   LRS switches connect across boards via direct LRS-to-LRS inter-board bundles
   (lrs_links=3 parallel 400G links per board pair per rail) with no L2 spine tier.
"""
import collections


CM_DEVICE_TYPES = [
    ("NPU", 8, list(range(7))),   # 7 UB sub-planes per Ascend 910 NPU
    ("CPU", 4, [0, 1, 2, 3]),     # 4 Kunpeng CPUs per node (shared subset of UB planes)
    ("NIC", 1, [0, 1]),           # Qingtian DPU / VPC-RDMA plane interface
    ("SSD", 1, [5, 6]),           # disaggregated storage pool planes
]

UBMESH_DEVICE_TYPES = [
    ("NPU", 8, [0, 1, 2, 3]),     # 4 LRS switch rails (+ 6 direct 1-hop mesh ports added below)
    ("CPU", 2, [0, 1]),
    ("NIC", 1, [0, 1]),
    ("SSD", 1, [2, 3]),
]


class _BasePublicTopo:
    def device_counts(self):
        cnt = collections.Counter()
        for h in self.hosts:
            if h["server"] == 0:
                cnt[h["type"]] += 1
        return dict(cnt)

    def node_ports(self, n):
        return self._nports[n]

    def find_host(self, typ, server, idx):
        for h in self.hosts:
            if h["type"] == typ and h["server"] == server and h["idx"] == idx:
                return h
        raise KeyError((typ, server, idx))


class CloudMatrixPod(_BasePublicTopo):
    """CloudMatrix384-style 7-plane 2-tier UB Clos pod (scaled S=4 nodes = 32 NPUs)."""

    def __init__(self, topo_name="cm_pod", servers=4, oversub=False, rate_gbps=400,
                 host_link_ns=20, fabric_link_ns=100, switch_fwd_ns=100, lanes=4):
        self.topo_name = topo_name
        self.S = servers
        self.P = 7
        self.K, self.J = (3, 2) if oversub else (4, 4)
        self.rate, self.lanes = rate_gbps, lanes
        self.host_link_ns, self.fabric_link_ns, self.switch_fwd_ns = (
            host_link_ns, fabric_link_ns, switch_fwd_ns)
        self.hosts = []
        for s in range(servers):
            for typ, cnt, pls in CM_DEVICE_TYPES:
                for i in range(cnt):
                    pls_ = [p for p in pls if p < self.P]
                    self.hosts.append(dict(
                        id=len(self.hosts), type=typ, idx=i, server=s, planes=pls_,
                        ports={p: k for k, p in enumerate(pls_)},
                    ))
        self.nh = len(self.hosts)
        self.l1 = {(p, s): self.nh + p * servers + s for p in range(self.P) for s in range(servers)}
        base = self.nh + self.P * servers
        self.l2 = {(p, k): base + p * self.K + k for p in range(self.P) for k in range(self.K)}
        self.nnodes = base + self.P * self.K
        self.l1_down = {
            (p, s): [(h["id"], h["ports"][p]) for h in self.hosts if h["server"] == s and p in h["ports"]]
            for (p, s) in self.l1
        }
        self.links = []
        self.l1_up_port = {}
        self.l2_port = {}
        self._nports = {h["id"]: len(h["ports"]) for h in self.hosts}
        for (p, s), sw in self.l1.items():
            down = self.l1_down[(p, s)]
            for port, (h, hp) in enumerate(down):
                self.links.append((h, hp, sw, port, host_link_ns))
            for k in range(self.K):
                for j in range(self.J):
                    up = len(down) + k * self.J + j
                    self.l1_up_port[(p, s, k, j)] = up
                    l2p = s * self.J + j
                    self.l2_port[(p, k, s, j)] = l2p
                    self.links.append((sw, up, self.l2[(p, k)], l2p, fabric_link_ns))
            self._nports[sw] = len(down) + self.K * self.J
        for (_, _), sw in self.l2.items():
            self._nports[sw] = self.S * self.J

    def write(self, out):
        import os
        with open(os.path.join(out, "node.csv"), "w") as f:
            f.write("nodeId,nodeType,portNum,allocationDelay,forwardDelay\n")
            for h in self.hosts:
                f.write(f"{h['id']},DEVICE,{len(h['ports'])},1ns,\n")
            for n in range(self.nh, self.nnodes):
                f.write(f"{n},SWITCH,{self.node_ports(n)},1ns,{self.switch_fwd_ns}ns\n")
        with open(os.path.join(out, "topology.csv"), "w") as f:
            f.write("nodeId1,portId1,nodeId2,portId2,bandwidth,delay\n")
            for n1, p1, n2, p2, d in self.links:
                f.write(f"{n1},{p1},{n2},{p2},{self.rate}Gbps,{d}ns\n")
        rows = []
        for a in self.hosts:
            for b in self.hosts:
                if a["id"] == b["id"]:
                    continue
                for p in a["planes"]:
                    if p in b["ports"]:
                        rows.append((a["id"], b["id"], b["ports"][p], [a["ports"][p]],
                                     2 if a["server"] == b["server"] else 4))
        for (p, s), sw in self.l1.items():
            local = {h: port for port, (h, _hp) in enumerate(self.l1_down[(p, s)])}
            ups = [self.l1_up_port[(p, s, k, j)] for k in range(self.K) for j in range(self.J)]
            for d in self.hosts:
                if p not in d["ports"]:
                    continue
                if d["server"] == s:
                    rows.append((sw, d["id"], d["ports"][p], [local[d["id"]]], 1))
                else:
                    rows.append((sw, d["id"], d["ports"][p], ups, 3))
        for (p, k), sw in self.l2.items():
            for d in self.hosts:
                if p not in d["ports"]:
                    continue
                ports = [self.l2_port[(p, k, d["server"], j)] for j in range(self.J)]
                rows.append((sw, d["id"], d["ports"][p], ports, 2))
        with open(os.path.join(out, "routing_table.csv"), "w") as f:
            f.write("nodeId,dstNodeId,dstPortId,outPorts,metrics\n")
            for n, d, dp, outs, m in rows:
                f.write(f"{n},{d},{dp},{' '.join(map(str, outs))},{' '.join([str(m)] * len(outs))}\n")

    def meta(self):
        return dict(
            topo=self.topo_name,
            paper_ref="arXiv:2506.12708 (CloudMatrix384, scaled 1/12 nodes: 4x8=32 NPUs, 7 planes)",
            servers=self.S, planes=self.P, spines=self.K, links_per_pair=self.J,
            rate_gbps=self.rate, lanes=self.lanes, hosts=self.hosts,
            l1={f"{p},{s}": n for (p, s), n in self.l1.items()},
            l2={f"{p},{k}": n for (p, k), n in self.l2.items()},
            l1_up_port={f"{p},{s},{k},{j}": v for (p, s, k, j), v in self.l1_up_port.items()},
            nnodes=self.nnodes,
        )


class UbMeshPod(_BasePublicTopo):
    """UB-Mesh 2D direct-connect full-mesh + Low-Radix Switch (LRS) hybrid pod."""

    def __init__(self, servers=4, lrs_links=3, rate_gbps=400, host_link_ns=20,
                 cross_direct_ns=40, lrs_link_ns=100, switch_fwd_ns=100, lanes=4):
        assert servers == 4, "ub_mesh public pod is fixed to 4 boards x 8 NPUs (32 NPUs)"
        self.topo_name = "ub_mesh"
        self.S = servers
        self.P = 4             # 4 LRS switch rails (planes 0..3)
        self.K = 0             # no L2 spine switches (direct LRS-to-LRS mesh)
        self.J = lrs_links     # parallel 400G links per (board, peer_board) per LRS rail
        self.rate, self.lanes = rate_gbps, lanes
        self.host_link_ns = host_link_ns
        self.cross_direct_ns = cross_direct_ns
        self.lrs_link_ns = lrs_link_ns
        self.switch_fwd_ns = switch_fwd_ns

        self.hosts = []
        for s in range(servers):
            for typ, cnt, pls in UBMESH_DEVICE_TYPES:
                for i in range(cnt):
                    pls_ = list(pls)
                    self.hosts.append(dict(
                        id=len(self.hosts), type=typ, idx=i, server=s, planes=pls_,
                        ports={p: k for k, p in enumerate(pls_)},
                    ))
        self.nh = len(self.hosts)

        # Direct 2D NPU mesh edges: intra-board 4-NPU groups ({0..3}, {4..7}) + cross-board columns
        npu_grid = {(h["server"], h["idx"]): h for h in self.hosts if h["type"] == "NPU"}
        direct_pairs = []
        # Intra-board 1D full-mesh per 4-NPU quad
        for s in range(servers):
            for base_i in (0, 4):
                for i in range(4):
                    for j in range(i + 1, 4):
                        direct_pairs.append((npu_grid[(s, base_i + i)], npu_grid[(s, base_i + j)], host_link_ns))
        # Inter-board column full-mesh across boards 0..3
        for idx in range(8):
            for s1 in range(servers):
                for s2 in range(s1 + 1, servers):
                    direct_pairs.append((npu_grid[(s1, idx)], npu_grid[(s2, idx)], cross_direct_ns))

        self.links = []
        self.direct_planes = {}  # pl -> (u_id, v_id, delay_ns)
        for eidx, (u, v, dly) in enumerate(direct_pairs):
            pl = self.P + eidx
            up, vp = len(u["ports"]), len(v["ports"])
            u["planes"].append(pl)
            v["planes"].append(pl)
            u["ports"][pl] = up
            v["ports"][pl] = vp
            self.direct_planes[pl] = (u["id"], v["id"], dly)
            self.links.append((u["id"], up, v["id"], vp, dly))

        # LRS switches: one LRS per (rail p, board s)
        self.l1 = {(p, s): self.nh + p * servers + s for p in range(self.P) for s in range(servers)}
        self.l2 = {}
        self.nnodes = self.nh + self.P * servers

        self._nports = {h["id"]: len(h["ports"]) for h in self.hosts}
        self.l1_down = {}
        self.lrs_inter_ports = collections.defaultdict(list)  # (p, b1, b2) -> [port on LRS(p, b1)]
        self.l1_up_port = {}  # (p, s, peer_s, j) -> port on LRS(p, s)
        self.lrs_port_to_bundle = {}  # (sw_id, port) -> (p, s, peer_s)

        for (p, s), sw in self.l1.items():
            down = [(h["id"], h["ports"][p]) for h in self.hosts if h["server"] == s and p in h["ports"]]
            self.l1_down[(p, s)] = down
            for port, (hid, hport) in enumerate(down):
                self.links.append((hid, hport, sw, port, host_link_ns))
            self._nports[sw] = len(down)

        # Inter-board LRS-to-LRS links (symmetric undirected)
        for p in range(self.P):
            for s1 in range(servers):
                for s2 in range(s1 + 1, servers):
                    sw1, sw2 = self.l1[(p, s1)], self.l1[(p, s2)]
                    for j in range(self.J):
                        p1 = self._nports[sw1]
                        p2 = self._nports[sw2]
                        self._nports[sw1] += 1
                        self._nports[sw2] += 1
                        self.lrs_inter_ports[(p, s1, s2)].append(p1)
                        self.lrs_inter_ports[(p, s2, s1)].append(p2)
                        self.l1_up_port[(p, s1, s2, j)] = p1
                        self.l1_up_port[(p, s2, s1, j)] = p2
                        self.lrs_port_to_bundle[f"{sw1},{p1}"] = [p, s1, s2]
                        self.lrs_port_to_bundle[f"{sw2},{p2}"] = [p, s2, s1]
                        self.links.append((sw1, p1, sw2, p2, lrs_link_ns))

    def write(self, out):
        import os
        with open(os.path.join(out, "node.csv"), "w") as f:
            f.write("nodeId,nodeType,portNum,allocationDelay,forwardDelay\n")
            for h in self.hosts:
                f.write(f"{h['id']},DEVICE,{len(h['ports'])},1ns,\n")
            for n in range(self.nh, self.nnodes):
                f.write(f"{n},SWITCH,{self.node_ports(n)},1ns,{self.switch_fwd_ns}ns\n")
        with open(os.path.join(out, "topology.csv"), "w") as f:
            f.write("nodeId1,portId1,nodeId2,portId2,bandwidth,delay\n")
            for n1, p1, n2, p2, d in self.links:
                f.write(f"{n1},{p1},{n2},{p2},{self.rate}Gbps,{d}ns\n")

        rows = []
        for a in self.hosts:
            for b in self.hosts:
                if a["id"] == b["id"]:
                    continue
                for p in a["planes"]:
                    if p not in b["ports"]:
                        continue
                    if p >= self.P:
                        hop = 1
                    elif a["server"] == b["server"]:
                        hop = 2
                    else:
                        hop = 3
                    rows.append((a["id"], b["id"], b["ports"][p], [a["ports"][p]], hop))

        # LRS routing: local hosts take downlink (metric 1); remote board hosts take J LRS-to-LRS links (metric 2)
        for (p, s), sw in self.l1.items():
            local = {h: port for port, (h, _hp) in enumerate(self.l1_down[(p, s)])}
            for d in self.hosts:
                if p not in d["ports"]:
                    continue
                if d["server"] == s:
                    rows.append((sw, d["id"], d["ports"][p], [local[d["id"]]], 1))
                else:
                    outs = self.lrs_inter_ports[(p, s, d["server"])]
                    rows.append((sw, d["id"], d["ports"][p], outs, 2))

        with open(os.path.join(out, "routing_table.csv"), "w") as f:
            f.write("nodeId,dstNodeId,dstPortId,outPorts,metrics\n")
            for n, d, dp, outs, m in rows:
                f.write(f"{n},{d},{dp},{' '.join(map(str, outs))},{' '.join([str(m)] * len(outs))}\n")

    def meta(self):
        return dict(
            topo=self.topo_name,
            paper_ref="arXiv:2503.20377 (UB-Mesh, 4x8=32 NPUs direct 2D mesh + 4 LRS switch rails)",
            servers=self.S, planes=self.P, spines=0, links_per_pair=self.J,
            p_lrs=self.P, lrs_links=self.J,
            rate_gbps=self.rate, lanes=self.lanes, hosts=self.hosts,
            l1={f"{p},{s}": n for (p, s), n in self.l1.items()},
            l2={},
            l1_up_port={f"{p},{s},{ps},{j}": v for (p, s, ps, j), v in self.l1_up_port.items()},
            lrs_port_to_bundle=self.lrs_port_to_bundle,
            nnodes=self.nnodes,
        )


def build_topo(topo_name, servers=4, links_per_pair=None, rate_gbps=400, lanes=4):
    if topo_name == "cm_pod":
        return CloudMatrixPod(topo_name="cm_pod", servers=servers, oversub=False,
                              rate_gbps=rate_gbps, lanes=lanes)
    if topo_name == "cm_pod_osub":
        return CloudMatrixPod(topo_name="cm_pod_osub", servers=servers, oversub=True,
                              rate_gbps=rate_gbps, lanes=lanes)
    if topo_name == "ub_mesh":
        return UbMeshPod(servers=servers, lrs_links=(links_per_pair or 3),
                         rate_gbps=rate_gbps, lanes=lanes)
    raise KeyError(topo_name)
