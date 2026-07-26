# GitCode next vs lifecycle: complex CTP/RTP differential

## Claim

The lifecycle implementation preserves `next@cf8ee683` CTP and RTP protocol behavior under
window pressure, concurrent work, retransmission, and TPG member selection.

## Simulator boundary

This is a deterministic simulator differential. It compares two already-built local binaries; it
does not claim physical-device behavior or measure network hardware.

## Revisions and controls

- control: `next@cf8ee683e00e5b6210661013e93cec7217d662e5`
- treatment: `codex/unified-bus-lifecycle-implementation@824b87a0503133a6ca4f43f8bd8ce9bbe2631341`
- fixed controls: identical case bytes, release build, local-single runtime, `rng-run=1`, topology,
  traffic, routing, flow control, trace settings, and parser semantics
- changed variable within every pair: source revision only

## Blocks

### CTP Jetty window restoration

Four simultaneous 1 MiB WRITEs share one bound Entity/VL Jetty. The three cases set
`UbJettyInflightMax` to 1, 4, and 128. Prediction: both revisions produce the same task completion
order and shared trace bytes at every window; increasing the window must visibly change completion
timing or throughput, otherwise the test did not activate the intended backpressure seam.

Falsification: one revision stalls, completes a different task set/order, produces an unexplained
shared artifact difference, or fails to reproduce the same cross-window response.

### RTP shared-TP window wake

Four bidirectional READ/WRITE tasks reuse the existing shared-TP fixture with transport inflight
limited to one packet. Prediction: every task completes on both revisions and ACK capacity wakes do
not lose, duplicate, or starve work.

### RTP GBN double loss

A 256 KiB WRITE uses GBN with an 8-packet window and deterministic one-time drops at PSN 1 and 3.
Prediction: both revisions complete with the same logical PSN coverage. Physical retry counts may
differ only if the cumulative-ACK descriptor cleanup difference is precisely evidenced.

### RTP TPG pinning under loss

Two concurrent 1 MiB WRITEs bind to the same three-member TPG. Task 0 loses PSN 1 on member 0 while
task 1 remains runnable. Prediction: task 0 retransmission remains on its selected member; member
selection, completion, ACK/RTO ordering, and shared traces match `next` unless a narrow, explained
lifecycle correction is demonstrated.

## Evidence plan

- case-derived: SHA-256 manifests for all paired inputs
- measured/trace-derived: `task_statistics.csv`, `throughput.csv`, TaskTrace, PacketTrace,
  AllPacketTrace ACK/PKT, PortTrace, and QueueTrace when emitted
- treatment-only: lifecycle schema/traces validated by the production lifecycle parser
- comparison classes: byte-identical, controlled explained difference, regression, or inconclusive

## Artifact contract

Each case directory contains an `experiment-spec.md`, immutable `case-input/`, captured stdout,
stderr, return code, command, timing, and generated simulator artifacts. Pair summaries and the
Chinese report live under this package. Existing experiment packages are never overwritten.

## Checkpoint policy

`continue_full_matrix`: continue through semantic mismatches so all independent protocol blocks
remain visible. Stop only on repeated toolchain failure, output collision, or unsafe resource use.
All simulations are serial; no parallel build, test, or simulation is allowed.
