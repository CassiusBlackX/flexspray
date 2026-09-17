# Per-Case Specifications

Every matrix row inherits topology, workload, routing, transport and network settings from the
matching frozen scenario under `scratch/20260802-main-next-controlled-comparison/cases/`.

- `*-explicit-on`: set both lifecycle attributes to `true` through the parameter writer.
- `*-implicit-default`: omit both lifecycle attributes from the authored snapshot and require the
  candidate runtime catalog to resolve both to `true`.

Materialization must preserve all other fixture hashes and must not run ns-3.
