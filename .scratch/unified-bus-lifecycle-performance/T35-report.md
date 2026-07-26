# T35: Module-level lifecycle hotspot calibration

## Scope

This investigation compares host runtime overhead in common Unified Bus paths. The control is
`next@cf8ee683`; the treatment is lifecycle `HEAD@51c60c5`. All runs reuse frozen accepted fixtures,
use `rng-run=1`, disable legacy and lifecycle trace output, load the role-specific frozen binary and
dylibs, and execute strictly serially.

The timing matrix separates ordinary RTP, CTP, RTP GBN/RTO, and PFC/DCQCN so an LDST-specific result
is not generalized to the rest of Unified Bus. One warm-up per role precedes counterbalanced pairs.
The reported wall differences are medians of paired deltas.

## Module timing results

| Workload | Total payload | next median wall | current median wall | Paired wall delta |
|---|---:|---:|---:|---:|
| ordinary RTP, 8 flows | about 256 MiB | 1.512 s | 1.547 s | +1.65% |
| ordinary CTP, 4 flows | 512 MiB | 1.561 s | 1.786 s | **+14.73%** |
| RTP GBN/RTO | 512 MiB | 1.612 s | 1.652 s | +2.51% |
| PFC/DCQCN, 2 flows | 512 MiB | 1.254 s | 1.266 s | +0.60% |

The ordinary CTP result is the only common-path regression that is both large and stable in all
three original pairs. RTP, retransmission, and DCQCN do not reproduce an LDST-sized penalty.

## CTP traffic scaling

The CTP fixture keeps four same-Jetty writes and `UbJettyInflightMax=4`. Only `dataSize(Byte)` changes.
This isolates work-volume scaling from topology, routing, Jetty window, and flow-count changes.

| Total payload | Pairs | next median wall | current median wall | Paired wall delta | next median RSS | current median RSS |
|---|---:|---:|---:|---:|---:|---:|
| 512 MiB | 3 | 1.561 s | 1.786 s | +14.73% | 15.5 MB | 25.9 MB |
| 2 GiB | 3 | 6.149 s | 7.074 s | +15.04% | 15.4 MB | 57.7 MB |
| 4 GiB | 2 | 12.270 s | 14.174 s | +15.52% | 15.4 MB | 99.9 MB |

All 14 enlarged-case simulator runs returned zero. Both 4 GiB pair orders reproduced the slowdown:
`+16.50%` for next-then-current and `+14.55%` for current-then-next. The stable percentage and growing
absolute delta show that the CTP penalty accumulates with transaction work rather than coming from
fixed startup cost. The RSS curve is consistent with repeated allocation high-water growth; it is not
by itself proof of a leak.

## Profile attribution

In the 512 MiB CTP profile:

| Inclusive symbol | next | current |
|---|---:|---:|
| `UbCtpTransportService::HandleReceivedPacket` | 259 ms | 444 ms |
| `UbCtpTransportService::CompleteFromTaAck` | 105 ms | 211 ms |
| `UbTransaction::GetOrCreateCtpTransactionState` | absent from top set | 68 ms |
| `UbCtpTransactionContext` constructor | absent from top set | 47 ms |
| ACK-window `vector<uint64_t>::assign` | absent from top set | 48 ms |
| bitmap zeroing `__bzero` | absent from top set | 35 ms |

The active current call path is:

```text
HandleReceivedPacket
  -> TrackInboundTaPacket
  -> GetOrCreateCtpTransactionState
  -> UbCtpTransactionContext
  -> resize/reset ACK bitmap
...
SendResponseSegment
  -> TryRetireTransactionState
  -> TryRetireCtpTransactionState
  -> erase transaction state
```

`GetOrCreateCtpTransactionState` creates a full send/TAACK context whenever an entity state is first
needed. Receiver-only request assembly reaches this function through `TrackInboundTaPacket`, even
though that side does not use the send ACK window. Once the response is sent and inbound units are
empty, `TryRetireCtpTransactionState` records the send cursor and erases the state. A long receive
workload therefore repeatedly allocates, zeros, and frees the ACK bitmap.

## Other measured costs

- `ObservePacketTransfer` with lifecycle `OFF` is about 0.06%-0.11%; its early return is effective.
- Packet birth/terminal bookkeeping is generally about 1%-3%. It is shared and worth later work, but
  it is below the CTP state churn in this matrix.
- Tracked events are workload-specific: ordinary RTP 0.32%, GBN/RTO 1.39%, DCQCN 1.67%, and ordinary
  CTP did not trigger the delayed-ACK tracked-event path.
- The CTP queue-empty callback is about 0.50% and is not the primary regression.
- `UbSwitchAllocator::PruneResolvedEvents` remains visible, but the T29 semantic-preserving prototype
  did not produce repeatable end-to-end improvement and should not precede the CTP fix.

## Conclusion and implementation direction

The first common-path optimization target is CTP receiver-only transaction state, not LDST. The
preferred change is lazy creation of `UbCtpTransactionContext`:

1. Keep inbound assembly in the existing Transaction owner and state map.
2. Create the send/TAACK context only when a sending or ACK-window path requires it.
3. Allow receiver-only state to retire without requiring a context or updating a send cursor.
4. Preserve CTP packet builders/parsers, Jetty admission, TAACK ordering, callback timing, and entity
   ownership.

After implementation, rerun the 512 MiB profiler and the 2 GiB timing block. The change is successful
only if CTP wall/RSS improve without moving RTP, GBN/RTO, or DCQCN behavior and without changing the
existing protocol acceptance artifacts.

## Evidence

- `evidence/t35-module-hotspots-v3/manifest.json`
- `evidence/t35-module-hotspots-v3/timing-summary.json`
- `evidence/t35-module-hotspots-v3/profile-summary.json`
- `evidence/t35-ctp-scale-v1/2g/timing-summary.json`
- `evidence/t35-ctp-scale-v1/4g/timing-summary.json`
