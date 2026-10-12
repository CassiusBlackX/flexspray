"""Approved 128-host, 16-leaf, 8-spine topology; existing tools own CSV generation."""
from net_sim_builder import NetworkSimulationGraph


def build_topology():
    graph = NetworkSimulationGraph()
    for node in range(128):
        graph.add_netisim_host(node, forward_delay="1ns")
    for node in range(128, 152):
        graph.add_netisim_node(node, forward_delay="1ns")
    for host in range(128):
        graph.add_netisim_edge(host, 128 + host // 8, bandwidth="400Gbps", delay="20ns")
    for leaf in range(128, 144):
        for spine in range(144, 152):
            graph.add_netisim_edge(leaf, spine, bandwidth="400Gbps", delay="20ns")
    return graph
