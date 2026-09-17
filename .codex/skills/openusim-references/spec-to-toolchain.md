# Spec to Toolchain Mapping

<reference-hint>
<use-when>Use this reference when translating a stable `experiment-spec.md` into concrete tool invocations during the run stage.</use-when>
<focus>How spec fields map into topology scripts, workload generation, and simulator execution.</focus>
<keywords>spec, toolchain, net_sim_builder.py, build_traffic.py, run stage</keywords>
</reference-hint>

## Contents

- Core Principle
- Topology Slot -> Topology Generation
- Workload Slot -> Traffic Generation
- Network Overrides Slot -> network_attribute.txt
- Routing Intent Slot -> Route Generation
- Transport Channel Mode Slot -> Case Expectations
- Observability Slot -> Observability Overrides
- Execution
- Spec Parameter Name Convention

## Core Principle

**Always generate a new Python builder function** in the case directory that returns
`NetworkSimulationGraph`. Do NOT copy or modify existing example scripts (`user_topo_*.py`). These are
code templates for reference only, not reusable tools. Prefer prepared structured cases for new
auto-generated routing cases; use CSV generation only when the experiment explicitly requires CSV as
its topology input or needs a manual route table.

## Node Delay Fields

- `node.csv` `forwardDelay` maps to `ns3::UbSwitch::InPortProcessingDelay`.
- `node.csv` `allocationDelay` maps to `ns3::UbSwitchAllocator::AllocationTime`.
- `forwardDelay` is fixed non-blocking in-port processing latency after route/output are known and before a forwarded packet becomes visible in VOQ to the allocator.
- Buffer Occupancy includes processing + VOQ + egress queued bytes. DCQCN/CAQM use Queue Backlog (VOQ + egress), excluding processing. Ingress admission and PFC still account for processing residency.
- `allocationDelay` is allocator arbitration latency. It is the only CSV field that overrides `AllocationTime`.
- Do not use `forwardDelay` to model allocator scheduling delay.

## Topology Slot → Topology Generation

### Process

1. **Read topology family and parameters** from `experiment-spec.md`
2. **Select the matching code template** from `topology-options.md` Generation Patterns section
3. **Generate a new script** `{case_dir}/generate_topology.py`:
   - Copy the complete pattern from `topology-options.md`
   - Substitute spec parameters (host_num, leaf_sw_num, etc.)
   - Expose `build_topology()` and return the graph
   - Adjust bandwidth/delay if specified in Network Overrides
   - Adjust priority_list if specified in Network Overrides
4. **For prepared structured routing**, record two commands in the manifest. First prepare:
   `python3 scratch/ns-3-ub-tools/ub_tools.py topology build {case_dir}/generate_topology.py --output-dir {case_dir} --route-generation mixed`.
   Then run: `python3 scratch/run_structured_topo_case.py --case-path {case_dir} --route-runtime-mode compressed-table-lookup`.
5. **For CSV output**, use the same generated builder with
   `python3 scratch/ns-3-ub-tools/ub_tools.py topology build {case_dir}/generate_topology.py --output-dir {case_dir} --case-format csv`.
   This defaults to full route generation; structured and mixed route generation require the
   structured case format. The original scripts that call `write_config(...)` remain the independent
   v1 path.
6. After topology, `network_attribute.txt`, and `traffic.csv` are present, run
   `python3 scratch/ns-3-ub-tools/ub_tools.py case check {case_dir}`. The check requires those shared
   runtime inputs and also requires `routing_table.csv` for CSV cases.

### Important Rules

