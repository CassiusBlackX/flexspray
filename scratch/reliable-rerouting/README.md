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

Before running a CSV case, validate the configured post-failure candidate paths:

```bash
python3 scratch/reliable-rerouting/validate_routes.py scratch/reliable-rerouting/2nodes_reliable-rerouting
```

This preparation check currently accepts single-port hosts with explicit destination-port0 route
rows. It checks all candidates under ingress exclusion and the configured bidirectional fault.
Runtime LastAck hop evidence remains necessary.
