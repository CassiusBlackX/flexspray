# Provision CTP Entities Separately from Endpoint Constraints

## Status

Accepted for the `next` development branch.

## Context

CTP has two different kinds of state that must not be conflated. An Entity is a persistent
communication and resource object that can bind one or more Host ports. A traffic operation may
also constrain which source or destination member port it uses. The Entity/VL communication and
congestion state can be shared across operations, but endpoint constraints are operation-local.

The previous implementation stored source and destination port hints in maps keyed by the shared
CTP Entity key. An explicit-port operation could therefore change a later wildcard operation, and
concurrent operations using different explicit ports could overwrite each other. Putting ports into
the Entity key would stop that leak but would incorrectly split Entity/VL state and congestion
control by port.

Traffic rows also exposed Entity IDs alongside endpoint ports. That made an application resource
identity look like a per-task routing input and allowed ordinary workload generation to construct
protocol control-plane state accidentally.

## Specification Basis

The UB Base Specification 2.0 establishes the following boundaries:

- Section 5.3.2.2 states that a Target Primary CNA may receive through all of its ports, a Port CNA
  identifies one receive port, and a response may use the request SCNA to return to the Initiator
  port.
- Section 6.5.2.1 defines CTP load balancing through an Entity that binds multiple ports. The LBF,
  round robin, or port load may select among those member ports.
- Section 6.6.2 defines CTP congestion-control granularity as `{destination Entity, VL}`.
- Section 6.7.2 states that one CTP TP Packet carries one complete transaction operation.
- Chapter 10 reserves the zero value of the 20-bit EID and says it must not be allocated.

## Decision

Each Host controller owns a registry from non-zero compact EID to a sorted, unique set of local
CNA16-encodable UB ports. Different Entities may share ports. Registries are provisioned after
topology and routes exist and are frozen before traffic registration; runtime Entity mutation and
deletion are outside this decision.

The optional control-plane input is `transport_entity.csv`:

```csv
nodeId,entityId,portId
0,1,0
0,1,1
0,2,1
```

When the file exists, all rows are parsed and validated before any registry is installed, and only
declared Entities are created. When it does not exist, the standard runner creates EID 1 on every
`UB_DEVICE` Host and binds every local CNA16-encodable `UbPort`. A Host with an unencodable port
fails instead of silently truncating the default Entity.

`traffic.csv` contains no Entity IDs. It supports the nine-column base schema or the eleven-column
schema ending in `srcPortId,dstPortId`. Each port cell may be empty independently: empty is a
wildcard and explicit `0` is port 0. The former Entity-bearing eleven-column schema and the
thirteen-column Entity-plus-port schema fail explicitly.

The standard application uses `LocalEntityId = 1` and `PeerEntityId = 1`. Advanced callers may
create separate `UbApp`/Jetty configurations with other Entity IDs. A traffic operation never
creates, selects, or mutates an Entity.

Endpoint constraints use the existing `UbWqe::sport/dport` and `UbWqeSegment::sport/dport` fields.
For CTP, `UINT8_MAX` is the internal wildcard sentinel and values `0..14` are explicit CNA16 port
IDs. The constraint is converted once when the WQE is created, copied unchanged into each segment,
and reversed on a response. No endpoint field or presence bit is added to the WQE, segment, CTPH,
or Entity key.

Before sending, both selected Entities and every explicit port membership are validated against
frozen registries. A wildcard source route is restricted to the Local Entity member set while
retaining the existing candidate order and selector. A wildcard destination remains a Primary CNA
on the wire; routing resolves an endpoint belonging to the Peer Entity using the existing LBF.
An explicit destination uses a Port CNA and endpoint route.

On receive, the DEID must identify a configured local Entity and the actual ingress UB port must be
a member. An explicit DCNA must also match that ingress port. This validation occurs before CNP,
TAACK, READ response, request state creation, or congestion-state mutation. A Primary-DCNA
per-flow request records the actual ingress port so the response returns through it; a per-packet
request retains the wildcard so one packet does not pin the whole operation.

The CTP Entity key, congestion key, Jetty key, queue key, header order, ACK/RTO behavior, and
congestion algorithms remain unchanged. In particular, port IDs do not become part of
`{destination Entity, VL}` congestion state.

## Considered Options

- Keep shared port-hint maps: rejected because operation-local input mutates shared Entity state.
- Add endpoint presence fields to WQE, segment, or CTPH: rejected because existing `sport/dport`
  and CNA formats already represent the required distinction.
- Put endpoint ports into the Entity key: rejected because it fragments specification-defined
  Entity/VL state and congestion control.
- Select or create Entities from `traffic.csv`: rejected because workload input is not the Entity
  control plane and cannot safely own shared resource lifetime.
- Create Entities lazily during `SendTraffic()`: rejected because partial runtime provisioning can
  diverge across owners and makes packet behavior depend on task arrival order.

## Consequences

Explicit, wildcard, sequential, and concurrent endpoint constraints are isolated per operation.
One default all-port Entity keeps ordinary cases simple, while the registry and application APIs
support multiple overlapping Entities without exposing them in workload rows.

Cases that used Entity ID 0 or traffic-facing Entity columns must migrate. This is an explicit
compatibility break: invalid old headers fail rather than being guessed as endpoint-port input.
RTP and LDST port semantics are unchanged.

Entity provisioning and endpoint routing now fail early when an EID, member port, destination
Entity, ingress port, or restricted route is invalid. These failures are configuration or protocol
errors; the implementation does not fall back to a port outside the selected Entity.
