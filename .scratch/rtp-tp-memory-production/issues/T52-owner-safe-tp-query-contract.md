# T52: Make TP Query And Resolution Ownership Explicit

Status: complete

Blocked by: T51

## Objective

Keep setup, trace, and diagnostic TP lookup non-mutating while making live-backing observation
explicit. Keep identity-bearing `ResolveLiveTp` as the only synchronous backing restoration seam and
reject restoration outside the endpoint execution owner.

## Public Test Seams

- `UbTransportChannelManager` query, storage snapshot, and resolution APIs.
- Existing dormant-backing tests in `unified-bus-tp-reservation`.
- Existing MTP lifecycle ownership harness where it can express a manager resolution call without
  adding a new example target.

## Required Behavior

- Add `PeekLiveTp(tpn) const`: observation-only, no lock, no restoration, null for missing or compact
  backing.
- Keep `GetTp(tpn) const` source-compatible as a documented legacy alias of `PeekLiveTp`.
- `Contains(tpn)` continues to mean the logical endpoint record exists, including compact backing.
- `GetTpnMap()` remains an observation snapshot and may contain null backing for a compact endpoint.
- `ResolveLiveTp(tpn, identity)` remains the only restoration path. It requires exact active identity
  and execution ownership; foreign rank/LP/public LP must return null without changing storage.
- Migrate production observation-only callers to `PeekLiveTp` so their intent is auditable. Actual TP
  consumers must continue to use `ResolveLiveTp`.

## Compatibility Boundary

Normal cases, CSVs, commands, trace schemas, TPNs, generations, and network behavior remain
unchanged. This ticket preserves the current C++ source surface but explicitly documents that
`GetTp` observes live backing; it does not make a public/setup observer mutate a remote owner in the
name of legacy pointer-return behavior.

## Invariants

- No remote LP borrowing, implicit scheduling, or restoration from observation APIs.
- No context-dependent `GetTp` behavior.
- No new wire/control protocol, flag, case field, or user migration.
- Wrong owner/generation and non-owner execution fail closed with no rehydration increment.
- Preserve all T51 changes and unrelated worktree artifacts.

## TDD Acceptance

1. Add a public-seam RED test for `PeekLiveTp` and a RED owner-execution test for `ResolveLiveTp` if
   the existing harness can express it.
2. Prove compact `GetTp`, `PeekLiveTp`, and `GetTpnMap` observations do not restore backing or change
   the rehydration counter; exact owner resolution does.
3. Prove wrong identity and non-owner resolution leave the compact record unchanged.
4. Build serially; run `unified-bus-tp-reservation`, `unified-bus-lifecycle`,
   `unified-bus-parallel-lifecycle`, and `unified-bus`.
5. Run `git diff --check` and report any owner-system coverage deferred to T55.

## Implementation Evidence

- RED: `unified-bus-test` failed to compile because the public manager seam had no
  `PeekLiveTp` member.
- RED: the first owner gate recursed through `NodeList` during controller disposal and deadlocked
  against TP reclaim; disposal now fails resolution closed before consulting execution ownership.
- RED: enabled-but-unpartitioned MTP aborted in `RequireNodeLocalLpId`; the manager now uses
  `isPartitioned`, `FindNodeLocalLpId`, and the current LP as a non-throwing readiness gate.
- GREEN: `PeekLiveTp` is observation-only; `GetTp` remains its source-compatible legacy alias;
  production observation callers use `PeekLiveTp`; pre-partition setup retains single-thread
  behavior, while partitioned missing/public/foreign ownership fails closed before restoration.
- Passed with the current build-profile runner: `unified-bus-tp-reservation`,
  `unified-bus-lifecycle`, `unified-bus-parallel-lifecycle`, `unified-bus-ctp-lifecycle`, and the
  aggregate `unified-bus` suite.
- Covered in the manager seam: pre-partition and missing-map behavior for reserved, inbound, and
  identity-bearing live resolution. T55 extends the focused harness with a real mapped owner worker:
  foreign and public LP resolution and reclaim fail without mutation, while the mapped owner restores
  and reclaims the exact endpoint identity.
