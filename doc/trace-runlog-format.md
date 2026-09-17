# Unified Bus Trace Runlog Format

The Unified Bus trace writer emits comma-separated value (CSV) files under a
case's `runlog/` directory. Each file has one fixed header followed by data
rows. The header is the format contract; no per-run JSON schema is required.

This document is a human reference for the CSV headers. It does not replace
the CSV header itself.

## General Rules

- `time_ps` is an integer simulation timestamp in picoseconds.
- `time_ns` is an integer simulation timestamp in nanoseconds. It is used only
  by lifecycle traces.
- An empty field is not applicable or was not provided by the event. Empty and
  numeric zero are different values.
- Fields ending in `_bytes` are byte counts. Fields ending in `_bps` are bit
  rates.
- `node_X` and `port_Y` in a filename identify the writer owner and local
  port. They are not additional columns.
- CSV fields use normal CSV quoting. Semicolon-separated values inside one
  field are not separate CSV columns.

## TaskTrace

Files: `TaskTrace_node_*.csv`

For MEM tasks, the standard runner subscribes to `UbLdstInstance` start/completion events.
It does not also subscribe to the App's forwarded MEM events, which would write duplicate
rows for the same task. Public App trace sources remain available to custom observers.

Header:

```csv
time_ps,event,task_id,jetty_num,ta_ssn
```

| Field | Meaning |
| --- | --- |
| `time_ps` | Simulation time of the task event, in ps. |
| `event` | Task event name, such as `WQE Starts`, `WQE Completes`, `MEM Task Starts`, or `COMPUTE Task Completes`. |
| `task_id` | Identifier of the whole task. One task may contain multiple transaction operations. |
| `jetty_num` | Local Jetty identifier when the event is Jetty-scoped; empty otherwise. |
| `ta_ssn` | Transaction-operation sequence number when operation-scoped; empty for task-only events. |

Use `(task_id, ta_ssn)` for an operation-level join. Joining only on
`task_id` intentionally groups all operations in one task.

## PortTrace

Files: `PortTrace_node_*_port_*.csv`

Header:

```csv
time_ps,event,port_id,packet_size_bytes
```

`event` is normally `Port Tx` or `Port Rx`. `packet_size_bytes` is the
size reported by that port event. This trace is a port event stream and does
not contain transport or task metadata.

## QueueTrace

Files: `QueueTrace_node_*_port_*.csv`

Header:

```csv
time_ps,event,source,port_id,in_port,priority,out_port_voq_bytes,egress_bytes,total_bytes,ingress_occupancy_bytes
```

| Field | Meaning |
| --- | --- |
| `time_ps` | Simulation time of the queue observation, in ps. |
| `event` | Queue event family. The current writer uses `Queue Update`. |
| `source` | Producer or observation point: `SAMPLE`, `VOQ`, `ingress`, `EGRESS_ENQUEUE`, or `EGRESS_DEQUEUE`. |
| `port_id` | Output-port context for output/egress observations. For ingress records it is the input-port context used by the callback. |
| `in_port` | Input-port identifier for ingress-scoped rows; empty for aggregate output rows. |
| `priority` | Ingress queue priority for ingress-scoped rows; empty for aggregate output rows. |
| `out_port_voq_bytes` | Aggregate bytes held by all VOQ groups under this output port. Groups are keyed by `(outPort, priority, inPort)`; this is not one individual VOQ. |
| `egress_bytes` | Current bytes in the local egress queue for this output port. |
| `total_bytes` | `out_port_voq_bytes + egress_bytes` for output-port observations. Empty without an aggregate snapshot. |
| `ingress_occupancy_bytes` | Current post-update bytes held by the ingress queue identified by `(in_port, priority)`. It is an occupancy snapshot, not an enqueue/dequeue delta. |

`VOQ` and `SAMPLE` rows are aggregate snapshots. Ingress rows carry the
per-input `(in_port, priority)` occupancy in `ingress_occupancy_bytes`. Egress rows
use the callback's egress occupancy in `egress_bytes`; an empty
`ingress_occupancy_bytes` means the row has no ingress occupancy snapshot.

## PacketTrace

Files: `PacketTrace_node_*.csv`

Header:

```csv
time_ps,event,task_id,transport,src_tpn,dst_tpn,tp_msn,psn,port_id,src_node,dst_node,src_entity,dst_entity,vl,ta_ssn,out_port,payload_bytes,ta_opcode,packet_type,peer_type,traversal_node
```

