# Unify Target Transaction Execution Behind One Seam

## Status

Accepted

## Context

The transaction layer currently has separate target-side paths for Read and Write requests and a
`UbRemoteUrmaRequestProcessor` callback that only handles those two operations. RTP and CTP each
contain a wrapper around that callback, while the default path uses separate Read and Write
execution functions and response builders. Atomic already enters part of this path, but its response
construction is attached to the old URMA-specific naming.

This structure makes target execution depend on the programming model and makes adding Message,
Maintenance, or Management operations require another opcode-specific branch. The default Read and
Write behavior is also an abstract successful response: the existing memory map records Write
lengths but is never read to produce Read data.

## Decision

The transaction layer exposes one target execution seam for all supported transaction operations.
The seam receives a complete `UbWqeSegment` request and its static transaction type rule. It delivers completion asynchronously through a callback; it never creates a response segment or packet and it
never knows whether RTP or CTP will carry the response.

The transaction layer owns one response builder. It combines the request, the static rule, and the
execution result to create the response segment, including response opcode, payload length, carrier
length, reversed endpoints, TASSN, task identity, and lifecycle correlation. The transport adapter
supplies a response sink: RTP enqueues the response against a local TPN and CTP sends it through the
appropriate Entity response queue.

The target executor is a registered asynchronous callback, separate from the static rule table. It
receives the complete request, its rule, and a completion callback. A built-in default executor
completes with SUCCESS through a simulation event; registered executors may defer completion further.
`UbTransaction` owns `TargetExecutionDelay`, defaulting to 0ns, which controls the event before the
executor is invoked. Executors do not model memory contents, atomic values, receive-queue
consumption, prefetch effects, or management command effects in this phase.
The executor configuration is set before simulation starts; once the first transaction enters target
execution, later `SetTargetExecutor()` calls are rejected. This configuration freeze does not freeze
or add state to `UbWqe` semantic fields.

Response matching first resolves the outstanding request by TASSN and carrier identity, then validates
the response opcode and payload against that request's rule. It never infers READ solely from a
non-TAACK response opcode and does not maintain an Atomic-only matching path.

Atomic constraints are part of the common transaction rules module rather than a separate
`ub-atomic-semantics.*` module. `UbAtomicOptions` remains a WQE input value object, while callers use
the generic transaction validation and normalized result API; Atomic-specific public rule functions
are removed.

Generic validation returns a `UbTransactionResolved` value containing transaction type, request
opcode, logical length, request/response payload lengths, `requiresResponse`, and normalized fields.
Submission, RTP, CTP, and response construction consume this result rather than recomputing Atomic or
ordinary transaction lengths independently.

Each request rule directly describes its response opcode, response Header layout, and response
payload constraints. Separate unlinked response records are not maintained.

`UbTransactionResolved` is produced once at submission or packet decode, cached on the WQE/segment,
and copied through transport, retransmission, and response paths. `UbAtomicOptions` is submission
input only; normalized values are not recomputed later.

The execution result reserves a strongly typed transaction response status aligned with the Full
ATAH `RSPST` values. The first phase only produces Successful Completion; RNR, Page Fault, Completer
Error, RSPINFO aggregation, retry mapping, Compact ATAH authentication status, and Management status
behavior are intentionally not modeled yet.

The wire opcode namespace is represented by one `UbTransactionOpcode` enum shared by requests and
responses. Static rules carry the request/response role and reject an opcode in the wrong role.
Reserved wire values remain representable as `RESERVED_*` entries, but no rule makes them supported
operations. The enum uses the specification names directly; the previous misleading names are
removed in the same change.

`UbWqe` does not store a second, independent transaction-type field. The static rule resolved from
its opcode is the single source of truth for whether the operation is Memory, Message, Maintenance,
or Management.

Each supported opcode has an immutable declarative rule containing its transaction type, direction,
ordered request/response Header kinds, conditional-header predicates, and payload constraints. RTP
and CTP adapt this layout to their wire carrier instead of maintaining opcode-specific header
assembly branches.

