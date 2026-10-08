# Use full transaction headers for CFG6 CTP

Status: Implemented; see [integration evidence](../implementation/ctp-full-tah-06.md).

Before this correction, the CFG6 CTP path emitted compact transaction headers, although UB Base
Specification 2.1.0 section 7.2.1 (p. 222) restricts compact BTAH to CFG9 or
CFG7/NLP4. Migrate CFG6 CTP send and receive together: requests use full BTAH,
all supported responses use full ATAH, and applicable MAETAH uses its full form.
Reuse existing full header classes and `UbTaHeaderPlan` with the current
operation rules. The outer CFG6 UPI/EID layout remains valid; a class name
containing `Compact` does not by itself require replacement.

Describe each CTP memory-access segment exactly: Read/Write MAETAH.Length is
that segment's logical byte length, including a short final segment, and its
address is the WQE base address plus the segment byte offset. Preserve TokenID,
check address/length arithmetic, and keep normalized segment fields consistent
with the serialized header. Do not copy whole-WQE lengths or retain divergent
address sources. Full MAETAH defines the individual slice's memory access
(UB Base Specification 2.1.0, section 7.2.3, pp. 232-233).

For Atomic, encode operand width N in MAETAH.Length, separately from request
payload length P (section 7.4.2.3.2, p. 263). Decode CTP memory length from
MAETAH and actual payload size from the packet after declared padding removal;
do not derive memory length from FlowTag or compact length rounding. Retain
existing operation validation, Atomic payload/sub-operation modeling and
response-length policies, including queue operations' P=N constraint. This
does not impose a general two-operand Atomic wire layout or remove every
request-side instrumentation dependency. RTP's existing valid packet behavior
remains outside this CTP length/address correction.

Support one CFG6 transaction layout after migration, without a legacy-compact
mode, automatic byte-pattern detection or fallback after full-format decode
fails. The old carrier has no separate format-version marker, so universal
recognition of legacy packets is not promised. Mixed old/new senders and replay
of old compact-format packets are outside this compatibility contract.

Update affected trace parsing and packet-size expectations. Serialized sizes
and transmission timing can change; old byte and timing baselines are not
compatibility requirements. This narrowly replaces ADR 0020's historical
raw-byte-equality requirement for affected CTP output; its lifecycle/ownership
contract remains unchanged, as does the requirement to preserve unaffected
RTP behavior.

Use the specified CTP alignment padding: append 0-3 zero bytes to align the
NTH-through-payload span to four bytes and declare the count in CTPH.Padding
(UB Base Specification 2.1.0, section 6.2.1, p. 176). Validate and remove it at
the CTP receive boundary before transaction payload checks. Padding contributes
to serialized packet size and link occupancy, not payload progress or segment
reservations. Keep it outside the shared TA codec; do not extend this decision
to RTP padding. Reject malformed/truncated padding and unexpected TAACK payload.

Remove `ExperimentTaAckPacketBytes` and its arbitrary-padding implementation
outright, including the TypeId attribute, member and test setter. The user
explicitly rejected the earlier proposal to retain zero-value compatibility.
Do not retain an alias, accepted no-op setting or tag-based exception to payload
validation. Old configurations containing the key, including value zero, are
unsupported. The old hook appends undeclared bytes and currently affects
ordinary requests as well as TAACK.

Remove the key from maintained case snapshots and fixtures during implementation
and update affected documentation and runtime-catalog expectations. Preserve
historical evidence and original workload/topology semantics; record the
configuration migration when rerunning a historical fixture. Original
ub-quick-example tests and reference cases remain mandatory acceptance checks
after this parameter removal, with full-header/padding sizes reflected in
timing expectations.

Extend the existing `ub-ta-codec` with stateless full transaction-header
encoding, decoding and byte-size helpers used by both CTP and RTP. The existing
header classes and `UbTaHeaderPlan` govern serialization and sizing. Carriers
retain payload construction, outer headers, routing and transport behavior;
`UbTransaction` retains requester identity, outstanding matching and completion.
The codec consumes explicit wire fields and rule/layout information without
looking up nodes, Jetties, WQEs or simulator tags, allocating IDs or advancing
protocol state. Preserve valid RTP wire bytes and behavior when adapting its
TA code to these helpers; its identity and tag policy are outside this change.

For request-dependent response layouts, decode the basic header first, let the
caller resolve the request rule, then decode the planned extensions. CTP uses
the exact outstanding match under ADR 0030. Preserve response fields, including
status, for owner validation; do not reduce ATAH to a request-header-shaped
value that loses them. Check structural bounds, including variable extension
lengths, and return decode failures to the caller's rejection policy. Operation
rules retain normalization and semantic constraints. Exact helper and type
names remain implementation details; no combined transport framework is added.

This supersedes only ADR 0026's preservation of the CTP CFG6/compact-header
deviation. Its operation rules, response-length policy and common target
executor remain in force. This decision does not add operations or CFG variants,
or authorize a general atomic wire-format or RTP identity redesign.
