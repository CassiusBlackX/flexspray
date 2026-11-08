#!/usr/bin/env python3
"""Generate multi-plane UB super-node cases for the FlexSpray load-balancing study.

Topology (defaults = the "medium" study scale):
  * S servers; each server hosts heterogeneous devices with different plane counts:
      NPU  x8 : one 400G port on each of planes 0..7
      CPU  x2 : one 400G port on each of planes 0..3
      NIC  x1 : planes 0,1
      SSD  x1 : planes 4,5   (storage pool / KVCache)
  * Each plane is an independent 2-tier Clos: one L1 switch per server per plane, and
    `spines` L2 switches per plane; every L1 connects to every L2 of its plane with
    `links_per_pair` parallel links (non-blocking: 4x3 = 12 uplinks >= 11 downlinks).
  * Hosts never forward. Host routing rows pin destination port on plane p to the host's own
    plane-p port, so TP groups are plane-matched by construction and a CPU<->NPU pair only uses
    their 4 common planes.

Usage:
  flexspray_case.py --out DIR --scenario {d2d,h2d,a2a,mixed} --fault F0 --scheme flexspray
See SCENARIOS / FAULTS / SCHEMES below.
"""
import argparse
import json
import os
import random
import sys

MTU = 4096
DATA_HDR = 78          # URMA_WRITE header bytes per 4 KB packet
ACK_BYTES = {"none": 62, "dcqcn": 62, "caqm": 70, "spraycc": 70}

DEVICE_TYPES = [
    # name, count per server, planes
    ("NPU", 8, list(range(8))),
    ("CPU", 2, [0, 1, 2, 3]),
    ("NIC", 1, [0, 1]),
    ("SSD", 1, [4, 5]),
]


class Topo:
    def __init__(self, servers=4, planes=8, spines=4, links_per_pair=3, rate_gbps=400,
                 host_link_ns=20, fabric_link_ns=100, switch_fwd_ns=100, lanes=4):
        self.S, self.P, self.K, self.J = servers, planes, spines, links_per_pair
        self.rate, self.lanes = rate_gbps, lanes
        self.host_link_ns, self.fabric_link_ns, self.switch_fwd_ns = (
            host_link_ns, fabric_link_ns, switch_fwd_ns)
        self.hosts = []           # dict(id, type, idx, server, planes, ports{plane:port})
        for s in range(servers):
            for typ, cnt, pls in DEVICE_TYPES:
                for i in range(cnt):
                    pls_ = [p for p in pls if p < planes]
                    h = dict(id=len(self.hosts), type=typ, idx=i, server=s, planes=pls_,
                             ports={p: k for k, p in enumerate(pls_)})
                    self.hosts.append(h)
        self.nh = len(self.hosts)
        self.l1 = {(p, s): self.nh + p * servers + s for p in range(planes) for s in range(servers)}
        base = self.nh + planes * servers
        self.l2 = {(p, k): base + p * spines + k for p in range(planes) for k in range(spines)}
        self.nnodes = base + planes * spines
        # L1 port map: downlinks to hosts of that server/plane in host order, then uplinks.
        self.l1_down = {}  # (p,s) -> list of (host, hostport)
        for (p, s), _ in self.l1.items():
            self.l1_down[(p, s)] = [(h["id"], h["ports"][p]) for h in self.hosts
                                    if h["server"] == s and p in h["ports"]]
        self.links = []    # (n1,p1,n2,p2,delay_ns)
        self.l1_up_port = {}  # (p,s,k,j) -> port on L1
        self.l2_port = {}     # (p,k,s,j) -> port on L2
        for (p, s), sw in self.l1.items():
            down = self.l1_down[(p, s)]
            for port, (h, hp) in enumerate(down):
                self.links.append((h, hp, sw, port, host_link_ns))
            for k in range(spines):
                for j in range(links_per_pair):
                    up = len(down) + k * links_per_pair + j
                    self.l1_up_port[(p, s, k, j)] = up
                    l2p = s * links_per_pair + j
                    self.l2_port[(p, k, s, j)] = l2p
                    self.links.append((sw, up, self.l2[(p, k)], l2p, fabric_link_ns))

    def node_ports(self, n):
        if n < self.nh:
            return len(self.hosts[n]["ports"])
        for (p, s), sw in self.l1.items():
            if sw == n:
                return len(self.l1_down[(p, s)]) + self.K * self.J
        return self.S * self.J

    def find_host(self, typ, server, idx):
        for h in self.hosts:
            if h["type"] == typ and h["server"] == server and h["idx"] == idx:
                return h
        raise KeyError((typ, server, idx))

    # ---------------- writers ----------------
    def write(self, out):
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
        for h in self.hosts:
            for d in self.hosts:
                if d["id"] == h["id"]:
                    continue
                for p, dp in d["ports"].items():
                    if p in h["ports"]:
                        hops = 2 if d["server"] == h["server"] else 4
                        rows.append((h["id"], d["id"], dp, [h["ports"][p]], hops))
        for (p, s), sw in self.l1.items():
            ups = [self.l1_up_port[(p, s, k, j)] for k in range(self.K) for j in range(self.J)]
            local = {h: port for port, (h, hp) in enumerate(self.l1_down[(p, s)])}
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
        return dict(servers=self.S, planes=self.P, spines=self.K, links_per_pair=self.J,
                    rate_gbps=self.rate, lanes=self.lanes, hosts=self.hosts,
                    l1={f"{p},{s}": n for (p, s), n in self.l1.items()},
                    l2={f"{p},{k}": n for (p, k), n in self.l2.items()},
                    l1_up_port={f"{p},{s},{k},{j}": v for (p, s, k, j), v in self.l1_up_port.items()},
                    nnodes=self.nnodes)