- **Do NOT** copy or modify `scratch/ns-3-ub-tools/user_topo_*.py` scripts
- **Do NOT** run `net_sim_builder.py` directly (it's a library, not a CLI)
- **Do NOT** reuse scripts across cases (each case gets its own generated script)
- **Always** use the complete pattern from `topology-options.md`, not abbreviated code
- When CSV output is selected, write it into `{case_dir}/`, not an extra timestamped output directory
- Do not export diagnostic CSV from the prepared path unless the experiment evidence plan requires it

### Example

**From spec:**
```yaml
Topology:
  family: clos-spine-leaf
  host_num: 64
  leaf_sw_num: 8

Network Overrides:
  bandwidth: 200Gbps
  delay: 10ns
```

**Generate `{case_dir}/generate_topology.py`:**
```python
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent / "scratch/ns-3-ub-tools"))

import net_sim_builder as netsim
import networkx as nx

def all_shortest_paths(G, source, target):
    try:
        return nx.all_shortest_paths(G, source, target)
    except nx.NetworkXNoPath:
        return []

if __name__ == '__main__':
    graph = netsim.NetworkSimulationGraph()
    graph.output_dir = str(Path(__file__).parent) + "/"

    # Parameters from experiment-spec.md
    host_num = 64
    leaf_sw_num = 8
    spine_sw_num = host_num // leaf_sw_num

    # Add hosts (0 to host_num-1)
    for host_id in range(host_num):
        graph.add_netisim_host(host_id, forward_delay='1ns')

    # Add leaf switches
    for leaf_idx in range(leaf_sw_num):
        graph.add_netisim_node(host_num + leaf_idx, forward_delay='1ns')

    # Add spine switches
    for spine_idx in range(spine_sw_num):
        graph.add_netisim_node(host_num + leaf_sw_num + spine_idx, forward_delay='1ns')

    # Connect hosts to leaves (bandwidth/delay from Network Overrides)
    host_per_leaf = host_num // leaf_sw_num
    for host_id in range(host_num):
        leaf_id = host_num + (host_id // host_per_leaf)
        graph.add_netisim_edge(host_id, leaf_id, bandwidth='200Gbps', delay='10ns')

    # Connect leaves to spines (full mesh)
    for leaf_idx in range(leaf_sw_num):
        for spine_idx in range(spine_sw_num):
            leaf_id = host_num + leaf_idx
            spine_id = host_num + leaf_sw_num + spine_idx
            graph.add_netisim_edge(leaf_id, spine_id, bandwidth='200Gbps', delay='10ns')

    # Generate config files
    graph.build_graph_config()
    graph.gen_compressed_route_table(path_finding_algo=all_shortest_paths, multiple_workers=4)
    graph.write_config(include_transport=False)
```

**Then run:**
```bash
cd {case_dir}
python3 generate_topology.py
```

### Heterogeneous link bandwidth

When the spec specifies different bandwidths per link layer, modify the `add_netisim_edge` calls accordingly:

```python
    # Host-leaf links: lower bandwidth
    for host_id in range(host_num):
        leaf_id = host_num + (host_id // host_per_leaf)
        graph.add_netisim_edge(host_id, leaf_id, bandwidth='200Gbps', delay='10ns')

    # Leaf-spine links: higher bandwidth
    for leaf_idx in range(leaf_sw_num):
        for spine_idx in range(spine_sw_num):
            leaf_id = host_num + leaf_idx
            spine_id = host_num + leaf_sw_num + spine_idx
            graph.add_netisim_edge(leaf_id, spine_id, bandwidth='400Gbps', delay='20ns')
```

## Workload Slot → Traffic Generation

### Built-in collective algorithms

For a supported collective, map the spec to the v2 collective and algorithm names:

```
python3 scratch/ns-3-ub-tools/ub_tools.py traffic {collective} \
  --host-count {host_num} \
  --group-size {group_size} \
  --data-size {data_size} \
  --algorithm {algorithm} \
  --group-layout {group_layout} \
  --phase-delay {phase_delay} \
  --output-dir {case_dir}
```

The command validates the collective schedule before it writes `{case_dir}/traffic.csv`. Use
`--placement-file` instead of `--group-layout` for an explicit group/rank/node mapping. AllToAll
`pairwise` additionally accepts `--peers-per-phase`; AllToAllV uses `--distribution` and its
related matrix/seed options. AllToAllV `--include-local` only emits self tasks during lowering;
it does not alter the schedule. `scatter` is not a current AllToAll algorithm name.

For an existing SimAI flow export, use
`python3 scratch/ns-3-ub-tools/ub_tools.py traffic convert-simai FLOW.jsonl --output {case_dir}/traffic.csv`.
This is an import path rather than collective algorithm selection, and it refuses to overwrite an
existing file unless `--overwrite` is supplied.

### Reference traffic.csv

If the workload is a reference `traffic.csv`, copy it into the case directory.

### Custom traffic skeleton

If the spec workload is `custom-traffic-skeleton`, generate `{case_dir}/traffic.csv` directly by following the decomposition steps in `workload-options.md`. The generated CSV must conform to the `traffic.csv Schema` section in that document.

Use the Write tool to create the CSV. Verify:
- Header is the nine-column base form, base + `srcPortId,dstPortId`, base +
  `atomicSubOperation,requestPayloadBytes`, or base + ports + atomic options (in that order).
  The two 11-column forms are distinguished by exact names; the combined form has 13 columns.
  Entity-bearing headers remain invalid; every row must match its header width.
- Atomic `dataSize(Byte)` is N (1/2/4/8/16/32/64), while explicit `requestPayloadBytes` is P
  (1..4096), overriding defaults CAS=2N and other atomic=N. Load/Store requires one of the eight
  local-only suboperations; see `workload-options.md` for all nine names and the N/P/S contract.
  Use the reference atomic CSVs or a custom skeleton; the collective generator stays Write-only.
- `taskId` is 0-indexed and sequential
- `sourceNode` and `destNode` are valid host IDs for the topology (0 to `host_num - 1`)
- `srcPortId` and `dstPortId`, when present, are valid endpoint ports; each may be empty independently, and an explicit `0` means port 0
- `opType` is a valid value from the runtime traffic-op parser (see `workload-options.md`
  "Valid `opType` values" / "URMA request operation names" and `ParseOpLocked` in
  `src/unified-bus/model/ub-traffic-gen.cc`); every accepted URMA name must also satisfy
  `UbTrafficGen::IsUrmaTask`, and the six references under
  `scratch/2nodes_transaction_extensions/` show the transport/operation split. MegaMoE cases may
  additionally use `COMPUTE` and `LOCAL_READY`
- `dependOnPhases` is empty string for first-phase tasks, space-separated phaseIds otherwise

### Multi-collective composition

If the spec combines multiple collectives:
1. Generate each collective's traffic separately (via `build_traffic.py` or hand-written).
2. Merge into a single `traffic.csv` following the composition rules in `workload-options.md`.
3. Write the merged result to `{case_dir}/traffic.csv`.

## Network Overrides Slot → network_attribute.txt

Use the `write_network_attributes()` function from
`.codex/skills/openusim-run-experiment/scripts/openusim_run_experiment/network_attribute_writer.py`:

```python
from openusim_run_experiment.network_attribute_writer import write_network_attributes

write_network_attributes(
    case_dir=Path("<case_dir>"),
    explicit_overrides={"ns3::UbApp::SomeParam": "value", ...},
    observability_overrides={"ns3::UbApp::TraceParam": "value", ...},
)
```

This writes a full catalog snapshot. Explicit overrides take precedence over observability overrides.
Unknown override keys must fail before execution. Do not write keys that are not in the runtime catalog or the documented UB-global fallback surface.

Runtime-catalog boundary:
- Treat the runtime catalog as the current build + current `scratch/ub-quick-example` introspection surface.
- Treat it as authoritative for `ns3::...` attribute keys.
- Runtime queries use `--no-build`; build before refreshing the catalog. `runtime-query-v2`
  preserves empty string defaults (including packet trace controls) and invalidates v1 caches.
- For UB globals, prefer `--PrintUbGlobals`; if that surface is unavailable in the current build, fall back only to the documented minimum UB-global set used by the project tooling.

### LDST network format

`ns3::UbLdstApi::HeaderConfig` and `ns3::UbLdstEndpoint::HeaderConfig` are independent
attributes, both defaulting to `9` and accepting only `7` or `9` (enum checker, not 7..9).
They select outgoing requests; receivers parse the packet and responses retain the request
CFG, including delayed replies. The CSV MEM task path uses Api/Instance/Thread; direct
Endpoint behavior is covered by the module integration suite.

| HeaderConfig | Network header | NLP | Transaction representation |
| --- | --- | --- | --- |
| 9 | CNA16 | 0 | Existing compact LDST TP bypass |
| 7 | CNA24 | 4 | Same compact headers and payload semantics |

CFG7/NLP4 is a simulator-local extension, not a standardized CFG7 bypass combination.
CFG7/NLP0 remains CTP. Requests use validated simulator node/optional-port CNA mapping;
CNA16 supports node 0..4095 and port 0..14, CNA24 node 0..65535 and port 0..254.
Explicit port 0 is distinct from an unspecified port. Api aborts on unencodable addresses;
Endpoint returns false without creating inflight state. Example: `scratch/2nodes_ldst-cfg7/`.

### Resolving natural-language parameter intent

When the user describes a parameter change as a behavioral intent (e.g. "开启流控", "启用可靠传输", "buffer 大一点") rather than a concrete `key = value`, resolve it through the runtime catalog:

1. **Identify the target module.** The user's intent usually implies a component: "流控" → UbSwitch, "可靠传输" → UbTransportChannel, "路由" → UbRoutingProcess, "拥塞控制" → UB_CC globals.

2. **Search catalog descriptions.** Read `.openusim/project/parameter-catalog.json` (or call `load_or_build_parameter_catalog()`). For each entry whose `module` matches the target component, check whether its `description` field matches the user's intent. Present the matching entries to the user:
   - parameter key
   - description
   - current default value
   - value type (Boolean, Enum, Integer, ...)

3. **If the parameter is an enum, present all valid values.** The catalog `description` typically lists them (e.g. "NONE, CBFC, CBFC_SHARED, or PFC"). If the description does not list them, grep the C++ source for `MakeEnumChecker` on that attribute (see "Discovering valid values" below).

4. **If the intent is a relative adjustment** ("大一点", "翻倍"), show the current default and ask the user for a concrete target value.

5. **If no catalog entry matches the intent**, the parameter may not exist in the current build. Tell the user and ask them to be more specific or check whether the feature is supported.

6. **Confirm the resolved `key = value` with the user** before writing to the spec.

### Parameter value validation boundary

The skill-layer toolchain validates **parameter keys** (whether a key exists in the catalog) but does **not** validate **parameter values** (whether the value is within a valid range or matches an expected enum). Invalid values will be written to `network_attribute.txt` and only caught by ns-3 at runtime.

#### Discovering valid values

Current examples from this repo's source of truth:

- `ns3::UbSwitch::FlowControl`: `NONE`, `CBFC`, `CBFC_SHARED`, `PFC_FIXED`, `PFC_DYNAMIC`, `PFC_DYNAMIC_PAPER`
- `ns3::UbQueueManager::DynamicPfcResumeGapBytes`: dynamic PFC xon/xoff gap in bytes
- `ns3::UbQueueManager::PaperDynamicPfcBeta`: paper-style dynamic PFC beta used by `PFC_DYNAMIC_PAPER`, the DCQCN paper reproduction mode for **"Congestion Control for Large-Scale RDMA Deployments"** (SIGCOMM 2015)

#### Legacy network attribute keys

Do not write these older keys into `network_attribute.txt`; current `UbUtils::SetComponentsAttribute()` rejects them before `ConfigStore` loads the file.

| Legacy key | Current key/value |
|------------|-------------------|
| `ns3::UbQueueManager::ResumeOffset` | `ns3::UbQueueManager::DynamicPfcResumeGapBytes` |
| `ns3::UbSwitch::EnableCBFC` | `ns3::UbSwitch::FlowControl "CBFC"` |
| `ns3::UbSwitch::EnablePFC` | `ns3::UbSwitch::FlowControl "PFC_FIXED"` or `"PFC_DYNAMIC"` |
| `ns3::UbApiThread::*` | `ns3::UbLdstThread::*` |

Routing has a separate temporary read-compatibility window at
`UbUtils::SetComponentsAttribute()`. Old `UsePacketSpray` / `UseShortestPaths` pairs and
`UbRoutingProcess::RoutingAlgorithm` are translated before ConfigStore, with one warning that
contains complete canonical replacement lines and states that compatibility may stop working in a
future release. New cases must write only `RoutingType` and `MultipathSelector`; model APIs do not
expose the legacy fields.

When the discussion turns from "which knob exists" to "what these congestion/PFC knobs mean in queue dynamics", consult:

- `congestion-control-and-pfc-lessons.md`

The runtime parameter catalog (`.openusim/project/parameter-catalog.json`) stores a `description` field for each entry. For enum-type attributes, the description typically lists valid values (e.g. "The flow control mechanism to use (NONE, CBFC, CBFC_SHARED, or PFC)."). Consult the catalog description before accepting a user-provided value.

For values not discoverable from the catalog, check the C++ source:

| What to verify | Source of truth in C++ | How to find |
|----------------|----------------------|-------------|
| Enum attribute values (FlowControl, RoutingType, MultipathSelector, VlScheduler, ...) | `MakeEnumChecker(...)` calls in the corresponding `GetTypeId()` | Grep for `MakeEnumChecker` in `src/unified-bus/model/` |
| GlobalValue enum values (UB_CC_ALGO, ...) | `MakeEnumChecker(...)` in `GlobalValue(...)` definitions | Grep for `GlobalValue.*UB_` in `src/unified-bus/` |
| Boolean attributes | `MakeBooleanChecker()` in `GetTypeId()` | Values are always `true` / `false` |
| Integer attributes with range | `MakeUintegerChecker<T>()` or `MakeIntegerChecker<T>(min, max)` | Grep for the attribute name in `src/unified-bus/` |
| traffic.csv `opType` values | `UbTrafficGen::ParseOpLocked` in `src/unified-bus/model/ub-traffic-gen.cc` plus `UbApp::SendTraffic` | Grep for `ParseOpLocked` and `RuntimeTaskOp` |

#### Risk mitigation during planning

If the user requests a value for an enum parameter, verify it against the C++ source or catalog description before writing to `network_attribute.txt`. If the user provides a suspicious value (negative number, missing unit, string for a numeric field), warn them that the skill layer has no runtime value check and the error will only surface when ns-3 starts.

## Routing Intent Slot → Route Generation

Use `routing-strategy-selection.md` to choose a scenario-appropriate profile. This section owns the
mechanical mapping from the confirmed profile to case attributes and route generation.

- `routing_type` maps to the `RoutingType` attribute on `UbApp`, `UbTransportChannel`, and `UbLdstApi` as applicable:
  - `PER_FLOW_ALL_PATHS` = RT bits `00`
  - `PER_PACKET_ALL_PATHS` = RT bits `01`
  - `PER_FLOW_SHORTEST_PATHS` = RT bits `10`
  - `PER_PACKET_SHORTEST_PATHS` = RT bits `11`
- `multipath_selector` maps to `default ns3::UbRoutingProcess::MultipathSelector "..."`.
- Valid per-flow selectors: `HASH64`, `CRC32`, `TOEPLITZ`, `INGRESS_PORT_STRIPE`.
- Valid per-packet selectors: `HASH64`, `CRC32`, `TOEPLITZ`, `ROUND_ROBIN`, `ADAPTIVE`.
- Hash selectors consume the complete 17-byte routing key once: `sip`, `dip`, `sport/Lb`, `dport`, `priority`, and node salt. Do not add a separate entropy or packet-ordinal field.
- `INGRESS_PORT_STRIPE` maps ingress port modulo candidate count; local injection falls back to full-key `HASH64`.
- Invalid routing-type/selector combinations are rejected during `network_attribute.txt` loading.
- `path_source = auto-path-finder` uses a prepared structured case by default. Call
  `graph.gen_compressed_route_table(...)` only when the selected topology input is CSV table routing.
- For a prepared structured case, `ub_tools.py topology build` calls
  `graph.prepare_structured_case(case_path=..., route_generation=...)`; it discovers and verifies the
  physical graph and writes `structured_topology.txt` plus `compressed_routing.bin` without
  intermediate topology or route CSV. `run_structured_topo_case.py` later launches ns-3 using only
  the prepared case directory. `--route-runtime-mode=compressed-table-lookup` loads the binary index;
  `structured-rule-compute` evaluates verified rules without the complete route matrix.
- Complete verified structures emit coordinate/group topology rules. Partly recognized graphs build
  destination DAGs and exact/range rules without a complete baseline table. A custom path finder keeps
  complete baseline equivalence validation and content caching.
- For an existing custom case with explicit endpoint ports, pass the case directory to
  `ub_tools.py topology build` rather than repacking ports with `build_graph_config()`.
- `mixed` assigns each destination-port lookup to either rules or table entries; candidate sets are not
  merged. `rule` fails if any lookup still needs table routing.
- `graph.gen_route_table(...)` is only for cases that explicitly need exact per-destination rows plus precomputed transport channels.
- `graph.gen_2layer_clos_compressed_route_table(host_num, leaf_sw_num)` is an explicit two-layer Clos optimization, not the default new-case path.
- `path_source = manual-route-table` means the topology script should use `graph.set_route_table(...)` for bounded route entries or directly write `routing_table.csv`

`routing_table.csv` supports compressed ranges in `nodeId` and `dstNodeId` using `a..b`. Runtime exact routes win over range routes. Range routes match port-scoped destination IPs by converting the packet destination IP back to node ID, while still using `dstPortId` as part of the route key. The generic compressed generator compresses each `dstPortId` independently and rejects conflicting range overlaps instead of silently choosing one route.

When the user asks for a non-template topology but provides bounded node/link facts, use the `custom-graph` pattern from `topology-options.md` instead of rejecting the request.

## Transport Channel Mode Slot → Case Expectations

- Default to `on-demand`; do not ask the user to precompute TP mappings unless they explicitly want fixed TP ids / priorities / endpoint pairs.
- `precomputed`: topology generation should call `config_transport_channel(...)` and `graph.write_config(include_transport=True)`; the case checker should require `transport_channel.csv`
- `on-demand`: `transport_channel.csv` may be omitted; topology generation should call `graph.write_config(include_transport=False)` so large cases do not precompute host-pair TP mappings. Compressed route generators require this mode.
- `traffic.csv` may include trailing `srcPortId,dstPortId`. Empty port cells are independent
  wildcards and explicit `0` means port 0. Traffic never selects CTP Entity IDs.
- The runner internally creates Entity 1 on each Host with all local CNA16-encodable UB ports;
  Entity membership is not a case-file input.
- Advanced multi-Entity cases select `LocalEntityId` and `PeerEntityId` through separate
  `UbApp`/Jetty configurations and the controller API. RTP TP records remain protocol-internal.
- CTP multipath remains entity based internally and should not be represented by multiple RTP TP rows.

## Observability Slot → Observability Overrides

```python
from openusim_run_experiment.network_attribute_writer import observability_preset

# Use a named tier
overrides = observability_preset("balanced")

# Or customize: start from a tier, then tweak
overrides = observability_preset("minimal")
overrides["UB_PORT_TRACE_ENABLE"] = "true"  # add port trace to minimal

# Lifecycle DETAIL is custom-only. Sampling is deterministic by complete identity chain.
overrides["UB_TRACE_PRESET"] = "custom"
overrides["UB_LIFECYCLE_TRACE_LEVEL"] = "DETAIL"
overrides["UB_LIFECYCLE_DETAIL_SAMPLE_STRIDE"] = "4"
overrides["UB_LIFECYCLE_DETAIL_SAMPLE_OFFSET"] = "0"

# Optional host-I/O tuning. These values do not alter simulated network semantics.
overrides["UB_TRACE_CHUNK_BYTES"] = str(64 * 1024)
overrides["UB_TRACE_OWNER_HIGH_WATER_BYTES"] = str(1024 * 1024)
overrides["UB_TRACE_OWNER_LOW_WATER_BYTES"] = str(512 * 1024)
overrides["UB_TRACE_MAX_OPEN_FILES"] = "1024"

# Pass to write_network_attributes
write_network_attributes(
    case_dir=Path("<case_dir>"),
    explicit_overrides={...},
    observability_overrides=overrides,
)
```

Refer to `trace-observability.md` for tier semantics and safe wording constraints.

Validate lifecycle artifacts independently after a run:

```bash
python3 scratch/ns-3-ub-tools/trace_analysis/lifecycle_trace.py {case_dir}
```

The lifecycle parser requires the C++-emitted schema metadata and rejects missing metadata,
unsupported versions or enum values, and inconsistent MPI-rank registries. It does not replace the
existing task or throughput parsers.

## Execution

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path={case_dir}'
```

Optional runtime switches (not part of experiment definition):
- `--mtp-threads=N`

## Spec Parameter Name Convention

Use toolchain-native parameter names in `experiment-spec.md`:
- `host_num` (not `host_count` or `num_hosts`)
- `leaf_sw_num` (not `leaf_count`)
- `comm_domain_size` (not `comm_size`)
- `data_size` with unit (e.g. `1GB`)
- `routing_type`, `multipath_selector`, `path_source`, `transport_channel_mode`

This avoids translation ambiguity at the plan→run boundary.
