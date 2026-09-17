#!/usr/bin/env python3
"""Generate a deterministic binary corpus of valid route lookup keys."""

import argparse
import struct
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "scratch" / "ns-3-ub-tools"
sys.path.insert(0, str(TOOLS_DIR))

from case_topology_loader import load_case_topology
from route_split import build_shortest_route_lookups_for_destination


HEADER = struct.Struct("<8sQ")
RECORD = struct.Struct("<II")
MAGIC = b"UBQRY001"


def node_ip(node_id, port_id):
    return (
        0x0A000000
        + (node_id // 256) * 0x00010000
        + (node_id % 256) * 0x00000100
        + port_id
        + 1
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("case", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--routing-csv", type=Path)
    args = parser.parse_args()

    graph = load_case_topology(args.case)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    routing_output = None
    if args.routing_csv is not None:
        args.routing_csv.parent.mkdir(parents=True, exist_ok=True)
        routing_output = args.routing_csv.open("w", encoding="utf-8")
        routing_output.write("nodeId,dstNodeId,dstPortId,outPorts,metrics\n")
    with args.output.open("wb") as output:
        output.write(HEADER.pack(MAGIC, 0))
        for destination in sorted(graph.host_ids):
            lookups = build_shortest_route_lookups_for_destination(graph, destination)
            for lookup in sorted(lookups):
                current, destination_node, destination_port = lookup
                output.write(RECORD.pack(
                    current,
                    node_ip(destination_node, destination_port),
                ))
                if routing_output is not None:
                    port_metrics = sorted(lookups[lookup].items())
                    routing_output.write(
                        f"{current},{destination_node},{destination_port},"
                        f"{' '.join(str(port) for port, _metric in port_metrics)},"
                        f"{' '.join(str(metric) for _port, metric in port_metrics)}\n"
                    )
                count += 1
        output.seek(0)
        output.write(HEADER.pack(MAGIC, count))
    if routing_output is not None:
        routing_output.close()
    print(f"queries={count} bytes={args.output.stat().st_size} output={args.output}")


if __name__ == "__main__":
    main()
