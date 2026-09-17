# RTP atomic reference case

Status: approved for generation and execution by atomic ticket 04.

Goal: validate the standard CSV-to-WQE-to-network completion path; this is a functional reference,
not a throughput comparison. Success requires all 25 tasks to complete once and the last Read to
start after all 23 atomic tasks complete.

Topology: copy node/topology/routing/transport CSVs from scratch/2nodes_single-tp, unchanged:
two endpoints (0,1), five reference switches, original port/link/bandwidth/delay values.
Transport: RTP, ROI, original precomputed TP records retained (CTP ignores RTP mappings).
Workload: phase 0 = 4096B Write; phase 1 = nine atomic requests plus seven additional Load/Store
suboperation pairs; phase 2 = dependent 8192B Read. Exact rows are recorded in traffic.csv.
N covers 1/2/4/8/16/32/64; explicit P includes 1,16,4096, and blank defaults. All eight Load/Store
suboperations are local only. No remote callback, no data-value arithmetic or address contention.
Parameters: full current runtime catalog snapshot; TransportMode=RTP, detailed trace,
CC/fault/automatic parser disabled. resolution.json records resolved values.

Artifacts: input CSVs, network_attribute.txt, resolution.json, runlog/, output/task_statistics.csv,
output/throughput.csv, canonical.rank0.txt.
Run from repo root: python3.12 ./ns3 run --no-build 'scratch/ub-quick-example --case-path=scratch/2nodes_atomic_rtp --canonical-output=scratch/2nodes_atomic_rtp/canonical --test'
Check and parse serially using the installed tool environment. A failure stays visible and is
fixed within ticket scope. Existing outputs are preserved; no shared output directory is used.

## Execution record

Executed successfully with the command above and `--test`; 25 tasks completed.
`.venv-atomic/bin/python scratch/ns-3-ub-tools/ub_tools.py case check scratch/2nodes_atomic_rtp --json`
reported valid with 25 tasks. The same tool's `trace parse scratch/2nodes_atomic_rtp
--output-dir scratch/2nodes_atomic_rtp/output --json` completed with all task timestamps present.
Actual argv/exit codes/logs were kept under `.scratch/atomic-support/evidence/04/`, which is no
longer kept in the repository.
Canonical phase order and per-task requests were checked by the atomic validation scripts;
RTP response Payload comes from AllPacketTrace, CTP response wire-size counts from PortTrace
in the isolated atomic phase, supported by production-header parsing in the atomic API suite.
Task statistics divide N by elapsed time; this is not an atomic wire-throughput measurement.
