# Separate CTP requester progress from shared Entity state

Status: Implemented; see [integration evidence](../implementation/ctp-full-tah-06.md).

Before this correction, the CTP path compared Jetty-local TASSNs against an Entity-keyed
transaction context, preventing independent Jetties from advancing correctly.
Give each RCID/Jetty generation one logical TASSN sequence and transaction
window, spanning its WQEs and destinations. Different RCIDs may have the same
outstanding TASSN; requester identity distinguishes those requests. This builds
on the RCID ownership and lifecycle decision in ADR 0028.

A full transaction window for one requester must not by itself stop selection
of another eligible Jetty. Routing, output resources and Entity/VL congestion
state keep their existing shared transport scope and may still constrain both
requesters. This preserves the shared congestion domains in ADR 0022 while
separating requester progress from transport resource contention.

Reuse `UbJetty` as the single authority for its existing TASSN cursor, ACK
window and WQE completion progress. `UbTransaction` owns RCID association and
outstanding-request correlation, coordinates admission, and delivers validated
responses through the existing segment-success and Jetty completion paths.
CTP owns packet encoding/decoding, routing, shared output queues and sending
scheduling, querying the transaction owner for requester state.

Remove CTP's independently advancing copy of the transaction window. A CTP
requester record may reference Jetty progress, but must not maintain another
send/confirmation cursor and ACK bitmap. Reuse the existing normal completion
and forced-cleanup paths so their progress cannot diverge between two copies.

Preserve the current contiguous completion frontier for WQE success notification
within one Jetty, including across destinations. An out-of-order valid response
marks its own segment complete, but the WQE's success callback waits until the
frontier has passed its final segment. If later WQE B responds before earlier
WQE A, B's dependent work may wait for A's missing responses. Keep that
same-Jetty completion head-of-line behavior in this correction; do not add
early per-WQE notification or per-destination sequence windows. Existing
transaction ordering rules still govern admission separately. This is a
simulator completion policy, not a claim that UB requires ordered notification
for every unordered WQE; different RCIDs retain independent progress.

Retain `UbJettyInflightMax` as the per-requester admission bound. CTP
preparation requires
`1 <= UbJettyInflightMax <= min(actual ACK-window capacity, 32768)` and rejects
invalid configuration explicitly, without silently reducing the configured
value. Validate before committing a new RCID association or CTP binding.
The 32768 bound is a conservative simulator constraint using half of the 16-bit
wire-sequence space, not a new UB specification requirement. The window spans
from the earliest uncompleted sequence to the next send sequence, including
holes retained for out-of-order completion; it does not bound cumulative work.

Allow CTP in-flight-limit, ACK-window-capacity and transaction-segment-size
updates only while the requester is fully idle: no accepted/queued WQEs,
unfinished reservations, outstanding requests or requester-owned dispatch and
completion events, and equal send/completion cursors. Validate the proposed
configuration before applying it; reject busy or invalid changes explicitly
without changing configuration or progress. Resize the actual ACK bitmap at its
current base only when idle, keeping logical counters and RCID unchanged. A
same-value write is a no-op and cannot clear confirmation bits. Do not defer
changes silently or recalculate existing WQE reservations.

Apply this CTP guard to attribute writes, public setters and reset helpers.
Initial configuration and bitmap construction remain possible before use;
validate before CTP preparation/admission so direct UB callers cannot bypass
the contract. Global defaults for future objects and unrelated RTP runtime
configuration are outside this change. Exact setter/result factoring remains
an implementation detail.

Support normal 16-bit wire TASSN wrap with the same RCID, matching responses
against the requester's live logical window and outstanding requests. A delayed
original response retains its uncompleted position, preventing the bounded
window from advancing a full wire-sequence cycle beyond that request.

Forcible abandonment of transmitted requests is restricted to explicit Jetty
closure or whole-run-stop cleanup. Close new admission before cleanup; future
traffic needs a new Jetty generation and RCID. Do not clear an abandoned
request's progress and then continue submitting new work to the same requester.
Reuse the existing lifecycle authority and cleanup modes; this does not add
automatic idle destruction.

Keep the existing Jetty manager as the final destruction authority. Graceful
destruction closes new admission, allows accepted work to finish, and retires
the association after its WQEs, outstanding requests and requester-owned events
resolve. Use owner-local drain conditions; do not wait for the whole network,
shared Entity/VL resources or unrelated requesters to become empty.