# ---------------- workloads ----------------
# Each task: (src, dst, bytes, op, priority, delay_ns, cls). Bulk sizes are rate x T_TARGET so that,
# in an ideal network, every flow of a scenario finishes together; flexspray_analyze.py computes the
# exact ideal makespan with an LP and reports measured efficiency against it.
BULK_PRIO = 7
CTRL_PRIO = 2
T_TARGET_US = 160.0
EFF_BIDIR = MTU / (MTU + DATA_HDR + 62)     # data out + ACKs for data in on the same port
EFF_UNI = MTU / (MTU + DATA_HDR)


def _bytes(rate_gbps, scale):
    return int(rate_gbps * 1e9 / 8 * T_TARGET_US * 1e-6 * scale)


def wl_d2d(t, scale):
    """Cross-server NPU permutation (D2D): NPU i of server s -> NPU i of server s+1."""
    r = 8 * t.rate * EFF_BIDIR
    return [(t.find_host("NPU", s, i)["id"], t.find_host("NPU", (s + 1) % t.S, i)["id"],
             _bytes(r, scale), "URMA_WRITE", BULK_PRIO, 0, "d2d")
            for s in range(t.S) for i in range(8)]


def wl_d2d_read(t, scale):
    """Cross-server NPU permutation with URMA_READ: NPU i of server s reads from NPU i of server
    s+1 (data flows responder -> requester)."""
    r = 8 * t.rate * EFF_BIDIR
    return [(t.find_host("NPU", s, i)["id"], t.find_host("NPU", (s + 1) % t.S, i)["id"],
             _bytes(r, scale), "URMA_READ", BULK_PRIO, 0, "d2d_read")
            for s in range(t.S) for i in range(8)]


def p2p_io_flows(t, scale):
    """Host/IO point-to-point set with exclusive endpoints (all device ports demanded at line rate):
    H2D CPU0->NPU6, RH2D CPU1->NPU7(next server), D2H NPU6->CPU1, NIC->CPU0 (network to host),
    NPU7->NIC (device to network), KVCache read NPU6<-SSD(next server), KVCache write NPU7->SSD."""
    R = t.rate * EFF_UNI
    tasks = []
    for s in range(t.S):
        n = (s + 1) % t.S
        cpu0, cpu1 = t.find_host("CPU", s, 0), t.find_host("CPU", s, 1)
        nic, ssd = t.find_host("NIC", s, 0), t.find_host("SSD", s, 0)
        npu6, npu7 = t.find_host("NPU", s, 6), t.find_host("NPU", s, 7)
        tasks += [
            (cpu0["id"], npu6["id"], _bytes(4 * R, scale), "URMA_WRITE", BULK_PRIO, 0, "h2d"),
            (cpu1["id"], t.find_host("NPU", n, 7)["id"], _bytes(4 * R, scale), "URMA_WRITE", BULK_PRIO, 0, "rh2d"),
            (npu6["id"], cpu1["id"], _bytes(4 * R, scale), "URMA_WRITE", BULK_PRIO, 0, "d2h"),
            (nic["id"], cpu0["id"], _bytes(2 * R, scale), "URMA_WRITE", BULK_PRIO, 0, "nic2h"),
            (npu7["id"], nic["id"], _bytes(2 * R, scale), "URMA_WRITE", BULK_PRIO, 0, "d2nic"),
            (npu6["id"], t.find_host("SSD", n, 0)["id"], _bytes(2 * R, scale), "URMA_READ", BULK_PRIO, 0, "kv_read"),
            (npu7["id"], ssd["id"], _bytes(2 * R, scale), "URMA_WRITE", BULK_PRIO, 0, "kv_write"),
        ]
    return tasks


def wl_h2d(t, scale):
    """Heterogeneous point-to-point traffic only (H2D/RH2D/D2H/NIC/KVCache)."""
    return p2p_io_flows(t, scale)