| Field | Meaning |
| --- | --- |
| `time_ps` | Simulation time of the packet event, in ps. |
| `event` | Unified Bus packet event, for example `First Packet Sends`, `Last Packet ACKs`, or `Last Packet Traverses`. |
| `task_id` | Owning task identifier when available. |
| `transport` | Unified Bus path that produced the event, such as `TP`, `CTP`, or `LDST`. |
| `src_tpn`, `dst_tpn` | Source and destination transport-point numbers when available. |
| `tp_msn` | Transport message sequence number when available. |
| `psn` | Packet sequence number when available. |
| `port_id` | Local port associated with the event when available. |
| `src_node`, `dst_node` | Packet source and destination node identifiers when available. |
| `src_entity`, `dst_entity` | Transaction entity identifiers when available. |
| `vl` | Virtual-lane identifier from the packet or transaction route. |
| `ta_ssn` | Transaction-operation sequence number when available. |
| `out_port` | Output port selected by the current node when available. |
| `payload_bytes` | Payload size associated with the event, in bytes. |
| `ta_opcode` | Transaction operation code when available. |
| `packet_type` | Unified Bus packet type when available. |
| `peer_type` | Peer classification when available. |
| `traversal_node` | Node that emitted a `Last Packet Traverses` record. It is an event owner, not a hop count or routing decision. |

`PacketTrace` is an event stream for Unified Bus paths covered by these
callbacks. It is not a generic trace for every ns-3 protocol and does not mean
that every row is an RTP row.

## AllPacketTrace

Files: `AllPacketTrace_PKT_node_*.csv`, `AllPacketTrace_ACK_node_*.csv`,
and other packet-type files emitted by the same callback.

Header:

```csv
time_ps,packet_uid,psn,src,dst,src_tpn,dst_tpn,type,ack_info,size_bytes,task_id,path_nodes,recv_ports,send_ports,recv_times_ns,send_times_ns
```

| Field | Meaning |
| --- | --- |
| `time_ps` | Time at which the complete packet observation is emitted, in ps. |
| `packet_uid` | Packet identifier assigned by the simulation. |
| `psn` | Packet sequence number when available. |
| `src`, `dst` | Source and destination node identifiers. |
| `src_tpn`, `dst_tpn` | Source and destination transport-point numbers when available. |
| `type` | Packet type such as `PKT`, `ACK`, `NAK`, `SACK`, or `CONTROL`. |
| `ack_info` | ACK/NAK detail when available. |
| `size_bytes` | Packet size, in bytes. |
| `task_id` | Owning task identifier when available. |
| `path_nodes` | Node sequence observed for the packet. |
| `recv_ports` | Receive-port sequence aligned with `path_nodes`. |
| `send_ports` | Send-port sequence aligned with `path_nodes`. |
| `recv_times_ns` | Receive timestamps aligned with `path_nodes`, in ns. |
| `send_times_ns` | Send timestamps aligned with `path_nodes`, in ns. |

The five path fields are semicolon-separated arrays inside one CSV cell. The
same array index is the same path observation. An endpoint can have an empty
receive or send port/time because that direction does not apply at that end.
The writer keeps this compact one-row representation; analysis code may split
it into one row per hop as a derived view.

## TPGPhaseTrace

File: `TPGPhaseTrace.csv`

Header:

```csv
time_ps,src_node,dst_node,task_id,member_count,initial_index,initial_port,strategy,member_ports
```

`member_ports` is a semicolon-separated list of member ports. The file is
created only when the TPG trace gate is enabled and a TPG phase event occurs.

## PFC Traces

Files: `PfcTrace_node_*_port_*.csv` and
`PfcDynamicTrace_node_*_port_*.csv`.

Headers:

```csv
time_ps,event,action,priority,ingress_bytes
time_ps,event,action,priority,ingress_total_bytes,shared_used_bytes,headroom_used_bytes,xoff_bytes,xon_bytes
```

The first file records fixed-PFC actions and the ingress byte amount. The
second records dynamic-PFC accounting and whether the action pauses or resumes
the priority. All byte fields are bytes; `priority` is a scheduler priority
and is not renamed to `vl` here.

## CBFC Trace

Files: `CbfcTrace_node_*_port_*.csv`

Header:

```csv
time_ps,event,action,reason,vl,available_credit_bytes,next_packet_bytes,delta_credit_bytes,trigger_threshold_bytes,emit_min_pending_bytes,credit_bytes,pending_credit_bytes,send_credit_bytes,credit_cell_bytes
```

All credit quantities in this file are bytes. The producer passes the current
cell geometry for the port, and the writer records it in `credit_cell_bytes`
on every row. The conversion is:

```text
credit_bytes = credit_cells * credit_cell_bytes
credit_cell_bytes = CbfcFlitLenByte * CbfcFlitsPerCell
```

Control-frame pending/send values are first converted to cell-equivalent
amounts using the configured control return grain. This keeps the CSV in bytes
without exposing a second cell-valued column family.

