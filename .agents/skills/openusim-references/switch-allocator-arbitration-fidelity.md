# Switch Allocator Arbitration Fidelity

<reference-hint>
<use-when>Use this reference when judging if per-output arbitration fidelity suffices, reading allocator results, or designing input-constrained matching experiments.</use-when>
<focus>Known fidelity boundary of the per-output arbitration abstraction, and the measured upgrade path (input-constrained matching, iSLIP iteration count) for studies that need input-port physical fidelity.</focus>
<keywords>iSLIP, switch allocator, arbitration, per-output round-robin, input conflict, double grant, crossbar matching, fidelity boundary</keywords>
</reference-hint>

## Insight

The default per-output-port round-robin arbitration is a **throughput-faithful but input-physics-unfaithful** abstraction.

Under uniform independent traffic at 0.90 offered load it delivers essentially all offered cells (normalized throughput 0.9000), so results that only depend on delivered throughput are not distorted by the abstraction. However, almost every slot contains at least one physically impossible double grant from the same input port. Any conclusion that depends on input-port-level behavior — HOL blocking propagation, input queue interaction, incast/hotspot dynamics at the input side — cannot be drawn from this abstraction.

If a study needs input fidelity, replacing per-output arbitration with an input-constrained matching model is sufficient at modest iteration counts: four iSLIP iterations removed all input conflicts while retaining 0.8998 of the 0.90 offered load. One iteration is not enough (backlog grew ~27x in the probe). At 0.99 offered load, four iterations reached 0.9789, so iteration count becomes a visible fidelity/throughput control knob near saturation and must be reported when used.

A greedy input-constrained round-robin also removed all conflicts at essentially full throughput (0.8999) in the probe, so iSLIP is one option, not the only one.

## Limits

These findings come from a standalone Python probe (16 ports, 20,000 slots, uniform independent cell traffic), not from the integrated simulator. The probe excludes virtual-channel scheduling, packet sizes, flow control, egress queue capacity, ns-3 event scheduling, and allocator latency. Treat the numbers as the fidelity/cost shape of the design space, not as simulator wall-clock or hardware behavior. Any production adoption must re-validate inside the simulator with those layers intact.

## Evidence

Probe configuration: 16 ports, 20,000 slots, uniform independent traffic, 0.90 offered load.

| Algorithm | Normalized throughput | Final backlog | Input-conflict slots |
| --- | ---: | ---: | ---: |
| Per-output RR (current abstraction) | 0.9000 | 58 | 19,990 |
| Greedy input-constrained RR | 0.8999 | 104 | 0 |
| One-iteration iSLIP | 0.8952 | 1,598 | 0 |
| Four-iteration iSLIP | 0.8998 | 129 | 0 |

The throwaway prototype that produced this table lived on the deleted branch `codex/islip-prototype` (2026-07); its README with the full verdict is archived locally at `.scratch/islip-prototype/README-islip.md`.