def wl_a2a(t, scale, npus=None, rate_per_npu=None, op="URMA_WRITE"):
    """Alltoall: every NPU posts all its sends concurrently (no per-phase barriers)."""
    npus = npus or [h for h in t.hosts if h["type"] == "NPU"]
    k = len(npus) - 1
    r = (rate_per_npu if rate_per_npu is not None else 8 * t.rate * EFF_BIDIR) / k
    return [(a["id"], b["id"], _bytes(r, scale), op, BULK_PRIO, 0, "a2a")
            for a in npus for b in npus if a["id"] != b["id"]]


def wl_a2a_read(t, scale):
    """Alltoall built from URMA_READ (every NPU pulls its shard from every other NPU)."""
    return wl_a2a(t, scale, op="URMA_READ")


def wl_mixed_read(t, scale, seed=1):
    """Read-dominant mix: alltoall among NPU0..5 with URMA_READ, the P2P IO set, a KVCache *read*
    hotspot (NPU4/NPU5 of servers 1..S-1 read from SSD0, so SSD0's 2-plane egress is the hotspot)
    and control writes."""
    a2a_npus = [h for h in t.hosts if h["type"] == "NPU" and h["idx"] < 6]
    readers = [t.find_host("NPU", s, i) for s in range(1, t.S) for i in (4, 5)]
    ssd0 = t.find_host("SSD", 0, 0)
    kv_rate = 2 * t.rate * EFF_UNI / len(readers)
    tasks = wl_a2a(t, scale, npus=a2a_npus, rate_per_npu=8 * t.rate * EFF_BIDIR - kv_rate,
                   op="URMA_READ")
    # SSD0's egress is the hotspot: drop the regular KV read that targets SSD0.
    tasks += [x for x in p2p_io_flows(t, scale) if not (x[6] == "kv_read" and x[1] == ssd0["id"])]
    tasks += [(h["id"], ssd0["id"], _bytes(kv_rate, scale), "URMA_READ", BULK_PRIO, 0, "kv_read_hot")
              for h in readers]
    tasks += wl_ctrl(t, a2a_npus, seed=seed)
    return tasks


def wl_stagger(t, scale, seed=1, levels=8, step_ns=5000):
    """Alltoall among all NPUs with unequal sizes (x0.25..x2, log-uniform) and staggered starts
    (each flow released at one of `levels` times, step_ns apart). The LP bound handles the release
    times exactly (time-indexed phases)."""
    rng = random.Random(seed)
    base = wl_a2a(t, scale)
    out = []
    for (a, b, n, op, pr, _d, c) in base:
        f = 2 ** rng.uniform(-2, 1)
        out.append((a, b, int(n * f), op, pr, rng.randrange(levels) * step_ns, "a2a_stagger"))
    return out


def wl_stagger_mixed(t, scale, seed=1, levels=8, step_ns=5000):
    """The mixed workload with unequal sizes and staggered starts for every bulk flow."""
    rng = random.Random(seed)
    out = []
    for (a, b, n, op, pr, d, c) in wl_mixed(t, scale, seed=seed):
        if c == "ctrl":
            out.append((a, b, n, op, pr, d, c))
            continue
        f = 2 ** rng.uniform(-2, 1)
        out.append((a, b, int(n * f), op, pr, rng.randrange(levels) * step_ns, c))
    return out


def wl_incast(t, scale, seed=1, oversub=4.0):
    """Congestion-tree test. Victims: alltoall among NPU0..5 of every server at full rate.
    Culprits: NPU6/NPU7 of every server write to SSD0 (planes 4,5 only), with `oversub` times the
    bytes SSD0 can absorb in the victims' ideal time, so the incast outlives the alltoall and its
    backpressure (CBFC) can spread into the shared L2->L1 links of planes 4 and 5."""
    a2a_npus = [h for h in t.hosts if h["type"] == "NPU" and h["idx"] < 6]
    culprits = [t.find_host("NPU", s, i) for s in range(t.S) for i in (6, 7)]
    ssd0 = t.find_host("SSD", 0, 0)
    tasks = wl_a2a(t, scale, npus=a2a_npus)
    kv = 2 * t.rate * EFF_UNI * oversub / len(culprits)
    tasks += [(h["id"], ssd0["id"], _bytes(kv, scale), "URMA_WRITE", BULK_PRIO, 0, "incast")
              for h in culprits]
    tasks += wl_ctrl(t, a2a_npus, seed=seed)
    return tasks