| Field | Meaning |
| --- | --- |
| `time_ps` | Event time, in ps. |
| `event` | `CBFC`. |
| `action` | `BLOCK`, `RESUME`, `CREDIT_LEVEL`, `CREDIT_RESTORE`, or `CONTROL_SEND`. |
| `reason` | Producer reason for a level change or control-frame send. |
| `vl` | VL row index. Vector values are emitted one row per VL. Scalar events use the credit array index passed by CBFC. |
| `available_credit_bytes` | Current available credit for the row's VL, in bytes. |
| `next_packet_bytes` | Size of the next packet considered by admission, in bytes. |
| `delta_credit_bytes` | Signed change in available credit, in bytes. |
| `trigger_threshold_bytes` | Control-return threshold, in bytes. |
| `emit_min_pending_bytes` | Minimum pending credit required to emit a control update, in bytes. |
| `credit_bytes` | Credit amount carried by a restore observation, in bytes. |
| `pending_credit_bytes` | Pending return credit for the row's VL, in bytes. |
| `send_credit_bytes` | Credit for the row's VL carried by the control frame, in bytes. |
| `credit_cell_bytes` | Runtime cell size used for this row, in bytes. |

## Congestion-Control Traces

DCQCN files and headers:

```csv
time_ps,event,out_port,total_queue_bytes,mark_probability
time_ps,event,action,ecn,location
time_ps,event,reason,alpha,current_rate_bps,target_rate_bps,bytes_since_last_increase,time_recovery_events,byte_recovery_events
```

These are respectively mark, CNP, and sender-state records. `location` is
the producer's location flag; it is not a node identifier.

CAQM files and headers:

```csv
time_ps,event,psn_start,psn_end,ce,ie,hint_e
time_ps,event,psn,sequence,in_flight,cwnd,ce,ie,hint
```

These are respectively ACK feedback and sender-state records. The `ce`,
`ie`, and hint fields retain the protocol's encoded values.

## TP Debug Trace

Files: `TpDebugTrace_node_*_tpn_*.csv`

Header:

```csv
time_ps,event,reason,psn_snd_nxt,psn_snd_una,inflight_packets,max_inflight_packets,inflight_limited,cc_limited,send_window_limited,active_segments,total_segments,ack_queue_len,cnp_queue_len
```

Counters are snapshots from the transport sender. The three `*_limited`
fields are boolean values encoded as `0` or `1`.

## LifecycleTrace

Files: `LifecycleTrace_node_*.csv` (with an optional rank suffix in MPI mode).

Header:

```csv
time_ns,ownerNode,ownerSequence,recordType,resourceKind,localId,generation,subjectKind,subjectId,event,before,delta,after,outcome,reason,siteId
```

`recordType`, `resourceKind`, `subjectKind`, `event`, `outcome`,
`reason`, and `siteId` are numeric enum values defined in
`src/unified-bus/model/ub-lifecycle-trace.h`. Stable values currently used
by the writer include:

| Enum | Values |
| --- | --- |
| `recordType` | `1 RESOURCE_STATE`, `2 LEDGER_DELTA`, `4 TERMINAL_OUTCOME`, `5 SUMMARY` |
| `subjectKind` | `0 NONE`, `1 RESOURCE`, `3 LEASE`, `4 WORK` |
| `outcome` | `0 NONE`, `1 COMPLETED`, `2 FAILED`, `3 CANCELED`, `4 FLUSHED` |
| `resourceKind` | `1 RTP_ENDPOINT`, `2 WQE`, `3 TRANSACTION_OPERATION`, `4 TRANSACTION_SEGMENT`, `5 JETTY`, `6 RTP_TPG`, `7 RTP_CONNECTION`, `8 CTP_SERVICE`, `9 LDST_INSTANCE`, `10 LDST_SEGMENT` |

The event, reason, and site registries are kept in the same C++ enum header;
the CSV does not emit a separate lifecycle schema file. `before`, `delta`,
and `after` are signed lifecycle counters. `ownerSequence` is monotonic
within an owner node. `localId` plus `generation` distinguishes reused
local resource identifiers.

## Joining Traces

- Task-level joins use `task_id`.
- Transaction-operation joins use `(task_id, ta_ssn)` when both are present.
- Packet-level joins use `packet_uid`; use `psn` when packet sequence is also needed.
- `PacketTrace` is an event stream. `AllPacketTrace` is a packet-level path summary. They answer different questions and are not interchangeable.
- `path_nodes`, `recv_ports`, `send_ports`, `recv_times_ns`, and `send_times_ns` are aligned arrays, not independent lists.

## File Selection and Compatibility

For a logical trace basename, tools should select at most one representation in
this order:

1. uncompressed `.csv`;
2. compressed `.csv.gz`;
3. legacy prose `.tr`;
4. compressed legacy `.tr.gz`.

This prevents a stale legacy file from being read when a canonical CSV exists.
The CSV header remains authoritative for the CSV representation. Legacy `.tr`
files remain readable for historical cases; they do not acquire the new column
names retroactively.

Trace gates permit recording but do not create modeled events. For example, a
TPG or congestion-control file may be absent when its corresponding event never
occurs in the case.
