# Reliable rerouting cases

- [2nodes_reliable-rerouting](2nodes_reliable-rerouting/): preserved two-host, two-switch parallel-link regression with real three-copy LastAck.
- [clos_128nodes_alltoall_64k](clos_128nodes_alltoall_64k/): 128 hosts, 16 leaves, 8 spines, real three-hop LastAck and 16,256 directed 64 KiB transfers.

Run from repository root:

```bash
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/reliable-rerouting/2nodes_reliable-rerouting'
python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py
python3 scratch/reliable-rerouting/2nodes_reliable-rerouting/check_results.py --self-test
python3 scratch/reliable-rerouting/run_case.py scratch/reliable-rerouting/clos_128nodes_alltoall_64k
python3 scratch/reliable-rerouting/clos_128nodes_alltoall_64k/check_results.py --self-test
```

Each case owns its configuration, `runlog/`, and `output/` directories.

Before running a CSV case, validate the configured post-failure candidate paths:

```bash
python3 scratch/reliable-rerouting/validate_routes.py scratch/reliable-rerouting/2nodes_reliable-rerouting
```

This preparation check currently accepts single-port hosts with explicit destination-port0 route
rows. It checks all candidates under ingress exclusion and the configured bidirectional fault.
Runtime LastAck hop evidence remains necessary.
