# CFG7 LDST engineering regression

## Planning Mode
single-case; approved engineering fixture for the CFG7 LDST bypass path.
User authorized generation, execution and repair of this bounded fixture.

## Goal
All four bidirectional MEM_STORE/MEM_LOAD tasks complete exactly once via CFG7 TP bypass
through a real middle Switch with CBFC. This CSV covers Api/Instance/Thread; direct Endpoint
and independent configuration are covered by unified-bus-ldst-cfg module tests.

## Topology
Hosts 0 and 1 have one port each; Switch 2 has two ports. Host0.0 ↔ Switch2.0 and
Host1.0 ↔ Switch2.1, both 400Gbps/20ns. Every node: allocationDelay=1ns, forwardDelay=0ns.

## Topology Realization
custom-graph, standard CSV via a local NetworkSimulationGraph builder. Four manual route
rows to Host0/Host1 cover both node CNA and precise destination port 0 in the production loader.

## Workload
custom-traffic-skeleton: task IDs 0..3, 4096 B each, phase 0, no dependencies, delay 10ns,
priority 7. Tasks: 0:0→1 STORE wildcard; 1:1→0 LOAD wildcard;
2:1→0 STORE explicit src/dst port 0; 3:0→1 LOAD explicit src/dst port 0.

## Routing Intent
PER_FLOW_SHORTEST_PATHS; runtime default HASH64; manual-route-table, no primary fallback
for explicitly addressed destination ports.

## Network Overrides
UbLdstApi::HeaderConfig=7; UbLdstEndpoint::HeaderConfig=7; UbSwitch::FlowControl=CBFC.
One LDST thread; StoreOutstanding=LoadOutstanding=4; StoreRequestSize=512,
LoadResponseSize=512, LoadRequestSize=64. Remaining values from a fresh complete runtime
catalog. CNA24/NLP4 is a simulator-local extension; compact headers/payload semantics retained.

## Transport Channel Mode
on-demand artifact contract; LDST is TP bypass and requires no transport_channel.csv.

## Observability
balanced preset: task, packet and port evidence. Runner stdout/stderr and exit status
retained separately. No packet-drop or failure evidence permitted. No throughput claim.

## Startup Readiness
README/QUICK_START read; launcher, smoke query case and critical tools present.
Tools submodule 0fec3aed initialized; dependencies installed in .venv-ldst-tools.
Release test-runner and ub-quick-example built with logs/tests enabled; final full build passed.

## Execution Record
Completed: runner exit 0, strict verifier PASS, tasks 0..3 each complete once; middle
Switch ports each Rx=Tx=32. Full evidence was collected under `.scratch/ldst-cfg7/evidence/`
and is no longer kept in the repository.
Before every run, move existing runlog/output/runner evidence into a unique history
directory; current validation reads only current files. Generator does not run a
simulation and writes only this case's inputs plus the documented project catalog cache.

## Validation Notes
Require task set {0,1,2,3}, one start and one completion per task with valid timestamps;
Switch 2 ports 0 and 1 must both show Rx/Tx; matching parser statistics and successful
runner exit/summary with no failure/drop evidence. Negative copies must fail validation.
Raw CFG/NLP assertions are supplied by production Header-based module tests, not inferred
from traces that lack those columns. Final byte milestone uses the approved stable CFG9
baseline; original uninitialized CCI evidence remains preserved in the feature ledger.
