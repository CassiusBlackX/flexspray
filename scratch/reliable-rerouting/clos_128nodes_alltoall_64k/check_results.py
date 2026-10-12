#!/usr/bin/env python3
"""Strict macro contract plus the preserved small case's shared causal evidence checker."""
import csv
import hashlib
import importlib.util
import json
import shlex
import sys
import tempfile
from pathlib import Path

CASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("rerouting_evidence", CASE.parent / "2nodes_reliable-rerouting/check_results.py")
common = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = common
spec.loader.exec_module(common)


def require(condition, reason):
    if not condition:
        raise common.CheckFailure(reason)


def read(path):
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def contract(case):
    nodes = read(case / "node.csv")
    require(len(nodes) == 152 and {int(r["nodeId"]) for r in nodes} == set(range(152)), "wrong node inventory")
    require(all(r["nodeType"] == ("DEVICE" if int(r["nodeId"]) < 128 else "SWITCH") for r in nodes), "wrong node roles")
    links = read(case / "topology.csv")
    expected = {frozenset((h, 128 + h // 8)) for h in range(128)} | {
        frozenset((l, s)) for l in range(128, 144) for s in range(144, 152)}
    require(len(links) == 256 and {frozenset((int(r["nodeId1"]), int(r["nodeId2"]))) for r in links} == expected, "wrong Clos connectivity")
    require(all(r["bandwidth"] == "400Gbps" and r["delay"] == "20ns" for r in links), "wrong link parameters")
    tasks = read(case / "traffic.csv")
    pairs = {(int(r["sourceNode"]), int(r["destNode"])) for r in tasks}
    require(len(tasks) == 16256 and pairs == {(s, d) for s in range(128) for d in range(128) if s != d}, "wrong alltoall task pairs")
    require(all(r["dataSize(Byte)"] == "65536" and r["opType"] == "URMA_WRITE" and r["priority"] == "7" and r["delay"] == "90us" and r["dependOnPhases"] == "" for r in tasks), "wrong task contract")
    require(sum(int(r["dataSize(Byte)"]) for r in tasks) == 1065353216, "wrong total payload")
    attributes = {}
    for line in (case / "network_attribute.txt").read_text().splitlines():
        fields = shlex.split(line, comments=True)
        if fields:
            require(len(fields) == 3 and fields[1] not in attributes, "ambiguous runtime snapshot")
            attributes[fields[1]] = fields[2]
    for name in ("routing_port_groups.csv", "routing_rules.csv", "routing_rule_ranges.csv", "routing_topology_rules.csv"):
        require(not read(case / name), "macro case expects full table routes without rule overrides")
    expected_attributes = {
        "ns3::UbApp::TransportMode": "RTP",
        "ns3::UbTransportChannel::EnableRetrans": "false",
        "ns3::UbTransportChannel::EnableFastRetrans": "false",
        "ns3::UbTransportChannel::RoutingType": "PER_FLOW_SHORTEST_PATHS",
        "ns3::UbApp::RoutingType": "PER_FLOW_SHORTEST_PATHS",
        "ns3::UbRoutingProcess::MultipathSelector": "INGRESS_PORT_STRIPE",
        "ns3::UbSwitch::FlowControl": "CBFC",
        "ns3::UbQueueManager::ReservePerQueueBytes": "8388608",
        "ns3::UbQueueManager::SharedPoolBytes": "0",
        "ns3::UbQueueManager::HeadroomPerPortBytes": "0",
        "UB_CC_ENABLED": "false",
    }
    require(all(attributes.get(k) == v for k, v in expected_attributes.items()), "runtime configuration changed or TP retransmission enabled")
    fault = common._read_link_fault(case)
    require((fault.node_id, fault.port_id) == (128, 8), "wrong failed leaf-spine link")
    manifest_path = case / "output/run_result.json"
    require(manifest_path.is_file(), "missing run exit/provenance evidence")
    manifest = json.loads(manifest_path.read_text())
    require(manifest.get("exit_code") == 0, "simulation did not exit successfully")
    require(manifest.get("inputs_unchanged") is True, "inputs or source changed during execution")
    runtime_inputs = {p.name for p in case.glob("*.csv")} | {"network_attribute.txt"}
    require(set(manifest.get("inputs_sha256", {})) == runtime_inputs, "runtime input inventory changed")
    for name in runtime_inputs:
        require(manifest.get("inputs_sha256", {}).get(name) == hashlib.sha256((case / name).read_bytes()).hexdigest(), f"stale input evidence: {name}")
    for name in ("execution.log", "task_statistics.csv", "reliable_rerouting.csv",
                 "reliable_rerouting_occupancy_summary.csv"):
        require(manifest.get("outputs_sha256", {}).get(name) == hashlib.sha256((case / "output" / name).read_bytes()).hexdigest(), f"stale output evidence: {name}")
    log = (case / "output/execution.log").read_text()
    require("[summary] Runtime packet drops: 0" in log and "[run] Simulation finished." in log, "missing clean runtime/drop summary")
    require(not any(term in log for term in ("Packet Dropped", "ReliableRerouting_TO", "ReliableRerouting_SEND_TO", "No legal", "Potential Deadlock", "[error]")), "runtime reported failure")
    events = read(case / "output/reliable_rerouting.csv")
    groups = {}
    for row in events:
        if row["event"] == "lastack_tx_start":
            groups.setdefault((row["source_node"], row["source_port"], row["copy"]), []).append(row)
    require(len(groups) == 6 and all(len(hops) == 3 for hops in groups.values()), "LastAck did not traverse the required three-hop paths")
    return events


def main():
    contract(CASE)
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        common.self_test(CASE)
        def alter_manifest(target, field, value):
            path = target / "output/run_result.json"
            record = json.loads(path.read_text())
            record[field] = value
            path.write_text(json.dumps(record))
        mutations = {
            "nonzero-exit": lambda target: alter_manifest(target, "exit_code", 1),
            "unbound-inputs": lambda target: alter_manifest(target, "inputs_sha256", {}),
            "unbound-outputs": lambda target: alter_manifest(target, "outputs_sha256", {}),
            "enabled-tp-retransmission": lambda target: (target / "network_attribute.txt").write_text(
                (target / "network_attribute.txt").read_text().replace(
                    'ns3::UbTransportChannel::EnableRetrans "false"',
                    'ns3::UbTransportChannel::EnableRetrans "true"')),
        }
        with tempfile.TemporaryDirectory() as directory:
            for name, mutate in mutations.items():
                target = common._mutate(CASE, Path(directory) / name, mutate)
                try:
                    contract(target)
                except common.CheckFailure as failure:
                    print(f"REJECTED {name}: {failure}")
                else:
                    raise common.CheckFailure(f"macro checker accepted {name}")
        print("PASS: four additional macro provenance/configuration rejection paths")
    else:
        common.check(CASE)
    print("PASS: 128 hosts, 16256 unique 64KiB tasks, 1065353216B, six three-hop LastAck sends, zero runtime drops, TP retransmission disabled")


if __name__ == "__main__":
    try:
        main()
    except (common.CheckFailure, OSError, ValueError, KeyError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
