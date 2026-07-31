# Ticket 02 implementation ledger

Status: complete

## Acceptance suite

- MTP-enabled targeted `unified-bus-test`, `test-runner`, and `ub-quick-example` builds with at most
  eight build jobs.
- `unified-bus-tp-reservation --fullness=QUICK` for repeated destroy, explicit endpoint owner
  acknowledgement, late drain-only materialization, callback order, and quarantine installation.
- `unified-bus --fullness=QUICK` for unchanged packet, ACK/RTO, queue, routing, and congestion paths.
- Frozen local and MTP canonical byte comparisons. MPI and hybrid delayed-destroy acceptance remains
  required before this ticket closes when the matching runtime is available.

## Batch Reuse Map

Pre-refactor implementation commit: `9969f56`

Semantic batch: explicit two-endpoint close acknowledgement and final reservation fence.

Old active symbols:

- `TpConnectionManager::DestroyConnectionOwned`
- `TpConnectionManager::RequestDestroyNotice`
- `TpConnectionManager::RequestEndpointDestroy`
- `TpConnectionManager::OnLocalEndpointDestroyed`
- `TpConnectionManager::OnEndpointDestroyed`
- `TpConnectionManager::TryFinalizeConnection`
- `TpConnectionManager::ApplyLifecycleControl`
- `UbTransportChannelManager::DestroyTp`
- `UbTransportChannelManager::AssessTpQuiescenceLocked`

Old call paths:

- Explicit connection destroy -> canonical sidecar -> per-owner notice/destroy -> endpoint callback ->
  final view erase -> quarantine -> callback.
- Closing packet ingress -> owner-local reservation -> drain-only materialization -> ordinary
  transport receive -> endpoint quiescence -> callback.

Continuous code blocks reused: existing destroy API, callback bundle, causal lifecycle control
routing, transport `FreezeNewWork`, owner-local quiescence assessment, endpoint destroy callback,
view erase, and Ticket 01 quarantine.

Reuse mode: retain the current close state machine and remove its inferred peer retirement. Use one
owner notice as the close request and require one explicit completion acknowledgement per slot.

Required lifecycle adaptations:

- Both slots enter `DRAINING`; the canonical owner never marks an unmaterialized slot retired.
- Each endpoint owner records close admission before inspecting local backing.
- A materialized owner calls existing `DestroyTp`; an owner with no backing acknowledges only from
  its local empty endpoint/transaction state.
- Late drain-only materialization reopens that slot to `DRAINING` before its eventual callback.
- Final view erase remains gated on explicit `RETIRED` acknowledgements from both slots.

Preserved ordering and observable behavior: packet parsing, ACK/RTO, routing, queues, allocator,
congestion control, and transport quiescence algorithms remain unchanged. Control messages carry
immutable identities only. Packet lifecycle bookkeeping remains connected until Ticket 05.

Existing characterization: Ticket 01 explicit paired destroy fixture, owner-local ingress tests,
P2 delayed-destroy failure at 136.720 ns, and frozen local/MTP canaries.

Explicit behavior changes: no peer slot retirement inferred from another endpoint callback; every
slot supplies its own close acknowledgement before quarantine installation.

Excluded paths: packet bookkeeping removal, dormancy removal, Jetty/TPG migration, ACK/RTO changes,
wire generation, timeout quarantine, and customer convenience policy.

Focused validation: targeted builds, TP reservation suite, UnifiedBus QUICK suite, then frozen
local/MTP bytes. A real admitted delayed-destroy fixture must precede MPI/hybrid closeout.

Re-approval triggers: any change to packet format, ACK/RTO, transaction completion, routing, queue,
allocator, congestion-control, or public destroy API semantics.

## Accepted-operation continuation batch

