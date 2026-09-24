#!/usr/bin/env python3
"""Validate CSV candidate reachability after the configured permanent bidirectional fault.

Explore every eligible candidate, not just graph connectivity or one fortunate selector result.
This is a preparation check of configured routes; actual network evidence is checked separately.
"""
import csv
import json
import shlex
import sys
from functools import lru_cache
from pathlib import Path


def rows(path):
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def expand(value):
    bounds = value.split("..")
    return range(int(bounds[0]), int(bounds[-1]) + 1)


def validate(case):
    attributes = {}
    for line in (case / "network_attribute.txt").read_text().splitlines():
        fields = shlex.split(line, comments=True)
        if not fields:
            continue
        if len(fields) != 3 or fields[0] not in ("default", "global"):
            raise ValueError("unsupported parameter override syntax")
        if fields[1] in attributes:
            raise ValueError("ambiguous duplicate parameter override")
        attributes[fields[1]] = fields[2]
    for owner in ("UbApp", "UbTransportChannel"):
        if attributes.get(f"ns3::{owner}::RoutingType") != "PER_FLOW_SHORTEST_PATHS":
            raise ValueError("preflight supports PER_FLOW_SHORTEST_PATHS only")
    if attributes.get("ns3::UbApp::TransportMode") != "RTP":
        raise ValueError("preflight supports RTP only")
    links = {}
    for row in rows(case / "topology.csv"):
        a = int(row["nodeId1"]), int(row["portId1"])
        b = int(row["nodeId2"]), int(row["portId2"])
        if a in links or b in links:
            raise ValueError("physical port appears on multiple links")
        links[a], links[b] = b, a
    failures = rows(case / "link_fault.csv")
    if len(failures) != 1:
        raise ValueError("one permanent bidirectional fault required")
    endpoint = int(failures[0]["nodeId"]), int(failures[0]["portId"])
    failed = {endpoint, links[endpoint]}
    routes = {}
    for row in rows(case / "routing_table.csv"):
        ports = list(map(int, row["outPorts"].split()))
        metrics = list(map(int, row["metrics"].split()))
        if not ports or len(ports) != len(metrics):
            raise ValueError("invalid route candidate/metric row")
        if row["dstPortId"] != "0":
            raise ValueError("preflight currently supports explicit destination port0 rows only")
        best = min(metrics)
        for node in expand(row["nodeId"]):
            for destination in expand(row["dstNodeId"]):
                key = node, destination, int(row["dstPortId"])
                if key in routes:
                    raise ValueError(f"overlapping route entries unsupported: {key}")
                shortest, other = routes.setdefault(key, (set(), set()))
                for port, metric in zip(ports, metrics):
                    (shortest if metric == best else other).add(port)
                    if (node, port) not in links:
                        raise ValueError(f"unconnected route candidate {node}:{port}")
    hosts = set()
    for row in rows(case / "node.csv"):
        if row["nodeType"] == "DEVICE":
            if row["portNum"] != "1":
                raise ValueError("preflight currently supports single-port hosts only")
            hosts.update(expand(row["nodeId"]))
    # These cases have one port per destination host. CSV loader also builds the device
    # route from these exact entries. Switch destinations use the same explicit port0 row.
    active = set()
    @lru_cache(None)
    def walk(node, ingress, destination):
        if node == destination:
            return 0
        state = node, ingress, destination
        if state in active:
            raise ValueError(f"candidate forwarding loop at {state}")
        if node in hosts and ingress != -1:
            raise ValueError(f"unexpected host transit {state}")
        shortest, other = routes.get((node, destination, 0), (set(), set()))
        legal = shortest - {ingress}
        candidates = {p for p in legal if (node, p) not in failed}
        if not candidates and legal and any((node, p) in failed for p in legal):
            candidates = {p for p in other - {ingress} if (node, p) not in failed}
        if not candidates:
            raise ValueError(f"no legal configured output at {state}")
        active.add(state)
        try:
            return 1 + max(walk(*links[node, port], destination) for port in candidates)
        finally:
            active.remove(state)
    handshake_hops = {}
    for node, port in sorted(failed):
        peer = links[node, port][0]
        handshake_hops[f"{node}:{port}->{peer}"] = walk(node, -1, peer)
    pairs = set()
    for row in rows(case / "traffic.csv"):
        if row["opType"] != "URMA_WRITE" or row.get("srcPortId", "") not in ("", "0") or row.get("dstPortId", "") not in ("", "0"):
            raise ValueError("preflight supports single-port URMA_WRITE only")
        pair = int(row["sourceNode"]), int(row["destNode"])
        pairs.add(pair)
        walk(pair[0], -1, pair[1])
    return {"status": "PASS", "failed_endpoints": sorted(failed),
            "task_pairs": len(pairs), "lastack_max_hops": handshake_hops,
            "validated_states": walk.cache_info().currsize}


if __name__ == "__main__":
    print(json.dumps(validate(Path(sys.argv[1])), indent=2))
