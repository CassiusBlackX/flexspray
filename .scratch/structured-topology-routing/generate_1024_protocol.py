import argparse
import csv
import shutil
import struct
import sys
import tempfile
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "scratch" / "ns-3-ub-tools"
DEFAULT_CASE = REPO_ROOT / ".scratch" / "structured-topology-routing" / "1dpod-1024npu"
sys.path.insert(0, str(TOOLS_DIR))

from case_topology_loader import load_case_topology
from route_split import build_shortest_route_lookups_for_destination, group_route_candidates
from structured_case import compile_structured_routes, serialize_structured_config


QUERY_MAGIC = b"UBQRY001"
QUERY_HEADER = struct.Struct("<8sQ")
QUERY_RECORD = struct.Struct("<II")


def _node_ip(node_id, port_id=None):
    last_byte = 0 if port_id is None else (port_id + 1) % 256
    return (
        0x0A000000
        + (node_id // 256) * 0x00010000
        + (node_id % 256) * 0x00000100
        + last_byte
    )


class _QueryWriter:
    def __init__(self, path):
        self._output = path.open("wb")
        self._output.write(QUERY_HEADER.pack(QUERY_MAGIC, 0))
        self.count = 0

    def write(self, current_node_id, destination_ip):
        self._output.write(QUERY_RECORD.pack(current_node_id, destination_ip))
        self.count += 1

    def close(self):
        self._output.seek(0)
        self._output.write(QUERY_HEADER.pack(QUERY_MAGIC, self.count))
        self._output.close()


def _write_workload_queries(traffic_path, output_path):
    writer = _QueryWriter(output_path)
    try:
        with traffic_path.open(newline="", encoding="utf-8") as traffic:
            rows = csv.DictReader(traffic)
            required = {"sourceNode", "destNode"}
            if rows.fieldnames is None or not required.issubset(rows.fieldnames):
                raise RuntimeError(
                    "workload traffic must contain sourceNode and destNode columns"
                )
            for row in rows:
                writer.write(int(row["sourceNode"]), _node_ip(int(row["destNode"])))
    finally:
        writer.close()
    return writer.count


def _counted(values):
    values = tuple(values)
    return " ".join([str(len(values)), *(str(value) for value in values)])


def _write_topology_prefix(output, network):
    topology = network.get_physical_topology()
    port_counts = {
        node_id: int(network.port_list[node_id][1])
        for node_id in range(len(network.port_list))
    }
    output.write("NS3_UB_STRUCTURED 3\n")
    output.write(f"NODES {len(topology.nodes)}\n")
    for node in sorted(topology.nodes, key=lambda item: item.node_id):
        node_type = "DEVICE" if node.node_type == "host" else "SWITCH"
        system_id = int(node.attributes.get("system_id", 0))
        output.write(
            f"{node.node_id} {node_type} {port_counts[node.node_id]} "
            f"{node.allocation_delay} {node.forward_delay} {system_id}\n"
        )
    output.write(f"LINKS {len(topology.links)}\n")
    for link in topology.links:
        output.write(
            f"{link.left_node} {link.left_port} {link.right_node} {link.right_port} "
            f"{link.bandwidth} {link.delay}\n"
        )


def _write_streamed_table(output, network, query_path=None, node_query_path=None):
    lookup_count = 0
    query_writer = _QueryWriter(query_path) if query_path is not None else None
    node_query_writer = (
        _QueryWriter(node_query_path) if node_query_path is not None else None
    )
    with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as table_rows:
        try:
            for destination in sorted(network.host_ids):
                lookups = build_shortest_route_lookups_for_destination(network, destination)
                lookup_count += len(lookups)
                previous_node_query = None
                for (current, destination_node, destination_port), metrics in sorted(
                    lookups.items()
                ):
                    candidates = group_route_candidates(metrics)
                    if candidates is None:
                        raise RuntimeError("generated table route has no output ports")
                    table_rows.write(
                        f"{current} {destination_node} {destination_port} "
                        f"{_counted(candidates.shortest_ports)} "
                        f"{_counted(candidates.non_shortest_ports)}\n"
                    )
                    if query_writer is not None:
                        query_writer.write(
                            current, _node_ip(destination_node, destination_port)
                        )
                    node_query = (current, destination_node)
                    if node_query_writer is not None and node_query != previous_node_query:
                        node_query_writer.write(current, _node_ip(destination_node))
                        previous_node_query = node_query
        finally:
            if query_writer is not None:
                query_writer.close()
            if node_query_writer is not None:
                node_query_writer.close()
        output.write(f"TABLE {lookup_count}\n")
        table_rows.seek(0)
        shutil.copyfileobj(table_rows, output, length=1024 * 1024)
    output.write("PORT_GROUPS 0\n")
    output.write("RULES 0\n")
    output.write("RULE_RANGES 0\n")
    output.write("TOPOLOGY_RULES 0\n")
    output.write("END\n")
    return lookup_count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("table", "topology-rule"))
    parser.add_argument("--case", type=Path, default=DEFAULT_CASE)
    parser.add_argument(
        "--queries",
        type=Path,
        help="write every table lookup key to a compact binary query corpus",
    )
    parser.add_argument(
        "--node-queries",
        type=Path,
        help="write every valid current-node/destination-node query",
    )
    parser.add_argument(
        "--workload-traffic",
        type=Path,
        help="traffic.csv used to produce an endpoint-only workload query corpus",
    )
    parser.add_argument(
        "--workload-queries",
        type=Path,
        help="output path for endpoint-only workload queries",
    )
    args = parser.parse_args()

    if (args.queries is not None or args.node_queries is not None) and args.mode != "table":
        parser.error("--queries and --node-queries are only valid in table mode")
    if (args.workload_traffic is None) != (args.workload_queries is None):
        parser.error("--workload-traffic and --workload-queries must be used together")

    started = time.perf_counter()
    network = load_case_topology(args.case)
    if args.mode == "table":
        _write_topology_prefix(sys.stdout, network)
        route_count = _write_streamed_table(
            sys.stdout, network, args.queries, args.node_queries
        )
        detail = f"tableRoutes={route_count}"
    else:
        compiled = compile_structured_routes(network, routing_mode="mixed", use_cache=False)
        sys.stdout.write(serialize_structured_config(network, compiled.config))
        detail = (
            f"topologyRules={len(compiled.config.topology_rules)} "
            f"memberships={sum(len(rule.nodes) for rule in compiled.config.topology_rules)}"
        )
    if args.workload_traffic is not None:
        workload_count = _write_workload_queries(
            args.workload_traffic, args.workload_queries
        )
        detail += f" workloadQueries={workload_count}"
    sys.stderr.write(
        f"generated mode={args.mode} {detail} seconds={time.perf_counter() - started:.6f}\n"
    )


if __name__ == "__main__":
    main()
