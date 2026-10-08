# Match CTP responses by wire identity before completion

Status: Implemented; see [integration evidence](../implementation/ctp-full-tah-06.md).

CTP completion is authorized by decoded wire identity and the exact local
outstanding request. Resolve the supported requester-context type and existing
RCID association, then locate the TASSN within that requester's logical window
and outstanding records. Validate peer/node/Entity identity, VL, applicable
endpoint constraints, response opcode, supported successful status, payload
length and required operation-specific fields before removing outstanding
records, advancing the ACK window or reporting successful completion. A lookup
miss must not create a requester context.

Recover local Jetty, WQE and lifecycle correlation from the matched request
record and use the existing transaction completion path. `UbFlowTag` remains
optional observability metadata for CTP response handling: it cannot select a
request or override a wire mismatch, and its absence cannot invalidate an
otherwise valid response. Operation-specific protocol fields, including the
Atomic_sync task ID echoed on wire, retain their own validation requirements.

Request-dependent response extension layouts follow the matched request's
existing transaction rule after the common response prefix has been decoded.
Preserve the common rule-based opcode/payload validation and completion seams
from ADRs 0024 and 0026; do not infer completion from a tag or advance progress
before validation succeeds.

At the new response-matching boundary, reject bad/unmatched packets and record
the cause without changing transaction progress. Distinguish retired RCIDs and
demonstrably completed segments from never-allocated RCIDs, missing outstanding
matches, malformed layouts, field mismatches and unsupported response statuses.
Do not label every miss as a duplicate when no historical identity is known.

A rejected response does not terminalize a live request; a later valid response
may still complete it once. If none arrives, work remains incomplete under the
existing runner outcome contract. This does not add retransmission, timeout
recovery or a new remote-error protocol.

Keep one explicit successful transaction response per CTP segment, retaining
the existing operation rules, enabled extensions and response-payload policies.
The supported full response profile uses successful-completion RSPST=000,
RSPINFO=0 (no aggregation), SV=0 (INI_TASSN is a transaction sequence), the
supported TA version and no poisoned payload. Validate fields with their
opcode-specific applicability. Reject unsupported response profiles before
completion: do not ignore an aggregate count, apply only one part of a grouped
acknowledgement, or treat RSPST=101 as ordinary success. The latter has distinct
semantics, including an unsupported-operation meaning for
Atomic_store_with_return_status (UB Base Specification 2.1.0, pp. 229-231).

Keep BTAH.No_TAACK disabled; reject requests for unsupported no-response or
SCID/ROT behavior before target execution. This correction does not add TAACK
aggregation, No_TAACK/ROL completion, ROT contexts or new CFG variants. The
presence of a header field or service-mode enum is not a promise to implement
every protocol mode. Minimal header accessors may expose the fields needed
for these checks without moving policy into the stateless shared codec.

Internal contradictions, such as a live RCID association pointing to a missing
Jetty generation or inconsistent ownership indexes, remain explicit simulator
errors. Preserve existing Entity/ingress configuration checks and upper target
execution service-fault behavior. Rejected input reports its cause through the existing CTP diagnostic path. This decision is scoped to the CTP correction and does
not redesign RTP requester identity or its packet-tag handling.
