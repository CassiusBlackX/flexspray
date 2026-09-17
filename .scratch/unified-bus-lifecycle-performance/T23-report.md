# T23 Report: Fast Minimal Lifecycle Observability

## Outcome

T23 passes its implementation, semantic, byte-contract, parser, documentation, and p02 performance
gates.

- `minimal -> OFF`
- `balanced -> SUMMARY`
- `detailed -> EVENT`
- Explicit `OFF`, `SUMMARY`, `EVENT`, and `DETAIL` remain authoritative.
- Lifecycle correctness accounting remains enabled at every trace level.
- Explicit SUMMARY reuses the T19 bounded writer and now acquires the lifecycle mutex and resolves
  its owner sink once per coordinator summary batch.

## Reuse Review

The implementation directly reuses the pre-refactor `UbUtils` trace append/flush path and the T19
resolved sink. The former single-record sink resolution and header-initialization block was extracted
without changing its statements, and the old `Emit` caller uses the extracted helper. The new batch
path reuses the same helper and `EncodeRecord` function.

No packet builder/parser, protocol state machine, ACK/RTO, queue/scheduler, congestion-control,
callback, timer, or writer-buffer algorithm changed. SUMMARY retains the original resource map order,
22-event order, owner-sequence increments, filename, preamble, header, record encoding, and sink call
order.

## Validation

- Targeted build: PASS.
- `unified-bus-lifecycle-trace`: PASS.
- Lifecycle parser tests: 8 PASS.
- OpenUSim helper tests: 8 PASS.
- OpenUSim documentation tests: 20 PASS.
- Simple RTP WRITE default minimal: no lifecycle files.
- Simple RTP WRITE explicit SUMMARY: 198 parser-valid lifecycle records.
- Default OFF versus explicit SUMMARY: task statistics, throughput, and every non-lifecycle trace are
  byte-identical.

## SUMMARY Byte Contract

The pre-T23 binary was built once from `3d1f363` and compared with the post-T23 working-tree binary
using the same explicit-SUMMARY input.

- Pre-T23 binary SHA-256:
  `39e8ac0181ec5389d98cf97cc5e4de905ec9406ff451ca9683bdaee43b2d8163`
- Post-T23 binary SHA-256:
  `7756e587027029b4d551f70d490659bcea4af0f31cc5fbd47ddc47a46bb45a78`
- Pre-T23 Unified Bus library SHA-256:
  `766a80a9ab980f35f28182c799b9a64f5cb4b96f1cf6818d6a72b1ef68614cfe`
- Final T23 Unified Bus library SHA-256:
  `1919f43b39e18025fa4e655fe6568cdd51d7a7e80339304e0ba6e835204012f3`
- Complete runlog tree: byte-identical.
- `LifecycleTraceSchema.json`: byte-identical.
- `LifecycleTrace_node_0.tr` and `LifecycleTrace_node_1.tr`: byte-identical.
- Exception manifest: not required.

## p02 Performance

The accepted V5 case keeps the p02 URMA_WRITE resource scale but changes the shared global routing
line from legacy `ADAPTIVE` to `HASH`. This is required because current/next reject per-flow UbApp and
TP producers combined with adaptive per-packet selection. The same materialized input is used for
main, current, and next. The two rejected warm-up attempts are retained separately.

Each accepted revision used one frozen binary, one warm-up, and five measured counterbalanced rounds.
All runs were serial.

| Revision | Median wall | Median user | Median sys | Median RSS |
|----------|-------------|-------------|------------|------------|
| GitCode main `ccf41b58` | 0.187 s | 0.14 s | 0.03 s | 58,753,024 B |
| T23 current `17cf695` | 0.198 s | 0.15 s | 0.03 s | 63,913,984 B |
| GitCode next `cf8ee683` | 0.195 s | 0.14 s | 0.03 s | 59,949,056 B |

- Current versus main paired-median wall: `+6.02%`.
- Current versus next paired-median wall: `+1.46%`.
- Current versus main RSS: `+8.78%`.
- Current versus next RSS: `+6.61%`.
- Page faults and block-I/O operations did not regress materially; median block input/output
  operations remained zero.

Default minimal therefore passes the 10 percent wall and RSS investigation thresholds.

Explicit SUMMARY on current produces 68,816 parser-valid records and 5,641,986 bytes. Its median
wall is 0.256 s versus 0.199 s for OFF, a `+28.71%` opt-in observability cost. Median RSS rises by
`+1.87%`. This cost is no longer paid by the default minimal preset.

## Controlled Differences

All three default-mode `task_statistics.csv` and `throughput.csv` outputs are byte-identical. Current
OFF and current SUMMARY also have byte-identical output and non-lifecycle trace trees.

Across main/current/next, only `TaskTrace_node_16.tr` through `TaskTrace_node_23.tr` differ. They
contain the same 28 rows per file with identical timestamps, task IDs, TASSNs, send events, and
complete events. Only same-timestamp send/complete line order differs. Sorting each file produces
byte-identical content across all three revisions. This is the inherited T05 scheduling-order
difference already present before T23; T23 neither changes nor widens it.

## Evidence

- Compact p02 result: `evidence/p02-performance-v5/summary.json`
- Portable frozen revision/input manifest: `T23-p02-manifest.json`
- Full local raw evidence: `evidence/p02-performance-v5/`
- SUMMARY milestone trees, including final commit output: `evidence/summary-byte/`
- Simple OFF/SUMMARY trees: `evidence/simple/`
