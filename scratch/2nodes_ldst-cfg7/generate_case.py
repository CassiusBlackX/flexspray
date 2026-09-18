#!/usr/bin/env python3
"""Generate the approved four-task CFG7 fixture; never runs a simulation."""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

CASE = Path(__file__).resolve().parent
ROOT = CASE.parents[1]
sys.path.insert(0, str(ROOT / "scratch/ns-3-ub-tools"))
sys.path.insert(0, str(ROOT / ".agents/skills/openusim-run-experiment/scripts"))

import net_sim_builder as netsim
from openusim_run_experiment.case_checker import check_case_files
from openusim_run_experiment.network_attribute_writer import (
    load_or_build_parameter_catalog,
    observability_preset,
    write_network_attributes,
)


def build_topology():
    graph = netsim.NetworkSimulationGraph()
    graph.output_dir = str(CASE)
    for host in (0, 1):
        graph.add_netisim_host(host, allocation_delay="1ns", forward_delay="0ns")
    graph.add_netisim_node(2, allocation_delay="1ns", forward_delay="0ns")
    for host in (0, 1):
        graph.add_netisim_edge(host, 2, bandwidth="400Gbps", delay="20ns")
    return graph


def main():
    graph = build_topology()
    graph.build_graph_config()
    graph.write_config(include_transport=False)
    # Exact rows also install primary node routes in UbUtils::AddRoutingTable.
    with (CASE / "routing_table.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["nodeId", "dstNodeId", "dstPortId", "outPorts", "metrics"])
        writer.writerows([(0, 1, 0, 0, 2), (1, 0, 0, 0, 2), (2, 0, 0, 0, 1), (2, 1, 0, 1, 1)])
    with (CASE / "traffic.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow([
            "taskId", "sourceNode", "destNode", "dataSize(Byte)", "opType", "priority",
            "delay", "phaseId", "dependOnPhases", "srcPortId", "dstPortId",
        ])
        writer.writerows([
            (0, 0, 1, 4096, "MEM_STORE", 7, "10ns", 0, "", "", ""),
            (1, 1, 0, 4096, "MEM_LOAD", 7, "10ns", 0, "", "", ""),
            (2, 1, 0, 4096, "MEM_STORE", 7, "10ns", 0, "", 0, 0),
            (3, 0, 1, 4096, "MEM_LOAD", 7, "10ns", 0, "", 0, 0),
        ])
    catalog, _ = load_or_build_parameter_catalog()
    entries = {entry["parameter_key"]: entry for entry in catalog["entries"]}
    for component in ("UbLdstApi", "UbLdstEndpoint"):
        entry = entries[f"ns3::{component}::HeaderConfig"]
        if entry["default_value"] != "9":
            raise ValueError(f"Unexpected HeaderConfig default: {entry}")
    resolution = write_network_attributes(
        CASE,
        explicit_overrides={
            "ns3::UbLdstApi::HeaderConfig": "7",
            "ns3::UbLdstEndpoint::HeaderConfig": "7",
            "ns3::UbLdstApi::RoutingType": "PER_FLOW_SHORTEST_PATHS",
            "ns3::UbSwitch::FlowControl": "CBFC",
            "ns3::UbLdstInstance::ThreadNum": "1",
            "ns3::UbLdstThread::StoreOutstanding": "4",
            "ns3::UbLdstThread::LoadOutstanding": "4",
            "ns3::UbLdstThread::StoreRequestSize": "512",
            "ns3::UbLdstThread::LoadResponseSize": "512",
            "ns3::UbLdstThread::LoadRequestSize": "64",
        },
        observability_overrides=observability_preset("balanced"),
    )
    (CASE / "attribute-resolution.json").write_text(json.dumps(resolution, indent=2) + "\n")
    result = check_case_files(CASE, transport_channel_mode="on-demand")
    if result["status"] != "ok":
        raise ValueError(result)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
