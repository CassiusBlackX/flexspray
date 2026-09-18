# CTP Endpoint-Port Fixture

This small two-host case uses the standard default CTP Entity (EID 1) on each host.
The Entity binds both local UB ports automatically; `traffic.csv` exposes only endpoint
constraints.

The first two tasks exercise explicit port 0 followed by wildcard traffic. The phase-2
tasks start together and combine explicit port 0, explicit port 1, source wildcard and
destination wildcard, covering the per-operation isolation regression.

Run it with:

```text
python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/ctp-endpoint-ports'
```

The run is accepted only when the runner prints `Simulation finished`.
