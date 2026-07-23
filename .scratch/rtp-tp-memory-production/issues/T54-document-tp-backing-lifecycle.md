# T54: Record The TP Backing Compaction Contract

Status: complete

Blocked by: T51, T52

## Objective

Publish the accepted simulator-local terminology and lifecycle decision in the repository domain
documentation so future TP and congestion-control changes preserve the compaction contract.

## Allowed Files

- `CONTEXT.md`
- One new numbered file under `docs/adr/`
- Existing TP manager or congestion-control Doxygen only if a documented contract is still missing
- This ticket

Do not change executable behavior or tests.

## Required Content

- Use `TP backing compaction` and `compact TP endpoint` in user-facing/domain text. Note that the
  implementation enum may use `DORMANT`, but do not imply disk paging or protocol teardown.
- Distinguish logical endpoint lifetime from live backing lifetime and explicit destroy.
- Define `LOCAL_COMPLETE` for transport/FCT observation and `COMPLETE_VISIBLE` for workload state,
  dependency release, and lifecycle cleanup. They may share a timestamp but have a strict order.
- Record owner-local restoration/reclaim rules, causal owner delivery in partitioned MTP, and the
  prohibition on remote object borrowing.
- Define `PeekLiveTp`, legacy `GetTp`, `Contains`, `GetTpnMap`, and identity-bearing `ResolveLiveTp`.
- Record congestion-control fail-closed eligibility and the work required for future algorithms.
- Record effects and non-goals: no TPN/generation/session reuse, no wire protocol, no case/CLI change,
  no claim of stable speedup, and no trace-volume solution.
- Include acceptance tests and operational consequences.

## Acceptance

- Documentation matches the current code rather than the earlier prototype.
- No hidden flag or unsupported promise is introduced.
- Markdown links/paths and terminology follow repository conventions.
- `git diff --check` passes.

## Implementation Evidence

- `CONTEXT.md` now defines TP backing compaction, compact TP endpoints, live-backing observation,
  identity-bearing backing resolution, and the two task-completion observations.
- `docs/adr/0002-compact-tp-backing-with-owner-local-resolution.md` records the accepted lifecycle,
  completion order, partitioned-MTP owner-delivery rule, query/resolution contracts,
  congestion-control fail-closed extension contract, alternatives, consequences, and acceptance
  evidence.
- The ADR documents current T52 behavior: legacy `GetTp` is source-compatible but remains an
  observation-only alias of `PeekLiveTp`; TP consumers use owner-local `ResolveLiveTp` with an exact
  lifecycle identity.
- No executable behavior or tests changed. `git diff --check` passes for the ticket files.
- Repository ignore rule `docs/*` means the new ADR must be explicitly added when this implementation
  series is staged.