def wl_ctrl(t, npus, period_ns=4000, horizon_ns=150000, size=256, seed=1):
    rng = random.Random(seed)
    tasks = []
    for a in npus:
        off = rng.randrange(period_ns)
        for k in range(horizon_ns // period_ns):
            b = rng.choice([x for x in npus if x["id"] != a["id"]])
            tasks.append((a["id"], b["id"], size, "URMA_WRITE", CTRL_PRIO, off + k * period_ns, "ctrl"))
    return tasks


def wl_mixed(t, scale, seed=1):
    """Collective + heterogeneous P2P + storage incast + control messages sharing the fabric.

    * alltoall among NPU0..5 of every server (24 NPUs);
    * the P2P IO set on CPUs/NIC/SSD/NPU6/NPU7 (see p2p_io_flows);
    * KVCache write incast: NPU4/NPU5 of servers 1..S-1 -> SSD of server 0 (planes 4,5 only),
      an oversubscribed hotspot whose backpressure shares L2->L1 links with alltoall traffic;
    * 256 B control writes on a higher-priority VL among the alltoall NPUs.
    """
    a2a_npus = [h for h in t.hosts if h["type"] == "NPU" and h["idx"] < 6]
    incast_src = [t.find_host("NPU", s, i) for s in range(1, t.S) for i in (4, 5)]
    ssd0 = t.find_host("SSD", 0, 0)
    kv_rate = 2 * t.rate * EFF_UNI / len(incast_src)
    tasks = wl_a2a(t, scale, npus=a2a_npus, rate_per_npu=8 * t.rate * EFF_BIDIR - kv_rate)
    # SSD0's ingress is the incast hotspot; drop its regular KV write-back so demand stays at line rate.
    tasks += [x for x in p2p_io_flows(t, scale) if not (x[6] == "kv_write" and x[1] == ssd0["id"])]
    tasks += [(h["id"], ssd0["id"], _bytes(kv_rate, scale), "URMA_WRITE", BULK_PRIO, 0, "kv_incast")
              for h in incast_src]
    tasks += wl_ctrl(t, a2a_npus, seed=seed)
    return tasks


SCENARIOS = {"d2d": wl_d2d, "d2dread": wl_d2d_read, "h2d": wl_h2d, "a2a": wl_a2a,
             "mixed": wl_mixed, "a2aread": wl_a2a_read, "mixedread": wl_mixed_read,
             "stagger": wl_stagger, "staggermixed": wl_stagger_mixed, "incast": wl_incast}
SEEDED = {"mixed", "mixedread", "stagger", "staggermixed", "incast"}


def write_traffic(out, tasks):
    with open(os.path.join(out, "traffic.csv"), "w") as f:
        f.write("taskId,sourceNode,destNode,dataSize(Byte),opType,priority,delay,phaseId,dependOnPhases\n")
        for i, (a, b, n, op, pr, d, _) in enumerate(tasks):
            f.write(f"{i},{a},{b},{n},{op},{pr},{d}ns,0,\n")
    with open(os.path.join(out, "tasks_meta.json"), "w") as f:
        json.dump([dict(task=i, src=a, dst=b, bytes=n, op=op, prio=pr, delay_ns=d, cls=c)
                   for i, (a, b, n, op, pr, d, c) in enumerate(tasks)], f)


# ---------------- faults ----------------
# Fault rows are (relative node/port selectors resolved against the topology).
def fault_rows(t, fault, t_ns, scenario="d2d"):
    """Fault on plane 3. Near end = a sending device, far end = a receiving device.

    d2d/a2a/mixed: sender NPU(0,0), receiver NPU(1,0) (its d2d peer).
    h2d: the H2D path CPU0(0) -> NPU6(0), so the near-end device has only 4 planes (N = 4).
    """
    if scenario == "h2d":
        npu00 = t.find_host("CPU", 0, 0)
        npu10 = t.find_host("NPU", 0, 6)
    else:
        npu00 = t.find_host("NPU", 0, 0)
        npu10 = t.find_host("NPU", 1, 0)
    p = 3
    # For reads the near-end device (requester) receives the data and the far end sends it.
    near_dir, far_dir = ("rx", "tx") if scenario == "d2dread" else ("tx", "rx")
    if fault == "F0":
        return [], None
    if fault == "F1near":   # endpoint single-plane failure, sender side
        return [(t_ns, npu00["id"], npu00["ports"][p], "ISOLATE", "")], \
            dict(dev=npu00["id"], dir=near_dir, n=len(npu00["planes"]))
    if fault == "F1far":    # endpoint single-plane failure, receiver side
        return [(t_ns, npu10["id"], npu10["ports"][p], "ISOLATE", "")], \
            dict(dev=npu10["id"], dir=far_dir, n=len(npu10["planes"]))
    if fault == "F2up":     # one L1 uplink (switch port) isolated
        sw = t.l1[(p, 0)]
        return [(t_ns, sw, t.l1_up_port[(p, 0, 0, 0)], "ISOLATE", "")], \
            dict(dev=None, dir="agg", n=t.K * t.J)
    if fault == "F3near":   # one lane lost on the sender's plane-3 port (x4 -> x3)
        return [(t_ns, npu00["id"], npu00["ports"][p], "DEGRADE", f"{t.lanes - 1}/{t.lanes}")], \
            dict(dev=npu00["id"], dir=near_dir, n=len(npu00["planes"]) * t.lanes)
    if fault == "F3far":
        return [(t_ns, npu10["id"], npu10["ports"][p], "DEGRADE", f"{t.lanes - 1}/{t.lanes}")], \
            dict(dev=npu10["id"], dir=far_dir, n=len(npu10["planes"]) * t.lanes)
    if fault == "F4":       # plane failure inside server 0: the plane-3 L1 switch is isolated
        return [(t_ns, t.l1[(p, 0)], 0, "ISOLATE_NODE", "")], dict(dev="server0", dir="agg", n=t.P,
                                                                     plane=p)
    # Faults on the planes of the 2-plane devices (SSD on planes 4,5; NIC on planes 0,1).
    ssd0, nic0 = t.find_host("SSD", 0, 0), t.find_host("NIC", 0, 0)
    if fault == "F1ssd":    # SSD0 loses one of its two ports (the storage hotspot in mixed)
        return [(t_ns, ssd0["id"], ssd0["ports"][4], "ISOLATE", "")], \
            dict(dev=ssd0["id"], dir="both", n=2, plane=4)
    if fault == "F3ssd":
        return [(t_ns, ssd0["id"], ssd0["ports"][4], "DEGRADE", f"{t.lanes - 1}/{t.lanes}")], \
            dict(dev=ssd0["id"], dir="both", n=2 * t.lanes, plane=4)
    if fault == "F1nic":
        return [(t_ns, nic0["id"], nic0["ports"][0], "ISOLATE", "")], \
            dict(dev=nic0["id"], dir="both", n=2, plane=0)
    if fault == "F3nic":
        return [(t_ns, nic0["id"], nic0["ports"][0], "DEGRADE", f"{t.lanes - 1}/{t.lanes}")], \
            dict(dev=nic0["id"], dir="both", n=2 * t.lanes, plane=0)
    if fault == "F4p4":     # server-0 plane-4 L1 isolated: hits SSD0 (and NPUs/no CPU) of server 0
        return [(t_ns, t.l1[(4, 0)], 0, "ISOLATE_NODE", "")], dict(dev="server0", dir="agg", n=t.P,
                                                                    plane=4)
    # Hard link-down variants (link_health DOWN): packets on the link are lost, nobody is told
    # except the link's own two ends (local detection); remote ends must detect it themselves.
    if fault.startswith("H"):
        rows, info = fault_rows(t, "F" + fault[1:], t_ns, scenario)
        conv = {"ISOLATE": "DOWN", "ISOLATE_NODE": "DOWN_NODE"}
        return [(r[0], r[1], r[2], conv[r[3]], r[4]) for r in rows], info
    raise KeyError(fault)


FAULTS = ["F0", "F1near", "F1far", "F2up", "F3near", "F3far", "F4",
          "F1ssd", "F3ssd", "F1nic", "F3nic", "F4p4",
          "H1near", "H1far", "H2up", "H4", "H1ssd", "H4p4"]


# ---------------- schemes ----------------
COMMON_ATTRS = {
    "ns3::UbApp::EnableMultiPath": "true",
    "ns3::UbPort::UbDataRate": "400Gbps",
    "ns3::UbSwitch::FlowControl": "CBFC",
    "ns3::UbTransportChannel::EnableRetrans": "false",
    "ns3::UbTransportChannel::TpOooThreshold": "8192",
    "ns3::UbJetty::JettyOooAckThreshold": "8192",
    "ns3::UbJetty::UbJettyInflightMax": "10000",
    "ns3::UbTransportChannel::DefaultMaxWqeSegNum": "1000",
    "ns3::UbTransportChannel::DefaultMaxInflightPacketSize": "1000",
    "ns3::UbSwitchAllocator::AllocationTime": "+1ns",
    "ns3::UbQueueManager::ReservePerQueueBytes": "1048576",
    # Transport ACK/NAK on a higher-priority VL for every scheme: with per-packet input round-robin,
    # small responses sharing a saturated output with data are otherwise starved (fabric setting,
    # applied identically to baselines and FlexSpray).
    "ns3::UbTransportChannel::ResponseVl": "1",
}
DCQCN_ATTRS = {
    # DCQCN parameters scaled from the paper's 40G/ms setting to 400G and a ~2 us RTT.
    "ns3::UbHostDcqcn::LineRate": "400Gbps",
    "ns3::UbHostDcqcn::InitialRate": "400Gbps",
    "ns3::UbHostDcqcn::CnpInterval": "+4000ns",
    "ns3::UbHostDcqcn::RateIncreaseTimer": "+5000ns",
    "ns3::UbHostDcqcn::AlphaUpdateInterval": "+5000ns",
    "ns3::UbHostDcqcn::ByteCounterThreshold": "1048576",
    "ns3::UbHostDcqcn::RateAi": "4Gbps",
    "ns3::UbHostDcqcn::HyperAiRate": "20Gbps",
    "ns3::UbHostDcqcn::AlphaGain": "0.0625",
}


DCQCN_SETS = {
    # Larger ECN thresholds/higher Pmax, faster timers (HPCC-paper style scaled to 400G).
    "dq2": {"ns3::UbSwitchDcqcn::KminBytes": "102400", "ns3::UbSwitchDcqcn::KmaxBytes": "409600",
            "ns3::UbSwitchDcqcn::Pmax": "0.2", "ns3::UbHostDcqcn::CnpInterval": "+2000ns",
            "ns3::UbHostDcqcn::RateIncreaseTimer": "+2000ns",
            "ns3::UbHostDcqcn::AlphaUpdateInterval": "+2000ns",
            "ns3::UbHostDcqcn::RateAi": "10Gbps", "ns3::UbHostDcqcn::HyperAiRate": "50Gbps"},
    # Moderate thresholds, slow gentle recovery.
    "dq3": {"ns3::UbSwitchDcqcn::KminBytes": "51200", "ns3::UbSwitchDcqcn::KmaxBytes": "204800",
            "ns3::UbSwitchDcqcn::Pmax": "0.1", "ns3::UbHostDcqcn::RateIncreaseTimer": "+10000ns",
            "ns3::UbHostDcqcn::RateAi": "2Gbps", "ns3::UbHostDcqcn::HyperAiRate": "10Gbps",
            "ns3::UbHostDcqcn::AlphaGain": "0.00390625"},
}


def scheme_attrs(name):
    """Return (defaults dict, globals dict, cc kind) for a named scheme."""
    d, g = dict(COMMON_ATTRS), {}
    parts = name.split("+")
    base = parts[0]
    flow = lambda pp: {"ns3::UbApp::RoutingType": pp, "ns3::UbTransportChannel::RoutingType": pp,
                       "ns3::UbLdstApi::RoutingType": pp.replace("FLOW", "PACKET")}
    if base == "ecmp":          # per-flow ECMP inside planes, adaptive multi-QP across planes
        d.update(flow("PER_FLOW_SHORTEST_PATHS")); d["ns3::UbRoutingProcess::MultipathSelector"] = "HASH64"
    elif base == "static":      # per-flow ECMP + static round-robin multi-QP striping
        d.update(flow("PER_FLOW_SHORTEST_PATHS")); d["ns3::UbRoutingProcess::MultipathSelector"] = "HASH64"
        d["ns3::UbTransaction::TpgDispatchPolicy"] = "STATIC_RR"
    elif base == "spray":       # per-packet hash spray
        d.update(flow("PER_PACKET_SHORTEST_PATHS")); d["ns3::UbRoutingProcess::MultipathSelector"] = "HASH64"
    elif base == "rr":          # per-packet round-robin spray
        d.update(flow("PER_PACKET_SHORTEST_PATHS")); d["ns3::UbRoutingProcess::MultipathSelector"] = "ROUND_ROBIN"
    elif base == "adaptive":    # per-packet local-queue adaptive (existing UB)
        d.update(flow("PER_PACKET_SHORTEST_PATHS")); d["ns3::UbRoutingProcess::MultipathSelector"] = "ADAPTIVE"
    elif base == "flex":        # FlexSpray switch layer
        d.update(flow("PER_PACKET_SHORTEST_PATHS")); d["ns3::UbRoutingProcess::MultipathSelector"] = "ADAPTIVE_RATE"
        d["ns3::UbTransaction::TpgDispatchPolicy"] = "MIN_DRAIN_TIME"
        # v4: strict deferral (wait whenever the best admissible plane is slower than the best
        # live plane). With the v3 read path this beats 0.1 in 19 of 79 scenario cells and loses
        # in 5 (read alltoall with a lane degrade 0.886 -> 0.964). Negative disables deferral.
        d["ns3::UbTransaction::TpgDispatchSlack"] = "0"
        d["ns3::UbSwitchAllocator::TpGroupFairShare"] = "true"
        d["ns3::UbJetty::TaSegmentBytes"] = "16384"
        d["ns3::UbTransportChannel::MaxOutstandingReadBytes"] = "196608"
        # v2: a separate, deeper pipeline for READ segments (requests and responses). The shallow
        # write depth (2 x 16 KB per TP) starved read alltoall (0.79 -> 0.95 with 8).
        d["ns3::UbTransportChannel::MaxActiveReadSegments"] = "8"
        # v3 (second review round):
        #  * fair-share weight counts only non-isolated planes of a flow (SSD/NIC port faults);
        #  * a flow's read quota is re-spread over its usable planes after an isolation;
        #  * READ requests ride the control VL (no HoL behind bulk data at a hot ingress);
        #  * READ responses are not gated by SprayCC: reads are receiver-driven by the quota.
        d["ns3::UbSwitchAllocator::TpGroupFairShareUsableOnly"] = "true"
        d["ns3::UbTransportChannel::ReadQuotaFollowsUsablePlanes"] = "true"
        d["ns3::UbTransportChannel::ReadRequestVl"] = "1"
        d["ns3::UbTransportChannel::CcExemptReadResponses"] = "true"
        # v5 (third review round):
        #  * app-limited guard on the delivery-rate estimator (T10: oversubscribed fabric);
        #  * transport failure detection (suspect -> probe on the control VL -> dead -> re-send the
        #    dead TP's segments on other planes): no oracle for hard link-downs (T1/T3);
        #  * TP retransmission (selective) for packets lost inside the fabric.
        d["ns3::UbTransportChannel::DeliveryRateAppLimitedGuard"] = "true"
        d["ns3::UbTransportChannel::FailureDetection"] = "true"
        d["ns3::UbTransportChannel::EnableRetrans"] = "true"
        d["ns3::UbTransportChannel::RetransmissionMode"] = "SELECTIVE"
    else:
        raise KeyError(base)
    cc = "none"
    for opt in parts[1:]:
        if opt == "dcqcn":
            cc = "dcqcn"; g["UB_CC_ENABLED"] = "true"; g["UB_CC_ALGO"] = "DCQCN"; d.update(DCQCN_ATTRS)
        elif opt in ("dq2", "dq3"):
            # Alternative DCQCN parameter sets (baseline tuning; pick the best per scenario).
            cc = "dcqcn"; g["UB_CC_ENABLED"] = "true"; g["UB_CC_ALGO"] = "DCQCN"; d.update(DCQCN_ATTRS)
            d.update(DCQCN_SETS[opt])
        elif opt == "selhash":     # ablation: FlexSpray with plain per-packet hash spraying in switches
            d["ns3::UbRoutingProcess::MultipathSelector"] = "HASH64"
        elif opt == "seladaptive":  # ablation: FlexSpray with the existing (bytes-only) ADAPTIVE selector
            d["ns3::UbRoutingProcess::MultipathSelector"] = "ADAPTIVE"
        elif opt.startswith("slack"):
            d["ns3::UbTransaction::TpgDispatchSlack"] = opt[5:]
        elif opt == "caqm":
            cc = "caqm"; g["UB_CC_ENABLED"] = "true"; g["UB_CC_ALGO"] = "CAQM"
        elif opt == "spraycc":
            cc = "spraycc"; g["UB_CC_ENABLED"] = "true"; g["UB_CC_ALGO"] = "SPRAYCC"
        elif opt == "v2":    # undo the v3 additions (ablation)
            for k in ("ns3::UbSwitchAllocator::TpGroupFairShareUsableOnly",
                      "ns3::UbTransportChannel::ReadQuotaFollowsUsablePlanes",
                      "ns3::UbTransportChannel::CcExemptReadResponses"):
                d[k] = "false"
            d["ns3::UbTransportChannel::ReadRequestVl"] = "4294967295"
        elif opt == "norqvl":
            d["ns3::UbTransportChannel::ReadRequestVl"] = "4294967295"
        elif opt == "noccxr":
            d["ns3::UbTransportChannel::CcExemptReadResponses"] = "false"
        elif opt == "nofd":
            d["ns3::UbTransportChannel::FailureDetection"] = "false"
        elif opt == "nortx":
            d["ns3::UbTransportChannel::EnableRetrans"] = "false"
        elif opt == "fd":    # transport failure detection (suspect -> probe -> dead -> re-send)
            d["ns3::UbTransportChannel::FailureDetection"] = "true"
        elif opt == "rtx":   # TP retransmission (recovers packets lost inside the fabric)
            d["ns3::UbTransportChannel::EnableRetrans"] = "true"
            d["ns3::UbTransportChannel::RetransmissionMode"] = "SELECTIVE"
        elif opt == "alg":   # app-limited guard on the delivery-rate estimator
            d["ns3::UbTransportChannel::DeliveryRateAppLimitedGuard"] = "true"
        elif opt == "noalg":
            d["ns3::UbTransportChannel::DeliveryRateAppLimitedGuard"] = "false"
        elif opt == "ccxr":  # read responses exempt from the CC window (receiver-driven reads)
            d["ns3::UbTransportChannel::CcExemptReadResponses"] = "true"
        elif opt == "rqvl":  # READ requests on the control VL
            d["ns3::UbTransportChannel::ReadRequestVl"] = "1"
        elif opt == "rqf":   # per-flow read quota follows usable planes
            d["ns3::UbTransportChannel::ReadQuotaFollowsUsablePlanes"] = "true"
        elif opt == "usable":  # fair-share weight from non-isolated group members only
            d["ns3::UbSwitchAllocator::TpGroupFairShareUsableOnly"] = "true"
        elif opt.startswith("fx"):  # fair-share exponent, e.g. fx2 or fx1.5
            d["ns3::UbSwitchAllocator::TpGroupFairShareExponent"] = opt[2:]
        elif opt.startswith("buf"):  # shallow switch buffer: CBFC credit per VL per port (KB)
            kb = int(opt[3:])
            d["ns3::UbPort::CbfcInitCreditCell"] = str(kb * 1024 // 160)   # 160 B cells
            d["ns3::UbPort::CbfcCtrlCrdRtrThldCell"] = "64"   # must stay below the credit
        elif opt.startswith("rd"):  # read pipeline depth (rd0 = shared with writes, v1)
            d["ns3::UbTransportChannel::MaxActiveReadSegments"] = opt[2:]
        elif opt.startswith("seg"):
            d["ns3::UbJetty::TaSegmentBytes"] = str(int(opt[3:]) * 1024)
        elif opt.startswith("depth"):
            d["ns3::UbTransportChannel::MaxActiveSendSegments"] = opt[5:]
        elif opt == "fair":
            d["ns3::UbSwitchAllocator::TpGroupFairShare"] = "true"
        elif opt == "nofair":
            d["ns3::UbSwitchAllocator::TpGroupFairShare"] = "false"
        elif opt.startswith("fa"):
            d["ns3::UbTransaction::TpgDispatchPolicy"] = "FIRST_ADMISSIBLE"
        elif opt.startswith("md"):
            d["ns3::UbTransaction::TpgDispatchPolicy"] = "MIN_DRAIN_TIME"
        elif "=" in opt:
            k, v = opt.split("=", 1)
            (g if k.startswith("UB_") else d)[k] = v
        else:
            raise KeyError(opt)
    return d, g, cc


def write_attrs(out, scheme, sample_ns=1000):
    d, g, cc = scheme_attrs(scheme)
    g.setdefault("UB_CC_ENABLED", "false")
    g.update({"UB_TRACE_ENABLE": "true", "UB_TASK_TRACE_ENABLE": "true",
              "UB_PACKET_TRACE_ENABLE": "false", "UB_PORT_TRACE_ENABLE": "false",
              "UB_RECORD_PKT_TRACE": "false", "UB_PARSE_TRACE_ENABLE": "false",
              "UB_GOODPUT_SAMPLE_INTERVAL_NS": str(sample_ns)})
    with open(os.path.join(out, "network_attribute.txt"), "w") as f:
        for k, v in d.items():
            f.write(f'default {k} "{v}"\n')
        for k, v in g.items():
            f.write(f'global {k} "{v}"\n')
    return cc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scenario", choices=sorted(SCENARIOS), required=True)
    ap.add_argument("--fault", choices=FAULTS, default="F0")
    ap.add_argument("--fault-ns", type=int, default=50000)
    ap.add_argument("--scheme", default="flex+spraycc")
    ap.add_argument("--scale", type=float, default=1.0, help="multiply all bulk sizes")
    ap.add_argument("--servers", type=int, default=4)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--sample-ns", type=int, default=1000)
    ap.add_argument("--map", type=int, default=0,
                    help="held-out traffic mapping: 1 relabels every endpoint (server s -> s+1, "
                         "device index i -> count-1-i) while the fault stays put, so faults hit "
                         "different traffic roles than in the tuning runs")
    ap.add_argument("--links-per-pair", type=int, default=3,
                    help="parallel L1-L2 links; 3 = non-blocking (12 up / 11 down), 2 = 1.375:1 "
                         "oversubscribed")
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    t = Topo(servers=a.servers, links_per_pair=a.links_per_pair)
    t.write(a.out)
    fn = SCENARIOS[a.scenario]
    tasks = fn(t, a.scale, seed=a.seed) if a.scenario in SEEDED else fn(t, a.scale)
    if a.map:
        cnt = {typ: c for typ, c, _ in DEVICE_TYPES}
        def remap(hid):
            h = t.hosts[hid]
            return t.find_host(h["type"], (h["server"] + a.map) % t.S, cnt[h["type"]] - 1 - h["idx"])["id"]
        tasks = [(remap(x[0]), remap(x[1])) + tuple(x[2:]) for x in tasks]
    write_traffic(a.out, tasks)
    cc = write_attrs(a.out, a.scheme, a.sample_ns)
    rows, finfo = fault_rows(t, a.fault, a.fault_ns, a.scenario)
    if rows:
        with open(os.path.join(a.out, "link_health.csv"), "w") as f:
            f.write("timeNs,nodeId,portId,action,value\n")
            for r in rows:
                f.write(",".join(map(str, r)) + "\n")
    meta = t.meta()
    meta.update(scenario=a.scenario, fault=a.fault, fault_ns=a.fault_ns if rows else None,
                fault_rows=rows, fault_info=finfo, scheme=a.scheme, cc=cc, traffic_map=a.map,
                ack_bytes=ACK_BYTES[cc], scale=a.scale, seed=a.seed)
    with open(os.path.join(a.out, "case_meta.json"), "w") as f:
        json.dump(meta, f, indent=1)


if __name__ == "__main__":
    sys.exit(main())
