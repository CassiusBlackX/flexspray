# Use compiled structured routing for verified topology regions

## Status

Accepted on 2026-07-22 for the `next` development branch.

## Context

Unified Bus simulations support arbitrary explicit topologies and routing tables. Large regular
topologies can require millions of per-destination forwarding records even though many nodes share
the same coordinate, group, or layered-switch relationship. Generating, parsing, and installing
those records increases startup time and resident memory.

Users should not need to select an internal topology label or reproduce compiler-specific roles to
obtain the optimization. The physical nodes, endpoint ports, links, parallel links, and routing
attributes remain the authoritative input. Existing CSV cases and custom routing must remain valid.

## Decision

Compile verified regions of a physical topology into rule-based forwarding artifacts before the
simulation starts.

1. `ns-3-ub-tools` accepts a Python/NetworkX graph or a JSON/YAML adapter input, normalizes the
   physical topology, discovers candidate structures, verifies them against the authoritative
   endpoint-port graph, and compiles table, rule, or mixed routing.
2. Route generation and runtime representation are independent choices. Preparation accepts
   `full`, `structured`, or `mixed` generation. Runtime uses either `compressed-table-lookup` or
   `structured-rule-compute`; `full` generation supports only the former.
3. The preparation step writes `structured_topology.txt`, `compressed_routing.bin`, and
   `structured_summary.json` into the case directory and exits. `structured` and `mixed` preserve
   both runtime choices in one prepared case. It does not launch ns-3.
4. `scratch/run_structured_topo_case.py` reads the prepared case and launches the existing C++ case
   runner. It does not import NetworkX or compile routes.
5. In `compressed-table-lookup`, Unified Bus loads one complete `UbNextHopIndex` and does not install
   structure rules. In `structured-rule-compute`, it installs structure rules plus only the compact
   supplemental index required by `mixed` generation.
6. One route lookup item is owned by exactly one source. Shortest and non-shortest candidates from
   different sources are not merged.
7. Candidate generation may change, but `GetOutPort()`, routing-type filtering, ingress-port
   filtering, primary-address fallback, `SelectOutPort()`, multipath selectors, and the existing
   one-detour behavior remain the protocol-semantic path.
8. Topology rules are enabled only after exact physical validation. Unknown, incomplete, or unsafe
   overlapping regions remain on exact/range or table routing in `mixed` mode.
9. Existing CSV topology and routing input remains a peer supported path. Loading an old routing CSV
   builds the same compressed in-memory index; tools may also convert it ahead of time. Diagnostic CSV generated
   by the structured compiler is optional evidence, not an intermediate runtime contract.
10. `compressed_routing.bin` is a portable format, not a C++ object dump. Its header contains magic,
    format version, fixed byte order, counts, content fingerprint, payload length, and checksum.
    Missing, corrupt, stale, or unsupported artifacts fail explicitly instead of changing modes.

## Considered options

### Keep only the complete routing table

This preserves the simplest lookup model but retains the dominant generation, parsing, installation,
and memory costs at large scale.

### Require users to declare a supported topology family

This simplifies compilation but couples user input to internal recognizer names and makes small
physical changes fragile. It was rejected in favor of discovering candidates from the graph and
validating them independently.

### Use one universal graph compressor

General graph partitioning, coloring, hashing, and quotient graphs can propose repeated regions but
do not prove forwarding equivalence or preserve endpoint-port semantics by themselves. They may be
used as candidate-generation tools, but verified structure-specific rules and the generic packed
index remain the executable representations.

### Generate and launch in one Python process

This makes the user command shorter but couples tools to a running simulator and prevents a prepared
case from being inspected or rerun independently. Preparation and execution are therefore separate.

## Consequences

- Fully verified regular topologies avoid complete route generation and obtain the lowest startup
  and routing-memory footprint.
- Arbitrary topologies can use the generic packed next-hop index when exact candidate sets are
  available, without adding a topology-specific recognizer.
- Mixed routing allows partial optimization without changing the behavior of unrecognized regions.
- Structured and mixed generation can be reused with either a speed-priority compressed lookup or a
  memory-priority structure-rule computation without rerunning topology discovery.
- Topology-rule lookup performs more computation than a direct complete-table lookup. A compact
  topology next-hop index reduces this cost, but the primary topology-rule benefits remain offline
  generation, input size, startup, and resident memory.
- The structured protocol is a compiled case artifact. Python and NetworkX are not present in the
  packet-processing runtime.
- Numeric routing metrics are currently reduced to a shortest candidate group and one non-shortest
  group. A future policy requiring additional metric levels must extend the compiled representation.
- Custom path finders retain complete baseline validation because the compiler cannot assume their
  routing semantics.

## Validation

Correctness requires complete candidate-set equivalence for every route lookup in the validated
scope, physical ownership of every structured link, and regression coverage of the unchanged final
port selectors. Performance claims must identify the representation, process boundary, query corpus,
sample count, and whether a result measures offline generation, startup, memory, candidate lookup, or
complete `GetOutPort()`.

Normal preparation does not build a full verification baseline. `--verify-routes` performs exhaustive
offline equivalence checking and is required for CI, first acceptance of a new recognizer, and the
release benchmark corpus. Byte-level output comparisons must cover multiple multipath selectors;
because every runtime representation feeds the same `GetOutPort()` path, identical inputs and RNG
state must produce identical selected ports and shortest-path decisions.

The implementation history, current measurements, raw samples, and reproducible benchmark sources
are maintained outside the released tree.
