# 2nodes_reliable-rerouting

Checked-in demonstration of UB 2.1 packet-level reliable rerouting. The design record is
maintained outside the released tree.

## Topology

```text
Host 0 -- port0 [Switch 2] port1 ===== port1 [Switch 3] port0 -- Host 1
                          port2 ===== port2
                               alternate
```

* Node 2 port 1 and node 3 port 1 are the same physical link; `link_fault.csv` fails it at 100 us.
* Both switches route towards the remote host over the equal-cost candidates `1 2` with
  `PER_FLOW_SHORTEST_PATHS` and `INGRESS_PORT_STRIPE`, so the host-facing ingress port 0 selects
  candidate 1 before detection and only candidate 2 afterwards.
* Both host-facing switch ingresses are port 0, so no scenario depends on a lucky hash.

## Files

| File | Role |
| --- | --- |
| `node.csv`, `topology.csv`, `routing_table.csv`, `traffic.csv` | Case inputs |
| `link_fault.csv` | Optional file-driven failure schedule: `nodeId,portId,failTimeNs` |
| `network_attribute.txt` | Full resolved snapshot of the runtime parameter catalog |
| `check_results.py` | Automated recovery-evidence checker |

`network_attribute.txt` is generated from the project parameter catalog
(`.openusim/project/parameter-catalog.json`, produced from `--PrintTypeIds`,
`--ClassName=<name>` and `--PrintUbGlobals`) as a full snapshot, not as a patch inherited from
another case. Key choices:

| Parameter | Value | Why |
| --- | --- | --- |
| `ns3::UbLink::EnableReliableRerouting` | `true` | Arms eligible switch-to-switch links only; the host access links stay ordinary |
| `ns3::UbLink::FailureDetectionDelay` | `+5us` | Positive modeled detection interval |
| `ns3::UbLink::LastAckDelay` | `+10us` | Positive modeled LastAck interval |
| `ns3::UbRoutingProcess::MultipathSelector` | `INGRESS_PORT_STRIPE` | Deterministic candidate choice |
| `ns3::UbTransportChannel::EnableRetrans` | `false` | The primary run proves link recovery without transport resend |
| `ns3::UbSwitch::FlowControl` | `NONE` | Bounded storage comes from the queue-manager reserve |
| `ns3::UbQueueManager::ReservePerQueueBytes` | `8388608` | Enough ingress admission for the demonstration load |

## Run

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_reliable-rerouting'
python3 scratch/2nodes_reliable-rerouting/check_results.py
python3 scratch/2nodes_reliable-rerouting/check_results.py --self-test
```

The second command validates the positive run. The third re-runs the positive path plus seven
rejection paths (missing/truncated evidence, missing task, wrong detection time, zero recovery in one
direction, duplicate transfer, false drain) on isolated copies.

## Measured invariants (2026-09-17, baseline revision `8be8ad8f`)

| Observation | Value |
| --- | --- |
| Failure instant | 100 000 000 ps (100 us) |
| Endpoint detection | 105 000 000 ps (failure + 5 us) |
| Modeled LastAck | 115 000 000 ps (detection + 10 us) |
| Unreceived recovery work at detection | 191 retained packets per direction (8472 bytes shown as the first row) |
| Recovery transfers | 382 (191 per direction), all `old_output=1 -> new_output=2`, all at the LastAck instant |
| Duplicate local attempts | 0 |
| Local drain | both endpoints drained with `retry_packets=0`, `pending_packets=0` |
| Tasks | 4 / 4 completed (1 MiB per task per direction, ~487 Gbps) |

## Scope notes

* Local drain is not destination completion: the checker verifies both, separately.
* Version one excludes host access-link recovery, multiple distinct failures, link repair,
  single-direction faults, MPI/MTP recovery, finite retry-buffer hardware behavior, flit-level
  retry, and real LastAck encoding. See the design document for the complete list.
