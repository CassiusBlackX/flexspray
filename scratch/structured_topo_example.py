"""Small structure-aware topology built with NetworkX."""

import networkx as nx

from net_sim_builder import NetworkSimulationGraph


def build_topology():
    device_count = 4
    switch_count = 2

    internal = nx.complete_graph(device_count)
    external = nx.complete_bipartite_graph(device_count, switch_count)
    topology = nx.compose(internal, external)

    graph = NetworkSimulationGraph()
    for node_id in range(device_count):
        graph.add_netisim_host(
            node_id,
            allocation_delay="1ns",
            forward_delay="0ns",
        )
    for node_id in range(device_count, device_count + switch_count):
        graph.add_netisim_node(
            node_id,
            allocation_delay="1ns",
            forward_delay="0ns",
        )
    for left, right in topology.edges:
        graph.add_netisim_edge(left, right, bandwidth="400Gbps", delay="20ns")
    return graph
