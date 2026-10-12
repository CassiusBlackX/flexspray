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
| `ns3::UbLink::LastAckTimeout` | `+1s` | Receive deadline for a real LastAck |
| `ns3::UbRoutingProcess::MultipathSelector` | `INGRESS_PORT_STRIPE` | Deterministic candidate choice |
| `ns3::UbTransportChannel::EnableRetrans` | `false` | The primary run proves link recovery without transport resend |
| `ns3::UbSwitch::FlowControl` | `NONE` | Bounded storage comes from the queue-manager reserve |
| `ns3::UbQueueManager::ReservePerQueueBytes` | `8388608` | Enough ingress admission for the demonstration load |

## Run

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/reliable-rerouting/2nodes_reliable-rerouting'
python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py
python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py --self-test
```

The second command validates the positive run. The third checks the positive evidence and
13 mutations, including missing actual sends, impossible paths, unknown copy/source-port identity,
changed receive boundary, missing tasks, early recovery, duplicate transfer and false drain (including unfinished input processing).

## Verified real-handshake run (2026-09-24)

The original topology and four 2 MiB tasks are preserved. Failure occurs at 100 us and both
endpoints detect it at 105 us. Each endpoint sends three real LastAck packets over port 2;
only its first valid incoming copy authorizes recovery. The checker reconstructs transmission,
serialization and propagation from the actual physical path. There are 382 transfers
(191 per direction), and all four tasks complete. Local drain and destination completion are
checked separately. Results are in this case's `output/` and `runlog/` after execution.

The old 2026-09-17 demonstration at revision `8be8ad8f` used a synthetic arrival at 115 us;
that historical timing is not an acceptance criterion for the real network handshake.

## Scope notes

Host access-link recovery, multiple distinct failures, repair, one-way faults, MPI/MTP,
finite retry-buffer hardware and flit-level retry remain outside scope. LastAck has real
CNA16/CNA24 encoding and packet-level boundaries, with the model's no-bit-corruption assumption.
See [ADR 0027](../../../doc/adr/0027-use-packet-boundaries-for-routed-lastack.md).
