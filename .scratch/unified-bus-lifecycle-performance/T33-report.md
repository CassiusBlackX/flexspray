# T33 Report: OFF lifecycle trace startup setup

## Outcome

Retain the change. Once lifecycle output resolves to OFF, startup now resets lifecycle trace state
with canonical empty configuration and returns before reading detail sampling values, resolving MPI
rank scope, constructing the sink resolver, or entering metadata emission.

The trace directory, runtime-drop diagnostics, queue-sampler state, and setup timestamp retain their
existing order. SUMMARY, EVENT, and DETAIL retain their previous setup block.

## Evidence

- Targeted Unified Bus build: PASS
- `unified-bus-lifecycle`: PASS
- bounded p01 OFF managed artifacts: exact against T32
- bounded p01 SUMMARY managed artifacts: exact against T32
- before-only, after-only, and changed artifacts: none at either level
- T32 library: `a7163d0e...38bb7`
- T33 library: `53ee3918...54353`

This is startup-only cleanup, so no repeated performance claim is made. It removes deterministic
unused work once per simulation without changing modeled behavior or trace bytes.