Bind deferred dispatch and completion work to the exact Jetty generation,
RCID and WQE/segment, preserving the modeled delays. Match and validate a
response before retaining delayed completion work, and revalidate the captured
identity when the callback runs. Never reconstruct a requester from an Entity
key or a caller Jetty number that may identify a successor generation.

Abort/force closes admission first, terminalizes accepted work once through
existing failed/flushed outcomes, cancels owned pending callbacks, and removes
its unsent requests still owned by CTP source queues. Identify queue entries by
exact local ownership, not numeric RCID alone: do not remove other requesters'
packets or target responses. Use bounded owner-local metadata and cleanup;
do not add a global packet ledger. Release backing only after local cleanup
obligations resolve and already-running callbacks unwind safely.

Requests handed to lower layers or remote targets may still be transmitted or
executed after abort. Local destruction does not promise remote cancellation
or rollback. Late responses retain their old RCID and follow ADR 0030's retired
requester rejection; a recreated Jetty has a new RCID. Cancelled or completed
callbacks cannot act on a successor, while contradictory live owner indexes
remain internal errors.

The accepted normal-run model assumes lower layers do not deliver duplicates.
It does not guarantee detection of an arbitrarily delayed duplicate whose
RCID, wire TASSN and response fields coincide with a new request after a full
wire-sequence cycle. Window validation alone cannot distinguish those packets.

Retain the existing 32-bit local sequence representation. At CTP WQE
submission, check the entire request's segment demand, including already
accepted/queued reservations, using overflow-safe arithmetic. Reject a WQE
before admission if its full demand would overflow reservation, send or
completion cursors. Already accepted work remains drainable. Do not reset the
counter or replace the RCID within the same Jetty generation; the caller may
destroy/recreate the Jetty for future work. This avoids accepting a request
that cannot finish and keeps a shared 64-bit sequence/lifecycle migration
outside this correction.

Within each existing CTP scheduling group, reuse round-robin selection with
one successfully admitted transaction segment per selected Jetty before moving
to the next. Skip requesters blocked by their own window, dispatch gate or
transaction dependencies without consuming their sequence or WQE progress.
A blocked candidate must not occupy a shared pending head that prevents other
eligible requesters from being selected.

Stop after a complete pass makes no progress. Resume on new ready work,
dispatch-delay completion, window advancement or dependency release, using
the existing event and owner boundaries rather than periodic polling.
Connect authoritative Jetty progress to CTP scheduling explicitly: the current
generic transaction retry loop skips CTP operations. Preserve graceful-drain
progress for accepted work and prevent abort/force cleanup from reopening
admission; handle reentrant callbacks and binding removal safely.

Do not add an Entity/VL or node-wide aggregate transaction admission quota in
this correction. Retain per-Jetty window bounds and the existing shared output
scheduling, packet-class priority and configured lower-layer flow control.
The old Entity-keyed transaction window also limited aggregate in-flight work;
independent requester windows can admit up to their sum. Queued requests,
outstanding records, memory use and completion timing may therefore increase
with active Jetty count. Include queue and progress observations in the planned
multi-Jetty validation; do not assume unchanged aggregate behavior.

Current CTP source queues have no independent enqueue capacity limit, and
dequeue flow control is not a source-queue memory bound. Any future aggregate
quota needs a separate resource-model decision covering units, ownership,
fairness and release/wakeup rules. Preserving shared CNP state does not add a
CNP-driven rate limiter that the current implementation does not provide.

The implementation reuses Jetty progress, Transaction records and the CTP service scheduler. Response
matching and rejection behavior are recorded in ADR 0030. This decision does
not select new window or congestion algorithms or a broader RTP context migration.

Protocol basis: UB Base Specification 2.1.0, section 8.2.3 (p. 279), describes
distinct requester contexts for SQs and no mutual ordering requirement between
different SQs. The mapping to one requester context per simulator Jetty
generation is the simulator design choice recorded here and in ADR 0028.

The maintained Clos CTP case originally configured inflight10000 with actual ACK capacity2048.
On2026-10-07 the user explicitly approved reducing its UbJettyInflightMax to2048 while retaining
ACK capacity2048 and all other topology/workload/parameters. The initial rejected run is retained;
acceptance uses fresh copies with this documented exception to the original configuration freeze.
