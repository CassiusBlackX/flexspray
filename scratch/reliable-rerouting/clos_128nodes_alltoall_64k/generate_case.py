#!/usr/bin/env python3
"""Generate the approved case using the existing topology, collective and snapshot writers."""
import argparse
import csv
import hashlib
import json
import subprocess
import sys
from pathlib import Path

SOURCE = Path(__file__).resolve().parent
CASE = SOURCE
ROOT = SOURCE.parents[2]
TOOLS = ROOT / "scratch/ns-3-ub-tools"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / ".agents/skills/openusim-run-experiment/scripts"))
from generate_topology import build_topology
from openusim_run_experiment.network_attribute_writer import write_network_attributes, observability_preset
import networkx as nx


def run(*args):
    subprocess.run([sys.executable, str(TOOLS / "ub_tools.py"), *map(str, args)], cwd=ROOT, check=True)


def expand(value):
    parts = value.split("..")
    return range(int(parts[0]), int(parts[-1]) + 1)


def main():
    global CASE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=SOURCE)
    CASE = parser.parse_args().output_dir.resolve()
    CASE.mkdir(parents=True, exist_ok=True)
    run("topology", "build", SOURCE / "generate_topology.py", "--output-dir", CASE,
        "--case-format", "csv", "--route-generation", "full", "--no-cache")
    graph = build_topology()
    graph.build_graph_config()
    route_path = CASE / "routing_table.csv"
    with route_path.open() as handle:
        reader = csv.DictReader(handle)
        header = reader.fieldnames
        generated = list(reader)
    routes = []
    for row in generated:
        for node in expand(row["nodeId"]):
            for destination in expand(row["dstNodeId"]):
                result = dict(row, nodeId=str(node), dstNodeId=str(destination))
                if node == 144 and destination < 8:
                    ports = list(map(int, row["outPorts"].split()))
                    metrics = list(map(int, row["metrics"].split()))
                    for leaf in range(129, 144):
                        ports.extend(graph.get_link_ports(144, leaf))
                        metrics.append(min(metrics) + 2)
                    result.update(outPorts=" ".join(map(str, ports)), metrics=" ".join(map(str, metrics)))
                routes.append(result)
    # Existing tooling builds host destinations only. Switch destinations use the same graph's
    # shortest-path distances and physical port map, with two bounded failure detour additions.
    for destination in range(128, 152):
        distance = nx.single_source_shortest_path_length(graph, destination)
        for node in range(152):
            if node == destination:
                continue
            ports, metrics = [], []
            for neighbor in sorted(graph.neighbors(node)):
                if distance[neighbor] == distance[node] - 1:
                    port_group = graph.get_link_ports(node, neighbor)
                    ports.extend(port_group)
                    metrics.extend([distance[node]] * len(port_group))
            detour_neighbors = (range(145, 152) if (node, destination) == (128, 144)
                                else range(129, 144) if (node, destination) == (144, 128) else ())
            for neighbor in detour_neighbors:
                ports.extend(graph.get_link_ports(node, neighbor))
                metrics.append(3)
            routes.append(dict(nodeId=node, dstNodeId=destination, dstPortId=0,
                               outPorts=" ".join(map(str, ports)), metrics=" ".join(map(str, metrics))))
    with route_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=header)
        writer.writeheader()
        writer.writerows(routes)
    run("traffic", "alltoall", "--host-count", 128, "--data-size", "8388608B",
        "--algorithm", "pairwise", "--peers-per-phase", 127, "--output-dir", CASE)
    traffic_path = CASE / "traffic.csv"
    with traffic_path.open() as handle:
        reader = csv.DictReader(handle)
        header, traffic = reader.fieldnames, list(reader)
    assert len(traffic) == 16256
    assert {(int(r["sourceNode"]), int(r["destNode"])) for r in traffic} == {
        (s, d) for s in range(128) for d in range(128) if s != d}
    for row in traffic:
        assert int(row["dataSize(Byte)"]) == 65536 and row["dependOnPhases"] == ""
        row["delay"] = "90us"
    with traffic_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=header)
        writer.writeheader()
        writer.writerows(traffic)
    (CASE / "link_fault.csv").write_text("nodeId,portId,failTimeNs\n128,8,100000\n")
    overrides = {
        "ns3::UbLink::EnableReliableRerouting": "true",
        "ns3::UbLink::FailureDetectionDelay": "+5us",
        "ns3::UbLink::LastAckTimeout": "+1s",
        "ns3::UbApp::TransportMode": "RTP",
        "ns3::UbApp::RoutingType": "PER_FLOW_SHORTEST_PATHS",
        "ns3::UbTransportChannel::RoutingType": "PER_FLOW_SHORTEST_PATHS",
        "ns3::UbTransportChannel::EnableRetrans": "false",
        "ns3::UbTransportChannel::EnableFastRetrans": "false",
        "ns3::UbRoutingProcess::MultipathSelector": "INGRESS_PORT_STRIPE",
        "ns3::UbSwitch::FlowControl": "CBFC",
        "ns3::UbQueueManager::ReservePerQueueBytes": "8388608",
        "ns3::UbQueueManager::SharedPoolBytes": "0",
        "ns3::UbQueueManager::HeadroomPerPortBytes": "0",
        "UB_CC_ENABLED": "false",
    }
    trace = observability_preset("balanced")
    trace.update(UB_TRACE_PRESET="custom", UB_PORT_TRACE_ENABLE="false", UB_QUEUE_TRACE_ENABLE="false")
    snapshot = write_network_attributes(CASE, explicit_overrides=overrides, observability_overrides=trace)
    # Match the repository's LF checkout policy before recording exact input hashes.
    for path in CASE.glob("*.csv"):
        path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n"))
    provenance = {"snapshot": snapshot, "tools_commit": subprocess.check_output(
        ["git", "-C", str(TOOLS), "rev-parse", "HEAD"], text=True).strip(),
        "route_representation": "CSV, full host routes plus bounded failure candidates",
        "task_count": len(traffic), "total_bytes": 16256 * 65536,
        "inputs_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in CASE.glob("*.csv")}}
    (CASE / "generation.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    main()
