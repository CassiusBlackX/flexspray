# Trace Observability

<reference-hint>
<use-when>Use this reference when discussing `trace/debug` choices, especially `minimal`, `balanced`, and `detailed`.</use-when>
<focus>Observability tiers, produced evidence, and runtime-overhead tradeoffs.</focus>
<keywords>trace, debug, minimal, balanced, detailed, observability</keywords>
</reference-hint>

## Contents

- [Tier Definitions](#tier-definitions)
- [Hard Facts](#hard-facts)
- [Non-Implications](#non-implications)
- [Safe Wording](#safe-wording)
- [Unsafe Wording](#unsafe-wording)
- [Authority](#authority)
- [Reliable rerouting occupancy](#reliable-rerouting-occupancy)

## Tier Definitions

| Tier | Intent | Post-processing output |
|------|--------|----------------------|
| minimal | 只要任务级统计，最快跑完 | `task_statistics.csv`（仅任务起止，无首包/末ACK）；lifecycle `OFF` |
| balanced | 任务 + 包 + 端口，够用的排查能力 | `task_statistics.csv`（含首包/末ACK）+ `throughput.csv` + lifecycle `SUMMARY` |
| detailed | 全开，含逐包逐跳路径证据 | 同 balanced + `AllPacketTrace_*.csv` + lifecycle `EVENT` |
| custom | 用户自选开关组合 | 可显式选择 lifecycle `DETAIL`，其余取决于所选开关 |

Use `observability_preset(tier)` from `network_attribute_writer` to get the concrete overrides dict.
Do not hardcode switch names in experiment specs or agent logic.

## Hard Facts

- `trace/debug` mode changes observability and logging overhead.
- `detailed` trace can increase wall-clock runtime, trace volume, and post-processing cost.
- `minimal` trace reduces observability compared with `balanced` or `detailed` and defaults
  lifecycle output to `OFF`; lifecycle correctness accounting remains enabled.
- `trace/debug` mode is an attribute overlay on `network_attribute.txt`, not a separate simulation result.
- `UB_QUEUE_TRACE_ENABLE` gates queue trace files such as `QueueTrace_*` from queue events and periodic sampling.
- `QueueTrace` includes processing residency in output-port occupancy; DCQCN/CAQM Queue Backlog includes only VOQ and egress queued bytes.
- `UB_PACKET_TRACE_ENABLE` gates packet timing files such as `PacketTrace_node_*.csv`,
  including CTP first-send and last-ACK rows. It also gates the targeted
  `output/reliable_rerouting.csv` state/ownership/LastAck-hop evidence independently of
  `UB_PORT_TRACE_ENABLE`; full per-port packet logs are not required for recovery acceptance.
- New trace producers use fixed-header CSV files and integer `time_ps` columns so the
  sub-nanosecond precision of the old timestamp rendering is retained without prose parsing.
  The task and throughput parsers still accept legacy `.tr` fixtures for migration and replay.
- `QueueTrace_*` can include ingress queue occupancy rows with the `ingress` marker when `UB_QUEUE_TRACE_ENABLE` is enabled.
- `UB_FLOW_CONTROL_TRACE_ENABLE` gates algorithm-emitted flow-control trace files such as `PfcTrace_*`, `PfcDynamicTrace_*`, and `CbfcTrace_*`.
- `UB_CONGESTION_CONTROL_TRACE_ENABLE` gates algorithm-emitted congestion-control trace files such as `DcqcnMarkTrace_*`, `DcqcnCnpTrace_*`, `DcqcnSenderTrace_*`, `CaqmAckTrace_*`, and `CaqmSenderTrace_*`.
- `UB_LIFECYCLE_TRACE_LEVEL` selects `OFF`, `SUMMARY`, `EVENT`, or `DETAIL`. `SUMMARY` reads the
  always-on lifecycle ledger at teardown and does not maintain a second event-accounting plane.
- Lifecycle `EVENT` and `DETAIL` write fixed numeric records plus `LifecycleTraceSchema.json`.
  `DETAIL` sampling uses a deterministic identity stride/offset and retains complete selected
  resource generations or packet attempts; it never randomly drops individual events.
- Trace records use lazy owner-local chunks. Defaults are 64 KiB per owner/sink, a 1 MiB owner
  high-water mark, and a 512 KiB pressure-flush target. Pressure is handled synchronously by the
  current append; there is no polling timer or asynchronous writer thread.
- `UB_TRACE_MAX_OPEN_FILES` defaults to 1024, but the process uses the smaller of that value and the
  platform soft descriptor limit minus 128 reserved descriptors. A platform-query failure falls
  back to 256. Flushed LRU files are reopened in append mode without changing final filenames.
- `UB_TRACE_CHUNK_BYTES`, `UB_TRACE_OWNER_HIGH_WATER_BYTES`,
  `UB_TRACE_OWNER_LOW_WATER_BYTES`, and `UB_TRACE_MAX_OPEN_FILES` are host-I/O controls. Changing
  them must not change simulation time, protocol state, trace record bytes, or parser inputs.
- These category gates default to `false`, so experiments must opt in explicitly when these files are needed.

## Non-Implications

- Do not claim that `detailed` trace itself lowers simulated throughput.
- Do not claim that `detailed` trace makes a topology less able to approach line rate.
- Do not treat stronger observability as a network-semantic performance regression.
- Do not treat weaker observability as evidence that performance is better.

## Safe Wording

- `detailed trace` is better for diagnosis, but it can make the run and post-processing slower.
- `balanced` is a good default when the user wants some observability without paying the highest logging cost.
- `minimal` is suitable when the user mainly wants a faster run and accepts weaker debugging evidence.

## Unsafe Wording

- `detailed trace` may itself reduce throughput.
- `detailed trace` is not suitable for judging whether the topology approaches line rate.
- turning on more trace means the simulated network performs worse.

## Authority

- `code/doc fact`: trace/debug is modeled as attribute overrides on `network_attribute.txt`
- `maintainer fact`: detailed trace affects runtime cost and observability, not simulated throughput semantics by itself

## Reliable rerouting occupancy

Armed switch-to-switch endpoints maintain four authoritative packet/byte stages: `in_flight`,
`retry`, `failed_egress`, and `recovery_voq`. `UbReliableRerouting::GetOccupancy(stage)` returns current
and peak packets/bytes. Retry means failure-retained attempts; healthy propagation is `in_flight`.
Recovery VOQ bytes are a subset of actual VOQ bytes, not additional occupancy to sum with that total.

`output/reliable_rerouting_occupancy_summary.csv` is emitted by default for armed endpoints before
simulator cleanup, independently of packet/port and master trace gates. Disabled/ineligible endpoints
emit no rows and maintain no new per-packet retention counters. The `failed_port` column identifies
the local origin port, including healthy endpoints whose state is `HEALTHY`.

`ns3::UbReliableRerouting::TraceOccupancyEvents` defaults to `false`. Enabling this attribute writes
`output/reliable_rerouting_occupancy_events.csv` from the same counters, independently of the ordinary
trace category switches. Both outputs use:
`time_ps,node_id,failed_port,stage,packets,bytes,peak_packets,peak_bytes,state,event,source`.
Integer `time_ps` preserves simulator precision. Bytes describe modeled packet storage, not RSS.

The existing `reliable_rerouting.csv` remains gated by packet trace and retains state/LastAck evidence.
Its schema appends `in_flight_packets,in_flight_bytes,failed_egress_packets,failed_egress_bytes,
recovery_voq_packets,recovery_voq_bytes`. `DRAINED` closes the original failed-output handoff; it does
not promise destination delivery or zero recovery VOQ occupancy. Early-stop summaries preserve
remaining work. Completed-case checkers additionally require all four occupancy stages to converge.

Occupancy `source` explicitly labels VOQ handoffs as `retry` or `failed_egress`; other rows use `none`.
