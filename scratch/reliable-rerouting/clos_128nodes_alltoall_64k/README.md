# 128-host Clos alltoall reliable rerouting

This case retains the full approved one-round workload: 128 hosts, 16 leaves, 8 spines,
256 links, 16,256 directed 64 KiB URMA_WRITE tasks, and 1,065,353,216 payload bytes.
It complements the preserved [small case](../2nodes_reliable-rerouting/README.md).

Every link is 400 Gbps with 20 ns propagation. Tasks begin at 90 us. Leaf 128 port 8–spine 144 port 0
fails permanently at 100 us; detection completes at 105 us. CBFC is enabled, congestion control
and both ordinary/fast TP retransmission are disabled. Each queue reserves 8 MiB;
shared pool and headroom are 0. Default CBFC cell geometry and initial/shared credit windows
are captured in the full runtime snapshot; their combined initial bytes fit within the
8 MiB reserve. Business priority is 7; LastAck uses VL/SL 1.

Routes use CSV full host routes from the existing tools, shortest-only forwarding with
INGRESS_PORT_STRIPE, plus explicit switch destinations and the failure-only detours documented
in ADR0028. `generation.json` records tools version, overrides and generated input hashes.
`structured_summary.json` describes the initial tool-generated topology and host routes;
the final augmented `routing_table.csv` has 22,952 rows, verified by the case checker.
No transport_channel.csv is required: RTP channels are allocated on demand.

From the repository root:

```bash
# Reproduce inputs in a fresh output directory; existing topology output is rejected.
python3 scratch/reliable-rerouting/clos_128nodes_alltoall_64k/generate_case.py --output-dir .scratch/clos-rerouting-reproduction
python3 scratch/reliable-rerouting/validate_routes.py scratch/reliable-rerouting/clos_128nodes_alltoall_64k
python3 scratch/ns-3-ub-tools/ub_tools.py case check scratch/reliable-rerouting/clos_128nodes_alltoall_64k --json

# With a current build; this wrapper runs the exact ns3 --no-build command and records its status.
python3 scratch/reliable-rerouting/run_case.py scratch/reliable-rerouting/clos_128nodes_alltoall_64k
python3 scratch/reliable-rerouting/clos_128nodes_alltoall_64k/check_results.py
python3 scratch/reliable-rerouting/clos_128nodes_alltoall_64k/check_results.py --self-test
```

For checked-in inputs, begin with preflight/run; the generation command is for reproduction
in a fresh output directory. Each case has isolated runlog/output. Preserve previous evidence before rerunning.

The wrapper writes `output/execution.log` and `output/run_result.json` with exit status and
input hashes captured before execution and checked afterward, plus output, script and binary hashes.
The checker imports the small case's causal handshake/ownership checks,
then verifies macro topology/workload, unchanged inputs, all tasks, actual three-hop paths,
zero runtime drop count and disabled TP retransmission. Custom tracing enables task, packet
timing, lifecycle summary and directed recovery evidence; high-volume port/queue traces are off.
Absence of throughput.csv port rows is expected and is not evidence of failed traffic.

See [validation.md](validation.md) for actual acceptance results and limitations, and
[acceptance-summary.json](acceptance-summary.json) for the recorded run provenance and output hashes.
