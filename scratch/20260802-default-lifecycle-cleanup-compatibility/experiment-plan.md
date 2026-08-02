# Default Lifecycle Cleanup Compatibility Matrix

## Status

- Planning mode: `experiment-group`.
- Case materialization and dry-run are allowed; simulation execution requires a separate approval.
- The package contains 18 serial runs across seven scenarios.

## Claim

With both standard-driver lifecycle policies defaulting to `true`, omitting the attributes is
behaviorally equivalent to explicitly setting both attributes to `true`.

## Control And Treatment

- Control: current candidate binary with both lifecycle attributes explicitly set to `true`.
- Treatment: the same binary and fixture bytes with both lifecycle attribute lines absent.
- Changed variable: attribute explicitness only.
- Historical reference: the completed 320-run package under
  `scratch/20260802-main-next-controlled-comparison`; those runs are not repeated.

## Coverage

| Scenario | Modes | Purpose |
| --- | --- | --- |
| S01 | local | deterministic byte baseline and default resolution |
| S03 | local, MTP2 | DWRR, flow control and cross-LP destroy |
| S08 | local | persistent CTP ownership boundary |
| S09 | local, MTP2 | dependency visibility and cleanup ordering |
| S10 | MTP8 | dense AllToAll and cross-LP lifecycle |
| S11 | local | phased release boundaries |
| S12 | local | retransmission and terminal cleanup |

S02 and S04-S07 remain covered by the historical matrix. They are not repeated because this
experiment changes only how the same Boolean values are selected, not TP/VL or routing behavior.

## Fixed Controls

- Reuse the immutable legacy fixture bytes from the historical package.
- Use one source revision and one binary SHA for every row.
- Preserve scenario-specific routing, transport, flow-control, scheduler, retransmission and
  congestion-control settings through the historical materializer's current-next lowering.
- Preserve `AllocationTime=5ns`, `InPortProcessingDelay=0ns`, `400Gbps` links, `100ns` link delay,
  RNG run 1 and `--dependency-visibility-delay=100ns`.
- Use `legacy-csv` routing and execute one simulator process at a time.

## Prediction And Falsification

Every pair should have equal input hashes after lifecycle lines are excluded, equal resolved
lifecycle values, and equal functional outcomes. Any default-resolution, input, completion,
drop, deadlock, teardown or terminal-lifecycle mismatch falsifies the claim and stops execution.

## Evidence And Artifact Contract

- Preflight records source revision, dirty state, binary SHA, parameter catalog and resolved defaults.
- Materialization records resolved configuration, frozen-fixture hashes and a case inspection. The
  historical traffic schema uses simulator-native `sourceNode`/`destNode`; the newer generic case
  checker expects `sourceNodeId`/`destNodeId`, so its traffic-column validator is intentionally not
  used to rewrite or reject these immutable fixtures.
- Execution records exact command, return code, stdout and stderr per row.
- Existing outputs are never overwritten unless materialization is explicitly invoked with
  `--overwrite`; historical package outputs are read-only.

## Checkpoint Policy

- `pause_for_user` on the first preflight, pair-input or execution mismatch.
- Simulation execution remains disabled in `command-manifest.yaml` until explicitly approved.
