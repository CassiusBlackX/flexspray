# Traffic Endpoint Port Selection

## Scope

Expose optional `srcPortId,dstPortId` in `traffic.csv`. The four fields
`sourceNode,destNode,srcPortId,dstPortId` identify the requested endpoint pair;
empty port cells remain independent wildcards. CTP derives its internal entity
identity from the endpoint constraints. RTP, CTP, and LDST use the requested
endpoint ports at their existing public entry points. TP channel internals are
out of scope.

## Batch Reuse Map

Pre-refactor commit: `d06e547c`

Semantic batch: traffic-driven endpoint selection and planned-TP ownership.

Old active symbols: `UbUtils::ForEachTrafficRecord*`,
`UbApp::SendTraffic`, `UbApp::SendCtpUrmaTraffic`,
`UbLdstApi::SendPacket`, `TpConnectionManager::ReservePlannedTpnsForTraffic`,
and `ActivateTrafficFromConfig`.

Old call paths: `traffic.csv` -> `UbUtils` -> `UbTrafficGen` -> `UbApp`; the
quick-example runner reserves planned RTP connections before task activation;
RTP/CTP/LDST retain their existing packet and channel implementations.

Continuous code blocks reused: CSV field parsing, existing CTP hint-port
mechanism, existing route-table lookup and candidate selection, existing TP
reservation/reconstruction, and existing lifecycle cleanup bookkeeping.

Reuse mode: adapt the existing entry-point arguments and lookup keys; no TP
channel or transport state-machine rewrite.

Required lifecycle adaptations: exact endpoint-port traffic must not fall back
to a node-level route; planned connection identity must remain owned by the
driver that reserved it, without claiming preloaded connections.

Preserved ordering and observable behavior: nine-column traffic remains valid;
empty optional port cells are wildcards; explicit port `0` remains distinct
from an empty cell; existing RTP/CTP/LDST behavior remains unchanged when no
port is specified.

Existing characterization: traffic parser, RTP/CTP/LDST focused tests,
planned-TP lifecycle tests, and the lifecycle corpus.

Explicit behavior changes: optional endpoint ports select the endpoint pair;
CSV rows must match the header width; exact destination-port routes are not
silently replaced by node-level fallback.

Excluded paths: TP channel internals, routing-table data structures, unrelated
experiment/generated artifacts, and legacy CTP entity fields in the user CSV.

Focused validation:

- `ninja -C cmake-cache unified-bus-test`
- focused unified-bus traffic/RTP/CTP/LDST tests
- planned-TP reservation and quick-example system tests
- `python3.12 -m pytest -q tests/unified_bus_lifecycle`
- `git diff --check`

Re-approval triggers: changing a TP channel/state machine, changing routing
table semantics, claiming preloaded connections, or adding a new public
entity-ID interface.

## Current Frontier

The existing implementation already carries endpoint ports through traffic,
CTP hints, RTP reservation criteria, and LDST source injection. Before closeout
the following regression boundaries must be fixed and reverified:

1. Exact destination-port lookups in RTP/CTP/LDST must not use primary or
   `ANY_PORT` node-route fallback. The generic wildcard path remains unchanged.
2. Planned-TP identity reuse must distinguish driver-owned reservations from
   preloaded connections and handle reverse traffic deterministically.
3. All traffic parser paths must reject rows whose width differs from the
   header width.
4. The medium CTP lifecycle fixture has one endpoint port per host, so the old
   `srcEntityId=2` rows intentionally become source-port wildcards rather than
   an invalid `srcPortId=1`.

## Review Follow-up

The first implementation review found that the existing generic route helpers
also fall back from a missing endpoint rule to `ANY_PORT`, and CTP used the
generic `GetOutPort` path. The follow-up keeps that behavior for wildcard
traffic and adds exact lookup only for rows with an explicit destination port.
The TP channel and transport state machine remain unchanged.

## Closeout Evidence

- `ninja -C cmake-cache unified-bus-test`: PASS; target was already up to date.
- `./build/utils/ns3.44-test-runner --test-name=unified-bus`: PASS.
- `python3.12 -m unittest discover -s tests/unified_bus_lifecycle -p 'test*.py'`: PASS, 38 tests.
- `git diff --cached --check`: PASS.
- `python3.12 -m pytest -q tests/unified_bus_lifecycle`: NOT RUN; this environment has no
  `pytest` module (`No module named pytest`).
- `./build/utils/ns3.44-test-runner --test-name=unified-bus-examples`: FAIL only at the existing
  seed-2 timing-offset comparison in `src/unified-bus/test/ub-test.cc:32026`; the observed
  difference is the order of two same-timestamp `COMPLETE_VISIBLE` records. All other example
  cases passed, and this failure does not exercise the endpoint-port feature.

The final review checked both traffic parser entry points, wildcard versus explicit port
presence, reverse RTP connection matching, CTP request/response hints, LDST request/response
routing, and the unchanged TP channel/state-machine boundary.
