# T26 Report: Reuse Validated RTP Transport Resolution

## Outcome

T26 passes its resolution, materialization, lifecycle, retransmission, and bounded current/next
acceptance. A direct or TPG WQE submission now resolves its generation-aware reference once instead
of four times. Repeated access to unchanged TPG membership builds one retained materialization
instead of rebuilding the member vector.

## Implementation

- `SubmitWqe` passes the exact preflight `RtpTransportResolution` into `TryAdmitRtpWqe`. The commit
  rechecks the binding fields and resolved object's exact lifecycle identity without another map or
  controller resolution.
- Each Jetty binding retains an index-aligned TPG transport materialization, following the
  pre-refactor `m_jettyTpGroup` ownership shape.
- The cache key includes exact group lifecycle identity and a monotonic membership version. Bind,
  TP deinit, member add/retire/remove, freeze/resume, destroy, and binding erase invalidate reuse.
- Segment admission still checks the current group cursor, member selectable state, exact TP
  generation, active lifecycle state, and `CanAdmitWqeSegment` in the original order.
- Test counters are disabled by default and enabled only by the focused fixture.

No packet builder/parser, protocol state machine, ACK/RTO policy, queue/scheduler, routing,
congestion-control, packet bytes, or retransmission pinning changed.

## Counter Evidence

| Exercised seam | Before | T26 |
|---|---:|---:|
| Direct WQE transport resolutions | 4 | 1 |
| TPG WQE transport resolutions | 4 | 1 |
| Two unchanged TPG vector requests | 2 builds | 1 build |

The materialization fixture also proves exactly one rebuild after member add, member remove, freeze,
and resume, followed by complete binding/cache erase on group destroy.

## Validation

- Targeted `unified-bus-test`, `test-runner`, and `scratch_ub-quick-example` build: PASS.
- `unified-bus-rtp-resolution --verbose`: PASS, 3/3.
- `unified-bus-lifecycle`: PASS.
- `unified-bus-transport-retrans`: PASS.
- `git diff --check`: PASS.

Review against `main@ccf41b58` confirms that the old retained-vector reuse shape is restored with
only lifecycle generation/version adaptations. Review of the current branch confirms that cursor
selection, stale-member filtering, admission, lifecycle resource/lease creation, TP commit, Jetty
commit, cursor commit, and use-lease release retain their prior order. First send, TPNAK, and RTO
continue using `SegmentRecord::resolvedTpIdentity`.

## Bounded m04 Current/Next Result

The existing direct RTP TPG input ran strictly serially with one initial warm-up and three
current/next pairs.

| Revision | Wall samples | Median wall | Median RSS |
|---|---|---:|---:|
| T26 `ba5d110` | 0.08, 0.07, 0.07 s | 0.07 s | 16,678,912 B |
| next `cf8ee683` | 0.09, 0.07, 0.07 s | 0.07 s | 16,678,912 B |

This bounded case is flat at the available 10 ms wall resolution; T26 does not claim a scenario-level
gain from it. Task statistics, throughput, every packet trace, every port trace, and 21 of 22 shared
artifacts are byte-identical. `TaskTrace_node_1.tr` retains the already approved T05 difference:
same 3,352 bytes, 64 lines, timestamps and full row multiset, with only same-timestamp Sends/Completes
ordering changed. There are no unexplained differences.
