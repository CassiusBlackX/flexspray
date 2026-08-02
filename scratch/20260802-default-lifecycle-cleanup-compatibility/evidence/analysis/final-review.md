# Default Lifecycle Compatibility Results

- Candidate source revision: `44acf477750a6a7d269045858b57202b2b281653`
- Pairs analyzed: `9`
- Status: `{'matched': 9, 'mismatched': 0}`
- FCT source: trace-derived from canonical `START` and `COMPLETE_VISIBLE` events.
- Host runtime source: log-derived summary lines; no performance claim is made from this matrix.

| Pair | Status | Canonical | Tasks/FCT | Lifecycle snapshot | Trace writer snapshot |
| --- | --- | --- | --- | --- | --- |
| S01-explicit-local vs S01-default-local | matched | True | True | True | True |
| S03-explicit-local vs S03-default-local | matched | True | True | True | True |
| S03-explicit-mtp2 vs S03-default-mtp2 | matched | True | True | True | True |
| S08-explicit-local vs S08-default-local | matched | True | True | False | False |
| S09-explicit-local vs S09-default-local | matched | True | True | True | True |
| S09-explicit-mtp2 vs S09-default-mtp2 | matched | True | True | False | False |
| S10-explicit-mtp8 vs S10-default-mtp8 | matched | True | True | False | False |
| S11-explicit-local vs S11-default-local | matched | True | True | False | False |
| S12-explicit-local vs S12-default-local | matched | True | True | False | False |

## Interpretation

All rows are expected to be `matched` only when fixture hashes, normalized attributes, canonical bytes and task/FCT evidence agree. The comparison isolates explicit attribute presence from effective runtime defaults; it does not compare main versus next and does not estimate lifecycle memory savings.

Snapshot differences, where present, are limited to `trace_writer:*` buffering and file lifecycle counters. Non-trace lifecycle/resource metrics are equal; these counters are observability artifacts and are not treated as protocol mismatches.
