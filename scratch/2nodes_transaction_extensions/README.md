# Transaction operation extension demos

These six two-node cases exercise the URMA CSV operation surface through the production runner.
The RTP and CTP directories use the same node, topology, routing, and transport inputs as the
reference two-node cases; only `TransportMode` and `traffic.csv` differ.

| Case | Coverage | Expected tasks | Command |
| --- | --- | ---: | --- |
| `rtp-queue` | `ATOMIC_STORE_WITH_RETURN_STATUS` and `ATOMIC_SYNC`, N=1..64, phased Write/Read | 16 | `python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/rtp-queue'` |
| `ctp-queue` | Same queue matrix over compact CTP | 16 | `python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/ctp-queue'` |
| `rtp-writes` | 0x19/0x1A single and multi-packet rows; three BE writes at 64/128/256/512/1024 B | 19 | `python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/rtp-writes'` |
| `ctp-writes` | Same writes over compact CTP | 19 | `python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/ctp-writes'` |
| `rtp-existing` | Write/Read, seven older URMA operations, nine existing atomics | 18 | `python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/rtp-existing'` |
| `ctp-existing` | Same existing operation coverage over compact CTP | 18 | `python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/ctp-existing'` |

The six inputs collectively contain all 25 accepted URMA CSV names. Each directory has a full
runtime-generated `network_attribute.txt`; the operation-specific details and evidence are recorded
in its `experiment-spec.md`. A task is successful only when its completion trace is present once;
normal process exit alone is insufficient. Logical atomic N and task statistics are not wire
throughput, and this model does not implement memory values, arithmetic, synchronization effects,
credentials, or exception state machines. CTP retains the existing compact base-format limitation.

## Acceptance record

Ticket 06 ran each case serially with `--test` on 2026-09-17. All six reported
`All tasks have complete packet timestamp info` and `TEST : 00000 : PASSED`, with exit code 0:
`rtp-queue` 16/16, `ctp-queue` 16/16, `rtp-writes` 19/19, `ctp-writes` 19/19,
`rtp-existing` 18/18, and `ctp-existing` 18/18. The per-case `experiment-spec.md` links the raw
command log and records the `test/task_statistics.csv` completion count. The default `Atomic_sync`
response is N; the explicit zero-response path is covered by the
`unified-bus-transaction-extensions` API round trip rather than an extra CSV column.
