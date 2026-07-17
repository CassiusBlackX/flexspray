# T32 Report: OFF packet-transfer evidence fast path

## Outcome

Retain the change. With lifecycle output OFF, packet transfer observations now return before
reading the 29-byte lifecycle tag, resolving the owner, validating active membership, updating
evidence counters, or routing transfer-only parallel control. Packet birth and terminal remain the
correctness ledger.

Coordinator creation also returns before taking the trace-owner mutex when lifecycle output is
OFF. SUMMARY, EVENT, and DETAIL retain the previous implementation unchanged.

## Correctness

- Packet registration, active-attempt and in-flight balance, typed terminal, generation,
  retirement, leases, cleanup, and callbacks remain active under OFF.
- Queue/link state, packet delivery, packet bytes, protocol ordering, ACK/RTO, CTP, and RTP are
  unchanged.
- Targeted Unified Bus build, lifecycle, and VOQ validation pass.
- Explicit SUMMARY managed artifacts are byte-identical before and after T32, including all
  lifecycle trace files.

## Performance

The frozen pre-T32 library is `0968fced...a32878`; the T32 treatment library is
`a7163d0e...38bb7`. One warm-up and five strictly serial counterbalanced p01 OFF pairs produced:

- paired-median wall: `-0.97%`
- paired-median user CPU: `-1.64%`
- paired-median RSS: `-0.08%`
- exact semantic artifact comparisons: `5/5`

Three of five individual wall pairs did not regress, so per-pair noise remains visible. The
predefined retention rule for this direct removal was exact semantics plus a non-regressing paired
median; the change passes.

## Residual Inventory

No ungated record construction or packet/resource selection remains. Startup still performs small
OFF configuration work, and active packet birth/terminal counters remain visible through public
snapshots. Those are not silently removed here because they have different semantics and are not
the transfer hot path.
