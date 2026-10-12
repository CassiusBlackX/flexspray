# Extend Transaction Rules With Response Length Policy

Status: Accepted, 2026-09-17

The CTP CFG6/compact-transaction-header compatibility boundary below is
superseded by [ADR 0031](0031-use-full-transaction-headers-for-cfg6-ctp.md)
(implemented; see the [integration evidence](../implementation/ctp-full-tah-06.md)).
The remaining decisions retain their scope.

UB 2.1.0 adds memory operations whose header layouts require UDETAH, BEETAH, and, for
Atomic_sync in the applicable CFG/NLP formats, TAIDETAH. Atomic_sync also permits a response
payload selected by target execution. ADR 0024's single fixed response length cannot express that
choice without incorrectly treating a target result as known at request admission.

Extend the existing transaction rules, normalized request result, shared TA codec, and common
target executor/result path. The request records the permitted response length policy and its
default. Target execution optionally selects the actual response payload length; the common
response builder and initiator validate it against the request's policy. The model permits zero or
one operand width for Atomic_sync, defaulting to one operand width. Other operations retain their
existing fixed response contracts. No parallel atomic completion or response-building path is added.

New operation layouts enable the required extension headers in both RTP and CTP; header presence,
order, variable lengths, and packet-size estimates share one plan. Missing required fields may be
defaulted at local submission, but never repaired on inbound decode. CSV gains operation names
without new parameter columns; required header values come from the same submission rules as the API.

This decision supersedes ADR 0024's deferred BE/TAIDETAH scope only for the operations enabled by
this feature, and generalizes its fixed response-length assumption. Existing atomic encoding and
the CTP CFG=6/compact-base-header modeling deviation remain compatibility boundaries. Upgrading all
old wire formats would change existing traffic and is deferred. The common executor ownership and
ADR 0025's single target execution delay remain unchanged.

The accepted model is implemented by the transaction rules, the shared TA codec, and the common
target executor and result path.