The first real admitted-WRITE fixture proved that `FreezeNewWork()` also blocked later segments of
the already accepted operation. The narrow correction keeps normal TP resolution and admission
closed after drain, while an existing direct-TP operation record may resolve the exact owner-local
DRAINING backing and admit only its remaining segments through the unchanged capacity checks. It
does not change packet, ACK/RTO, queue, transaction-completion, allocator, routing, or congestion
algorithms. The focused fixture must prove both sides of the boundary: a new WQE is rejected after
connection drain begins, while the previously admitted WQE completes before connection retirement.

## Current validation evidence

- `git diff --check`: passed before the targeted build.
- `ninja -C cmake-cache -j8 unified-bus-test test-runner`: passed; existing compiler and linker
  warnings only.
- `./build/utils/ns3.44-test-runner --test-name=unified-bus-tp-reservation
  --fullness=QUICK`: failed with exit 139 in the new admitted-write delayed-destroy case.
- The diagnostic rerun with `--verbose --stop-on-failure` reproduced the same failure: all 21
  preceding TP reservation cases passed, then `UbExplicitConnectionDestroyDrainsAdmittedWriteTest`
  reached transport admission and began close, but its admitted WRITE had not completed by 1 ms.

Implementation is stopped after the repeated failure. The current manager diff independently removes
peer-retirement inference and routes both endpoint notices through the existing `DestroyTp` path,
including dormant backing and explicit `NOT_FOUND` acknowledgement. Resolving the admitted-write
failure requires deciding whether coordinated Jetty/transaction close belongs in Ticket 02; those
paths are excluded from the current approved batch.

## 64 KiB accepted-lineage diagnosis

The real WRITE fixture is one 64 KiB transaction segment split into sixteen 4 KiB transport
packets. The receiver creates the transaction response only after the final packet completes local
reassembly. By then the receiver endpoint is DRAINING, but `EnqueueRemoteRtpSource` used the normal
ACTIVE-only identity lookup and silently discarded that response before the existing accepted-work
resolver ran. The 4 KiB experiment passed only because its single packet generated the response
before the drain notice took effect.

The correction carries the exact receiver TP generation already recorded on the accepted inbound
segment onto its response and uses that identity with `ResolveTpForAcceptedWork`. Normal endpoint
identity lookup remains ACTIVE-only, and packet format, ACK/RTO, allocator, routing, queue,
congestion-control, and transaction completion algorithms remain unchanged.

## Final evidence

- Targeted `ub-mtp-remote-tp-regression`, `unified-bus-test`, and `test-runner` builds passed in the
  release MPI+MTP configuration.
- `unified-bus-tp-reservation --fullness=QUICK` passed, including local and true MTP cross-owner
  delayed destroy. `unified-bus --fullness=QUICK` also passed.
- The delayed-destroy fixture now uses one 64 KiB WQE split into two 32 KiB transaction segments
  with sender TP capacity one. This forces an inter-segment admission gap after close begins.
- The stronger fixture passed in MPI-only (`--mtp-threads=1`) and hybrid
  (`--mtp-threads=2`) execution; both ranks printed `PASSED` and both commands exited zero.
- The stronger fixture confirms the existing owner-local transaction binding is the quiescence
  evidence for accepted-but-not-yet-materialized segments. No replacement fence, global counter,
  or accepted-operation ledger was added.
- Current local canonical output is raw-byte-identical to the frozen baseline (`cmp=0`, SHA-256
  `10f54f22c7b3f8e935b08c87eb9ed1e8d8e6384fc8ece55c8b77b5a6e0b9dd64`). The frozen MTP
  replay remains byte-identical (`cmp=0`, SHA-256
  `72a03b332b4fb144ad1a3ccc03d454f1c7e506ceafcb6274360b5ca96b7579f6`).
- No MPI/hybrid canonical baseline exists in the frozen artifact set. The quick-example bootstrap,
  parser, and remote-link failures are recorded as separate follow-up work and are not reported as
  lifecycle acceptance.
- Final review found one coverage gap around accepted work spanning a transport-capacity gap. The
  stronger multi-segment fixture closed it without an implementation change and passed in local,
  MTP, MPI-only, and hybrid modes. No other actionable finding remained.
- `git diff --check` passed.
