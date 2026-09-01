# Model Device Protocol Delays at Existing Stage Boundaries

## Status

Accepted for the `next` development branch.

## Context

Measured short-message latency contains fixed endpoint work that link serialization, propagation,
and switch forwarding do not explain. OpenUSim needs configurable endpoint protocol delays without
introducing a CPU service queue, a kernel-launch identity, or a second transaction lifecycle.

RTP may split one WQE into several transaction segments and resolve them onto different TPs. The
target receives one completed TA unit at a time. Reconstructing the whole WQE would change this
processing model and require new cross-TP buffering and failure semantics.

## Decision

`UbController` exposes three non-negative `Time` attributes, all defaulting to `0ns`:

- `InitiatorProtocolDispatchDelay`: after synchronous WQE admission and before its first transport
  dispatch.
- `TargetProtocolProcessingDelay`: after one complete inbound TA unit and before target READ/WRITE
  processing and response generation.
- `InitiatorProtocolCompletionVisibilityDelay`: after protocol completion and before the application
  completion trace, TrafficGen completion visibility, and dependency release.

Positive values schedule independent ns-3 events. They represent pipeline latency, not occupation
of a shared CPU or protocol-stack server. Events from different WQEs and from different TA units of
one WQE may overlap.

For RTP, every completed TA unit enters the target-delay gate independently. The implementation
does not aggregate by WQE, task ID, or TP and does not suppress later TA units with the same task
ID. A segmented WQE therefore normally sees its final response move by approximately one target
delay rather than accumulating a serialized `segment count * delay` cost. CTP carries one complete
transaction operation per TP packet, so the same rule gives one target event per CTP request.

The initiator dispatch gate is also per admitted WQE. Multiple WQEs may share one CTP Jetty and
entity context; the existing Jetty still assigns their transaction sequence numbers, while each
WQE's dispatch-ready event is independent. The existing TrafficGen dependency visibility delay,
when configured, remains a separate delay after the application completion gate.

Zero values retain the existing direct call ordering. Delayed events are cancelled by the component
whose stage they postpone. Retransmission, packet processing, congestion control, switch delay, and
shared endpoint compute contention remain outside these attributes.

## Consequences

The model represents fixed endpoint latency while preserving overlap and synchronous admission. It
does not model protocol-engine throughput or queueing under endpoint saturation; that would require
a separate explicit service-resource model.

Large RTP operations do not require cross-TP WQE reconstruction merely to apply this fixed delay.
Every completed TA unit must still execute and generate its normal response, including when several
units share one task ID.

## Acceptance Criteria

- Negative values are rejected and `0ns` preserves existing behavior.
- RTP and CTP first dispatch is delayed only after successful synchronous admission.
- Two RTP TA units with one task ID both execute after the target delay and overlap.
- Completion trace and TrafficGen dependency visibility move together by the completion delay.
- Pending delayed work is cancelled safely during component disposal.
