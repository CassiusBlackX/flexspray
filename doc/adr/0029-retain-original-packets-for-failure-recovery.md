# Retain Original Packets for Failure Recovery

Status: Accepted and implemented. All four tickets completed on 2026-10-01: original packet
retention, recovery VOQ/ingress isolation, backlog ordering, and occupancy reporting.
See [validation](../../src/unified-bus/doc/reliable-rerouting-buffer-accounting-validation.md)
for commits, executed checks, integrated evidence, and the unrelated missing pcap fixture.

The packet-level rerouting model tracks original packet references while they are in
flight and retains those packets for recovery when their link fails. It avoids creating a separate
`Packet::Copy()` object for every normal transmission. Tracking must cover every unconfirmed packet
in a link direction, including packets still propagating after the port clears its current transmit
packet; the current transmit pointer alone is insufficient.

Use the existing `ns3::UbLink::EnableReliableRerouting` switch, whose default is false. The new
in-flight bookkeeping is active only on endpoints actually armed for rerouting under the existing
switch-to-switch eligibility rules. Disabled or ineligible links keep their ordinary delivery path
without the new per-packet tracking, retention, or recovery occupancy updates. This is a functional
gate, independent from whether traces are recorded.

The retry accumulation window starts at physical link failure and ends when the local endpoint
detects that failure. At physical failure, unconfirmed in-flight packets become retained recovery
work; subsequent failed transmission attempts add retained packets until local detection. Ordinary
CBFC eligibility still applies during this interval: exhausting downstream credit pauses service,
and failure handling must not bypass credit checks or fabricate replacement credit.

Local detection stops new transmission attempts on the failed output and closes the retained
attempt set. Packets already represented in that set must not be added again by delayed delivery
callbacks. While waiting for the first valid LastAck, the retry set is retained but does not grow
from further sends on the failed output. LastAck authorizes recovery; it does not determine when
failure-time accumulation stops. Unsent VOQ and egress backlog remains a separate category from
attempts already transmitted on the failed link.

Recovery coverage explicitly includes packets still queued in VOQs for the failed output. These
packets must migrate to healthy-output VOQs, preserving original ingress identity, VL and their
existing ingress reservation; they have not yet been transmitted on the failed link. Releasing
that original reservation after successful service returns upstream credit exactly once.
The first valid LastAck gates migration of all three retained categories: retry attempts, unsent
failed-egress packets and old VOQ backlog. Detection alone does not release old VOQ packets toward
a healthy output. This timing was explicitly confirmed during the design discussion.

As soon as the local endpoint detects the failure, new route selections exclude that output.
Newly arriving traffic follows the existing healthy-route and failure-detour policy of ADR 0028,
with ordinary ingress admission and credit accounting; it need not wait for LastAck. Detection
also invalidates pending grants toward the failed output. Packets whose input-processing callback
still names a previously selected failed output must be retained or redirected through the
appropriate existing-reservation path rather than escaping the detection cutoff.

Independent recovery accounting covers both retry attempts and unsent failed-egress packets whose
original ingress reservation has already been released. After valid LastAck, these packets re-enter
the existing VOQ for their healthy output, original VL and original ingress identity. A local
recovery marker distinguishes their storage accounting from ordinary packets in the same queue.
Neither category reacquires ingress capacity or returns the original upstream credit a second time.
Retained retry occupancy and unsent egress recovery occupancy remain separately identifiable. Packets still holding their
original ingress reservation, including retained VOQ packets, keep that reservation and its ordinary
single credit-return obligation; rerouting alone does not exempt them from this accounting.
These rules concern the original local ingress. Forwarding over healthy links remains subject to
normal downstream flow control and the corresponding per-hop credit lifecycle.

The independent recovery stores have no modeled hard capacity limit in this change. They retain
accepted recovery work and report current and peak packet/byte occupancy, with retry attempts and
unsent egress recovery separately identifiable. This preserves the existing unbounded retry-storage
abstraction while removing ingress-capacity conflicts; it does not model finite retry-memory
pressure or a finite-store admission/backpressure policy. Healthy egress capacity and downstream
credit constraints continue to apply when recovery work is forwarded.

Recovery work shares the existing VOQs and allocator rather than registering a separate recovery
source. It preserves its original VL and participates through the ordinary inter-VL policy and
same-VL round-robin selection of VOQs. Recovery has no strict priority override. A full healthy
egress or insufficient downstream credit leaves the packet queued for later service rather than
dropping it or charging its original ingress. Integration must preserve the wakeup/grant and
commit/rollback rules of ADR 0021.

Physical VOQ packet counts and queued-byte totals include both ordinary and recovery-marked packets.
Admission accounting is separate: enqueue/dequeue of a recovery-marked packet must neither add to
nor subtract from the original ingress reservation. The local marker must be captured consistently
by queue accounting and credit-return processing, and consumed before the packet leaves this switch
so downstream hops perform ordinary admission and credit return. A marker that only suppresses
credit return, without separating ingress accounting on both enqueue and dequeue, is insufficient.
Retained packets targeting the same healthy VOQ are appended in category order: retry attempts,
unsent failed-egress backlog, then old VOQ backlog. Preserve each category's internal order and do
not prepend these packets ahead of traffic already in the destination VOQ. This is a migration
order, not a recovery-priority scheduler or an end-to-end ordering guarantee.

Successful insertion into the healthy VOQ transfers the packet reference out of the old retry or
failed-egress recovery store. In the same handoff, decrease that store's occupancy and increase
VOQ recovery occupancy, retaining the recovery origin needed for attribution. The packet object
remains alive under VOQ ownership. Successful VOQ-to-healthy-egress service decreases VOQ recovery
occupancy and increases ordinary egress occupancy; blocked service changes neither ownership nor
these counts. VOQ recovery occupancy is a subset of actual VOQ occupancy and must not be added to
that total again. Final packet destruction occurs only when all remaining references are released.

Provide current-occupancy query interfaces and a compact end-of-run CSV summary, grouped by node
and failed port with stage-specific current and peak packet/byte counts. Time-series event detail
is opt-in rather than enabled by default. Rerouting-disabled links do not maintain the new records
or statistics. The summary and event outputs must use the same authoritative occupancy counters.

This trades an additional packet object and its copy bookkeeping for lightweight in-flight
reference bookkeeping. Failure recovery must invalidate the old delivery action before reusing a
packet, and transfer responsibility without dropping the last reference. Removing a retry entry
releases that store's reference; it does not destroy a packet that another queue or event still
holds. Retry occupancy describes retained packet bytes, not host memory allocated by the simulator.

This decision revises the normal-transmission backup representation described in ADR 0027 while
preserving its packet-level, idealized-acknowledgement scope and detection-time output shutdown.
All retained-backlog categories preserve ADR 0027's LastAck migration gate, whereas new route
selections avoid the failed output immediately at detection. These rules supplement ADR 0027's
retention representation and preserve ADR 0028's legal failure-detour policy.
