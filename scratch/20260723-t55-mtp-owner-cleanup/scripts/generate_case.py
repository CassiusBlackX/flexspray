#!/usr/bin/env python3
"""Generate one frozen T55 case with repo-native topology tooling."""

from __future__ import annotations

import argparse
import csv
import shutil
import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = PACKAGE_ROOT.parents[1]
TOOLS_DIR = REPO_ROOT / "scratch/ns-3-ub-tools"
NETWORK_TEMPLATE = REPO_ROOT / "scratch/20260721-rtp-tp-memory-probe/cases/p001/network_attribute.txt"
sys.path.insert(0, str(TOOLS_DIR))
import net_sim_builder as netsim  # noqa: E402


def generate(output_dir: Path) -> None:
    if output_dir.exists():
        raise RuntimeError(f"refusing to overwrite case input: {output_dir}")
    output_dir.mkdir(parents=True)

    graph = netsim.NetworkSimulationGraph()
    graph.output_dir = str(output_dir)
    hosts, leaves, spines = 32, 4, 8
    for host in range(hosts):
        graph.add_netisim_host(host, forward_delay="1ns")
    for leaf in range(hosts, hosts + leaves):
        graph.add_netisim_node(leaf, forward_delay="1ns")
    for spine in range(hosts + leaves, hosts + leaves + spines):
        graph.add_netisim_node(spine, forward_delay="1ns")
    for host in range(hosts):
        graph.add_netisim_edge(host, hosts + host // 8, bandwidth="400Gbps", delay="20ns", edge_count=1)
    for leaf in range(hosts, hosts + leaves):
        for spine in range(hosts + leaves, hosts + leaves + spines):
            graph.add_netisim_edge(leaf, spine, bandwidth="400Gbps", delay="20ns", edge_count=1)
    graph.build_graph_config()
    graph.gen_2layer_clos_compressed_route_table(hosts, leaves)
    graph.write_config(include_transport=False)

    with (output_dir / "traffic.csv").open("x", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["taskId", "sourceNodeId", "destNodeId", "dataSize(Byte)", "opType",
                         "priority", "delay", "phaseId", "dependOnPhases"])
        for source in range(hosts):
            writer.writerow([source, source, (source + 1) % hosts, 65536, "URMA_WRITE", 7,
                             "10ns", 0, ""])
        for source in range(hosts):
            writer.writerow([hosts + source, source, (source + 1) % hosts, 65536, "URMA_WRITE", 7,
                             "1000ns", 1, 0])

    shutil.copyfile(NETWORK_TEMPLATE, output_dir / "network_attribute.txt")
    unexpected = output_dir / "transport_channel.csv"
    if unexpected.exists():
        raise RuntimeError("on-demand case unexpectedly generated transport_channel.csv")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    generate(args.output_dir.resolve())


if __name__ == "__main__":
    main()
