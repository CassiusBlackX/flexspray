# T33 Task Brief: Skip unused lifecycle trace setup when output is off

Status: implementation reviewed; awaiting implementation commit

## Batch Reuse Map

- Pre-refactor commit: `main@ccf41b58`
- Semantic batch: lifecycle trace startup configuration
- Old active symbol: `UbUtils::CreateTraceDir`
- Old call path: prepare the runlog directory and ordinary trace state; no lifecycle trace
  configuration existed
- Continuous code reused: the complete current non-OFF level/detail/rank/resolver/metadata block
- Reuse mode: direct branch around the current implementation
- Required adaptation: OFF calls the existing `UbLifecycleTrace::Configure` with canonical empty
  values so prior global trace state is still reset
- Preserved behavior: trace directory creation, runtime-drop reset, queue-sampler reset, setup
  timestamp, and every non-OFF lifecycle artifact
- Explicit behavior change: invalid or unused detail-sampling values are not read or validated when
  lifecycle output resolves to OFF
- Excluded: lifecycle state mutation, packet paths, public snapshots, parallel control, protocol,
  queue/link, ACK/RTO, CTP, RTP
- Focused validation: targeted build, lifecycle suite, bounded p01 OFF artifact comparison, and
  explicit SUMMARY artifact comparison against T32
- Re-approval triggers: any non-OFF artifact change, ordinary trace change, or excluded-path edit

## Evidence

- Source diff: `UbUtils::CreateTraceDir` resolves the lifecycle level first and uses a canonical
  empty `Configure` call before returning under OFF; the existing non-OFF block remains intact
- Targeted `unified-bus` / `unified-bus-test` build: PASS
- `unified-bus-lifecycle`: PASS; expected child-process fail-fast abort text remains visible
- OFF p01 managed artifacts: exact against T32
- explicit SUMMARY managed artifacts: exact against T32
- `git diff --check`: PASS
- Review against `main@ccf41b58`: trace directory creation, queue sampler reset, and setup timestamp
  retain the old ordering; only post-refactor lifecycle setup is skipped
