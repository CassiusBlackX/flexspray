# Replace Target Protocol Processing Delay With One Target Execution Delay

## Status

Accepted

## Context

ADR 0023 modeled endpoint protocol delays at existing stage boundaries and defined three
`UbController` attributes: `InitiatorProtocolDispatchDelay`, `TargetProtocolProcessingDelay`,
and `InitiatorProtocolCompletionVisibilityDelay`. `TargetProtocolProcessingDelay` represented the
wait between receiving one complete inbound TA unit and target processing.

The transaction refactor then moved target-side execution behind one seam: `UbTransaction` holds
`TargetExecutionDelay`, and RTP and CTP both deliver complete inbound requests to the common
`ExecuteInboundTransaction` operation before invoking a registered target executor. With that seam
in place, the two attributes describe the same point in the target path, and keeping both produced
carrier-specific behavior: RTP summed the protocol-processing delay and the target-execution delay
before executing, while CTP applied only the protocol-processing delay and called the common entry
directly, so the two carriers could not express the same target timing.

## Decision

Remove `TargetProtocolProcessingDelay` from `UbController`, including its attribute, member,
getter, tests, and callers. The target path keeps one configurable delay,
`UbTransaction::TargetExecutionDelay`, applied exactly once at the common
`ExecuteInboundTransaction` entry for both RTP and CTP. Its default remains `0ns`, and executor
registration stays frozen after the first inbound transaction.

`TargetExecutionDelay` is measured before the target executor is invoked. An executor that needs
more time still controls its own completion timing through the completion callback.

The other two delays defined by ADR 0023 remain in force: `InitiatorProtocolDispatchDelay` and
`InitiatorProtocolCompletionVisibilityDelay` keep their existing stage boundaries and semantics.
The text of ADR 0023 is not modified; this ADR supersedes only its `TargetProtocolProcessingDelay`
decision.

## Consequences

- RTP and CTP express target processing delay through one attribute and one code path.
- The double delay that RTP could previously apply disappears. Scenarios or artifacts that
  configure `TargetProtocolProcessingDelay` must migrate to `TargetExecutionDelay`.
- ADR 0023's pipeline-latency interpretation is unchanged: the delay represents fixed endpoint
  latency, not occupation of a shared protocol engine, and events from different requests may
  overlap.

## Acceptance Criteria

- No production, test, or case reference to `TargetProtocolProcessingDelay` remains.
- `TargetExecutionDelay` is applied once per complete inbound request at the common entry for both
  carriers; equal configured delays produce the same executor invocation time in RTP and CTP.
- Negative delays are rejected and executor configuration freezes after the first inbound request.
- Pending delayed events are cancelled during component disposal.
