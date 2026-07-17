# Unified Bus `next` Routing Calibration Task Brief

## Scope

Calibrate the lifecycle implementation branch to the canonical routing model published by
GitCode `next@cf8ee683e00e5b6210661013e93cec7217d662e5`, then compare both implementations with
byte-identical inputs. This is a post-T22 integration task; completed T00-T22 work is not reopened.

## Batch Reuse Map

Pre-refactor commit: `main@ccf41b58b0ba3b9711efea595732a2908e3e07cf`

Semantic batch: canonical routing type and multipath-selector calibration

Old active symbols:

- `UbRoutingProcess::GetOutPort`
- `UbRoutingProcess::SelectOutPort`
- `UbRoutingProcess::SelectAdaptiveOutPort`
- `UbTransportChannel::CreateDataPacket`
- `UbCtpTransportService` routing-policy propagation
- `UbLdstApi` routing-policy propagation
- `UbDataLink::GenPacketHeader`
- `UbDatalinkPacketHeader` routing bits

Old call paths:

- RTP/CTP/LDST packet builder -> datalink routing bits -> `UbSwitch` parser ->
  `UbRoutingProcess::GetOutPort`
- `network_attribute.txt` -> ns-3 attributes -> app/service routing policy and switch selector

Continuous code blocks reused:

- `next@3d4d88b` `RoutingType`, `MultipathSelector`, conversion and validity helpers
- `next@3d4d88b` canonical header encode/decode and policy propagation blocks
- `next@3d4d88b` `HASH64`, `CRC32`, `TOEPLITZ`, `ROUND_ROBIN`, `ADAPTIVE`, and
  `INGRESS_PORT_STRIPE` selection blocks
- `next@185a88b` deferred CTP validation correction
- Existing `next` routing-mode fixtures and real `t-*-packet-rr` scenario assets

Reuse mode: copy-and-adjust from the GitCode `next` active path

Required lifecycle adaptations:

- Preserve lifecycle-owned RTP descriptors and derive the per-packet hash field from descriptor PSN.
- Preserve packet-attempt registration and typed terminal placement around existing builders.
- Preserve lifecycle-owned CTP queues/entities while replacing only routing-policy storage and inputs.
- Keep current route storage ownership and candidate-view extraction while making selector behavior
  match `next`.

Preserved ordering and observable behavior:

- Packet header/tag construction order except for the `next`-defined routing-bit representation.
- Candidate ordering, adaptive scoring, ingress exclusion, and `next` selector cursor updates.
- Lifecycle register/terminal order, ACK/RTO/GBN state, congestion accounting, queue scheduling,
  callbacks, timers, and trace timing.

Existing characterization:

- `next` routing-mode unit tests introduced by `3d4d88b` and corrected by `185a88b`.
- Current lifecycle routing, CTP, LDST, transport, and packet-attempt suites.
- Byte-identical-input `next` vs lifecycle comparison using the existing `next` RR cases.

Explicit behavior changes:

- Replace the lifecycle-only rule `packet spray => sport % candidate_count` with `next`'s
  independent `RoutingType` and `MultipathSelector` model.
- `ROUND_ROBIN` uses the `next` candidate-set cursor. Per-packet `HASH64` remains hashing rather
  than being silently converted to strict RR.
- Existing two-boolean routing configuration is migrated to the canonical four-value routing type.

Excluded paths:

- ACK/TPACK/TPNAK/TPSACK semantics and GBN/selective retransmission algorithms
- Packet payload, TA/TP/network header builders beyond routing-bit propagation
- Queue/allocator scheduling, flow control, congestion control, lifecycle ownership, and cleanup
- Routing-strategy experiment documentation and large generated result packages from `next`

Focused validation:

1. Targeted `unified-bus` build with at most `-j2`.
2. Canonical routing-mode focused tests, plus lifecycle routing/CTP/LDST/transport suites.
3. One simple RR characterization before broader scenarios.
4. Serial frozen-binary runs for GitCode `next` and the calibrated lifecycle branch using an
   identical RR case manifest.
5. Raw-byte comparison of shared stable artifacts; lifecycle-only evidence is parser-validated.

Re-approval triggers:

- A change to ACK/RTO/retransmission, packet payload/header construction order, queue scheduling,
  congestion accounting, lifecycle ownership, or cleanup semantics.
- A `next` caller cannot be mapped without inventing a new public routing model.
- A focused test fails for an unclear reason, or the same failure repeats twice.
- Byte output differs outside lifecycle-only evidence or a precisely explained existing lifecycle
  exception.

## Current Evidence

- GitCode `origin/next` resolves to `cf8ee683e00e5b6210661013e93cec7217d662e5`.
- Current lifecycle packet spray selects `key.sport % candidateCount`.
- GitCode `next` defaults to `MultipathSelector::HASH64`; strict RR exists only when
  `MultipathSelector::ROUND_ROBIN` is selected and advances a cursor keyed by candidate set.
- Therefore the two implementations are not generally byte-equivalent before calibration.