Rule validation returns a structured failure for unsupported or malformed requests. During the first
phase, RTP and CTP log the failure and drop the request without generating a protocol error response;
Completer Error and other non-success response statuses remain future executor behavior.

The old `UbRemoteUrmaRequestProcessor`, `UbRemoteUrmaRequestContext`, no-argument response sender,
URMA-specific dispatch and response methods, separate Read/Write execution methods, and the
write-only `m_memoryStore` model are removed. Tests and internal callers migrate in the same change;
no compatibility adapter is provided.

Target processing delay remains at the existing stage boundary. The delay schedules a complete TA
request before it enters the common target execution seam; RTP and CTP retain ownership of their
transport-specific delayed-event cancellation.

## Follow-up correction

The initial implementation review found that carrier code still emitted a fixed MAETAH/request-header
stack and that CTP encoded every response as Compact ACK. The correction is to make the request and
response rule layouts authoritative for both RTP and CTP encoding and decoding. Optional headers are
consumed only when their presence is indicated by an explicit wire flag; required headers are validated
explicitly. Packet builders own static, code-defined presence switches that default to off and are not
user configuration. Decoders infer TVETAH, MTETAH, and UDETAH from `TV_EN`, `MT_EN`, and `UD_FLG`.
Headers without such a flag, including TAIDETAH and OFSTETAH, are omitted and not guessed from
remaining payload bytes until a future wire-identification rule exists. This is a protocol codec
correction, not a compatibility promise for the fixed legacy stack.

The first phase explicitly excludes `WRITE_WITH_BE` and `WRITEBACK_WITH_BE`. Their numeric values
remain in `UbTransactionOpcode` for wire recognition, but no supported request rule is registered;
submission and reception return `UNSUPPORTED_OPCODE`. BEETAH codec support is deferred to a later
ticket.

Every supported ROI request rule explicitly carries `requiresResponse`. WQE creation, inbound
segment admission, and completion matching consume this rule instead of maintaining separate
Read/Write/Atomic opcode conditionals. Unsupported opcodes never open lifecycle state.

The old `ProcessInboundTaRequest` wrappers are removed. RTP and CTP both call one
`ExecuteInboundTransaction(localTpn, request)` operation for rule lookup, validation, execution, and
response-segment construction, delivering the response through an asynchronous completion/sink rather
than a synchronous return value.

Because TAIDETAH and OFSTETAH have no first-phase wire presence signal, they are removed from the
active Read and Read Response layouts. They are neither emitted nor parsed in this phase; completion
continues to use existing TASSN, task identity, and correlation metadata.

Operation-required extension headers are generated automatically with code defaults and are not user
configuration in this phase. MTETAH context, NTFETAH address/token/data, and MGMTETAH code/status use
zero defaults; IMMETAH immediate/token use zero defaults while message length follows transaction
payload length. Truly optional headers such as TVETAH remain disabled by default.

## Consequences

All transaction types use one target-processing path and one response construction path. Adding a
transaction type requires a static rule, its header layout and codec support, and an executor behavior,
without adding another Read/Write-style callback family.

The change deliberately removes a public callback interface. Existing tests that install the old
callback must be rewritten against the new executor result and completion callback. Existing ROI
Read/Write behavior remains the default execution baseline, while the old fake memory bookkeeping
and URMA-specific API shape are not compatibility contracts.

RTP and CTP continue to own their response queues and network routing. Packet construction remains
outside the executor, so target semantics cannot accidentally depend on a transport mode.

The active extension codecs are MTETAH, TVETAH, IMMETAH, NTFETAH, and MGMTETAH. Existing
TAIDETAH/OFSTETAH primitives are not active layouts, and BEETAH support is excluded. Field value
objects live with transaction data types; ns-3 `Header` classes own wire serialization only.
