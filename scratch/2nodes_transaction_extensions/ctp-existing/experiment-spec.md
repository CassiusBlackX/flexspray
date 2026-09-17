# ctp-existing

Status: generated for ticket 06 integration acceptance.

Goal: exercise the CSV -> WQE -> CTP -> target executor -> completion path with the
operation set listed below. A passing run must produce exactly 18 task completions, one for
each task row, with phase dependencies respected. The run is a functional packet/timing check and
does not claim real memory, atomic, queue, synchronization, or management effects.

Topology: two endpoint nodes (0 and 1), five reference switches, and the checked-in two-node
routing/transport CSVs copied from `scratch/2nodes_atomic_ctp`.
Transport: CTP; ROI; priority 7; no fault injection; retransmission remains disabled.
Workload: 18 rows in `traffic.csv`, phase IDs 0, 1; accepted
opTypes are URMA_ATOMIC_COMPARE_SWAP, URMA_ATOMIC_FETCH_ADD, URMA_ATOMIC_FETCH_AND, URMA_ATOMIC_FETCH_OR, URMA_ATOMIC_FETCH_SUB, URMA_ATOMIC_FETCH_XOR, URMA_ATOMIC_LOAD, URMA_ATOMIC_STORE, URMA_ATOMIC_SWAP, URMA_MANAGEMENT, URMA_PREFETCH_TGT, URMA_READ, URMA_SEND, URMA_SEND_WITH_IMMEDIATE, URMA_WRITE, URMA_WRITEBACK, URMA_WRITE_WITH_IMMEDIATE, URMA_WRITE_WITH_NOTIFY.

Expected evidence: exit code 0, `18` rows with completion timestamps in
`output/task_statistics.csv` (or equivalent task trace), and representative request/response
records in `runlog/`. The runtime snapshot is the complete catalog resolved by
`network_attribute_writer.py`, with TransportMode set to CTP and the `balanced` observability
preset applied.

Run from the repository root:

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_transaction_extensions/ctp-existing'
```

The seven older URMA operations retain their existing rule/header behavior. BE rows use a full
bitmap by default and keep logical bytes equal to payload bytes. The CTP compact base format remains
the project-defined compatibility format. Atomic N is a logical operand width; task statistics based
on N must not be interpreted as on-wire throughput.

## Execution record

2026-09-17: the command above with `--test` appended completed with exit code 0.
`test/task_statistics.csv` contains 18/18 tasks with a non-empty `taskCompletesTime(us)`; the runner
reported `Processed 18 tasks`, `All tasks have complete packet timestamp info`, and
`TEST : 00000 : PASSED`. Raw command/output/exit code were kept under
`.scratch/transaction-operation-extensions/evidence/`, which is no longer kept in the repository.
