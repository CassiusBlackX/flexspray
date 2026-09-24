# Reliable rerouting cases

- [2nodes_reliable-rerouting](2nodes_reliable-rerouting/): preserved two-host, two-switch parallel-link regression with real three-copy LastAck.
- The planned 128-host Clos acceptance case will be added by ticket 04; it is not yet available.

Run from repository root:

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/reliable-rerouting/2nodes_reliable-rerouting'
python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py
python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py --self-test
```

Each case owns its configuration, `runlog/`, and `output/` directories.
