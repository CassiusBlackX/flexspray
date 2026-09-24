# Real LastAck small-case acceptance

Single-case integration regression, approved in `.scratch/ub21-reliable-rerouting-lastack/design.md` and ticket 01.

Preserve the original CSV topology: hosts 0/1, switches 2/3, parallel switch links at ports 1/2, all links 1200Gbps and 20ns. Four directed URMA_WRITE tasks of 2097152 bytes each begin at 90us. Permanent bidirectional failure of switch 2 port 1 at 100us; detection delay 5us. RTP on demand, TP retransmission disabled, NONE flow control. Shortest-path routing with INGRESS_PORT_STRIPE. Add device-level switch destinations for actual alternate-path LastAck. LastAck uses VL/SL1, three copies, 1s receive timeout, fixed 500ms send deadline.

Keep original resolved choices, regenerate a full snapshot from the new runtime catalog. Trace must prove real source sends, wire receipts, valid receive gates, unique ownership handoff and both nonzero retries; all four input tasks must complete. No output-directory sharing.

## Execution record

2026-09-24: regenerated 144-entry runtime snapshot; case check PASS with tools `0fec3ae`.
Simulation exits 0. Replayed canonical CSV traces with the tools parser: four tasks complete,
382 recovery transfers over both directions. Checker PASS and all 12 negative mutations rejected.
Commands are in the parent README. Historical pre-migration measurements are not current evidence.
