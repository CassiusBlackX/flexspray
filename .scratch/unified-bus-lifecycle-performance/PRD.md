# Specification: Unified Bus Lifecycle Runtime Efficiency

Status: ready-for-agent

Source baseline: completed Unified Bus lifecycle implementation through T22, with behavior baseline
`main@ccf41b58`, routing calibration against `origin/next`, and the archived p01/p02 differential
evidence from 2026-07-16.

## Problem Statement

The Unified Bus lifecycle implementation preserves the exercised protocol behavior and gives
packets, work, leases, events, bindings, and resource generations explicit ownership and terminal
conditions. Representative performance comparisons nevertheless show two distinct host-runtime
costs. A packet-heavy LDST workload remains roughly 17-20 percent slower after packet-spray and
lifecycle trace output are removed, which localizes the remaining regression to always-on lifecycle
bookkeeping. A resource-heavy RTP workload spends most of its additional wall time serializing
lifecycle SUMMARY records at teardown and retains additional host memory even when lifecycle trace
output is disabled.

The current measurements compare an earlier lifecycle candidate with `main@ccf41b58`; they do not
establish the exact regression of the current branch against the current GitCode main and next
reference revisions. The accepted T21 policy also maps the minimal observability preset to lifecycle
SUMMARY based on a bounded single-case measurement. The later five-run p02 evidence demonstrates
that this default is expensive for resource-heavy workloads, so changing it must be an explicit
observability-policy revision rather than an undocumented optimization.

The simulator needs lower host wall time and bounded lifecycle storage without weakening the
always-on correctness ledger, changing modeled network behavior, introducing a second trace writer,
or treating automatic TP cleanup as an available optimization.

## Solution

Make the minimal observability preset omit lifecycle trace output while preserving lifecycle SUMMARY
for the balanced preset, lifecycle EVENT for the detailed preset, and every explicit lifecycle-level
override. Keep correctness accounting mandatory and independent from trace output.

When SUMMARY is enabled, retain the existing schema, filenames, record order, parser contract, and
record bytes while reducing repeated lifecycle-layer locking, sink resolution, filename construction,
and per-record dispatch. Reuse the existing bounded trace writer and its accepted chunk, pressure,
and file-descriptor behavior rather than adding another buffering or I/O system.

Use refreshed, serialized current-branch measurements to select further optimizations. Reduce
steady-state packet-attempt and lease bookkeeping only through container and notification changes
that preserve identity, duplicate-terminal, underflow, stale-generation, causal-order, and drain
invariants. Release heavy retired-resource state while retaining a compact generation tombstone and,
only when required by enabled evidence, a final summary snapshot. Remove repeated RTP transport
resolution and TPG membership materialization without changing admission, selection, retransmission,
or callback timing. Treat selective-retransmission timer scanning as an independent protocol-specific
optimization with its own characterization.

Each optimization is accepted through a complete current behavior and performance slice. Frozen
reference binaries are built once per required revision and reused serially. The packet-heavy p01
case is measured with packet spray and lifecycle trace disabled; the resource-heavy p02 case is
measured with lifecycle OFF and SUMMARY separately. Normal tickets use focused tests and targeted
builds, while byte comparison is reserved for the SUMMARY contract milestone and final performance
acceptance.

## User Stories

1. As an OpenUSim user, I want the minimal observability preset to prioritize the fastest bounded run, so that resource-heavy simulations do not pay for lifecycle files I did not request.
2. As an OpenUSim user, I want the balanced preset to retain lifecycle SUMMARY evidence, so that ordinary diagnosis still includes final lifecycle balance.
3. As a diagnostics user, I want an explicit lifecycle trace override to take precedence over a preset, so that I can request OFF, SUMMARY, EVENT, or DETAIL deterministically.
4. As a diagnostics user, I want explicitly enabled SUMMARY artifacts to retain their schema, filenames, order, and bytes, so that existing parsers and archived workflows remain valid.
5. As a simulation user, I want trace policy to change host observability cost only, so that task completion, throughput, packet behavior, and simulated time remain unchanged.
6. As a simulator maintainer, I want lifecycle correctness accounting to remain mandatory, so that performance work cannot hide stale generations, duplicate terminals, underflow, or premature drain.
7. As a simulator maintainer, I want SUMMARY generation to reuse the accepted trace writer, so that optimization does not create a second buffering, flushing, or file-descriptor policy.
8. As a performance engineer, I want current HEAD, GitCode main, and next measurements to reuse frozen binaries and identical inputs, so that build drift and run order do not contaminate comparisons.
9. As a performance engineer, I want packet-heavy and resource-heavy costs measured separately, so that packet bookkeeping is not confused with teardown serialization or storage retention.
10. As a performance engineer, I want warm-up and counterbalanced repeated measurements, so that a one-off host timing result cannot select an optimization.
11. As an LDST user, I want packet-attempt tracking to become cheaper without changing routing, transfer, terminal, or fault behavior, so that large MEM workloads retain their existing semantics.
12. As an RTP user, I want ACTIVE endpoints to avoid destroy-only notification work, so that normal packet and lease completion does not repeatedly check a drain condition that cannot yet succeed.
13. As an RTP user, I want transport references resolved once per admission attempt, so that generation safety does not require duplicate lookup work.
14. As a TPG user, I want unchanged membership to reuse a generation-aware materialization, so that repeated transmit triggers do not rebuild the same member list.
15. As a lifecycle maintainer, I want retired resources to release heavy active containers, so that long task churn does not retain every historical set, map, and callback.
16. As a lifecycle maintainer, I want retired identities to retain compact generation history, so that late work still fails as stale rather than mutating a reused local identifier.
17. As a retransmission user, I want timer checks to avoid unnecessary outstanding-window scans, so that large selective windows do not introduce avoidable superlinear host work.
18. As a protocol developer, I want ACK/RTO, queue, scheduler, parser, packet-builder, and congestion algorithms preserved, so that runtime optimization does not become a protocol rewrite.
19. As a contributor, I want every performance change tied to a red feedback loop and focused characterization, so that inferred hotspots are measured before broader redesign.
20. As a maintainer, I want each completed optimization independently reviewable and revertible, so that one unsuccessful idea does not invalidate the other gains.
21. As a release reviewer, I want final current-to-main and current-to-next evidence, so that performance claims identify the exact compared revisions.
22. As a release reviewer, I want known output differences explicitly classified and unexplained differences rejected, so that faster execution cannot silently change modeled results.

## Implementation Decisions

- Treat this work as a performance follow-up to the completed lifecycle feature rather than reopening or rewriting T00-T22.
- Revise the observability policy narrowly: minimal resolves to lifecycle OFF, balanced resolves to SUMMARY, detailed resolves to EVENT, and custom or explicit lifecycle-level configuration remains authoritative.
- Preserve the lifecycle trace schema, filenames, fixed fields, enum registry, parser inputs, record ordering, and bytes when the same explicit level is selected.
- Reuse the accepted bounded trace writer, owner-local chunks, pressure flushing, file registry, append behavior, and descriptor limits. Do not add an asynchronous writer or parallel buffering layer.
- Keep the correctness ledger always enabled. No performance switch may disable generation validation, packet balance, lease balance, terminal uniqueness, drain readiness, or causal owner routing.
- Build and hash one frozen executable for each compared revision and compatible runtime configuration, then reuse it for all repetitions.
- Use the current GitCode main and a pinned next commit as separate controls. Do not infer current-to-next performance from the existing single-run semantic package.
- Use p01 without packet spray and with lifecycle trace OFF as the primary packet-bookkeeping feedback loop.
- Use p02 with lifecycle OFF and SUMMARY as separate resource-storage and observability feedback loops.
- Require one warm-up and five counterbalanced measured pairs for a gating wall-time conclusion. Run all builds, tests, and simulations serially.
- Prefer container substitutions, redundant-lookup removal, and drain-only notification gating before introducing new abstractions.
- Install TP lifecycle state-change notification only when a destroy request transitions an endpoint to DRAINING, with an immediate post-install drain check so no completion edge is lost.
- Preserve packet-attempt identity and out-of-order terminal validation when changing the active-attempt container. Do not replace exact membership with a counter alone.
- Split active resource state from compact generation history. Retired active containers may be released only after their balance and terminal invariants hold.
- Retain final summary data only when the selected observability contract requires it; compact generation history remains available for stale-generation rejection.
- Reuse an already validated RTP transport resolution through preflight and admission instead of resolving the same reference twice.
- Cache TPG member materialization only behind generation and membership-version validation, with invalidation on binding, membership, drain, and destroy changes.
- Optimize selective retransmission separately and preserve its ACK, SACK, RTO, retry ordering, and descriptor-retirement behavior.
- Do not claim benefit from automatic TP cleanup. Task-driven removal, idle teardown, and RSS-triggered teardown remain disabled.
- Treat a 10 percent wall-time regression as the investigation threshold for p01 and p02. RSS must not regress by more than 10 percent, and the final report must retain absolute values as well as percentages.

## Testing Decisions

- Test observability policy at the production preset-to-attribute boundary rather than by inspecting private flags.
- Reuse the existing OFF/SUMMARY/EVENT/DETAIL cases, lifecycle parser, schema validation, trace writer tests, and quick-example output comparison.
- For the SUMMARY optimization, compare explicitly enabled pre-change and post-change lifecycle trees byte for byte, including schema metadata and every node/rank file.
- Verify that minimal, balanced, detailed, custom, and explicit override combinations produce the documented lifecycle level.
- Verify that OFF and SUMMARY produce identical task statistics, throughput evidence, packet/protocol traces, completion state, and simulated time.
- Use the existing lifecycle coordinator tests for duplicate birth, duplicate terminal, underflow, stale generation, retired mutation, and drain-blocking coverage.
- Use packet-heavy p01 with packet spray disabled and lifecycle trace OFF to measure always-on bookkeeping independently from known routing and observability differences.
- Use resource-heavy p02 with lifecycle OFF to measure core runtime and peak RSS, then enable SUMMARY on the same binary and inputs to isolate summary cost.
- Record elapsed, user CPU, system CPU, peak RSS, page faults, file I/O, lifecycle record count, lifecycle bytes, and exact revision/binary hashes.
- Use one warm-up and five counterbalanced measured pairs for performance gates; preserve every raw timing record and failed or inconclusive run.
- Before changing a suspected hot path, establish a tight red feedback loop and collect either a bounded profile or site-specific counters. Code inference alone does not select a broad rewrite.
- After changing active-attempt or lease containers, reuse production packet tags and terminal paths rather than a simplified identity oracle.
- After changing TP notifications, verify ACTIVE steady state, idle destroy, busy destroy, reentrant terminal/lease completion, callback exactly once, late generation, and MTP owner routing.
- After compacting retired state, verify local-ID reuse, generation increment, stale mutation rejection, explicit summary retention, forced stop, and zero active residue.
- After reusing RTP resolution or TPG membership, verify direct TP, TPG, member add/remove, draining member, no eligible member, shared TP, retransmission pinning, and existing selection order.
- After changing selective timer checks, verify GBN remains unchanged and SELECTIVE preserves ACK/SACK/RTO timing, retry set, and completion.
- Run focused suites and targeted builds per ticket. Do not run repository-wide tests unless a public interface or build configuration change requires them.
- Run frozen byte comparison only for the SUMMARY artifact-contract ticket and final performance acceptance, unless a focused failure exposes an unexplained output difference.
- Accept a known byte difference only through a narrow revision-, runtime-, case-, path-, and hash-bound exception with an explained cause and impact.

## Out of Scope

- Automatic TP cleanup after task completion, idle timeout, host RSS pressure, or allocator pressure.
- A new TP creation, close, reset, or teardown wire protocol.
- Rewriting packet builders, packet parsers, protocol state machines, ACK/RTO algorithms, queue scheduling, routing selection, flow-control, or congestion control.
- Replacing exact lifecycle correctness with sampling, approximate counters, or a production off switch.
- Changing lifecycle trace schema, enum values, filenames, parser inputs, or canonical record order.
- Adding a second trace writer, asynchronous I/O thread, or unbounded queue.
- Using the known macOS umbrella-suite status-139 residual as a performance acceptance gate.
- Claiming large-scale memory benefit from TP reclamation before on-demand TP cleanup is separately designed and implemented.
- Running several build, test, profiling, or simulation processes concurrently.

## Further Notes

- The p01 and p02 measurements identify performance categories, not a complete function-level CPU profile. SUMMARY serialization is strongly isolated for p02; individual packet-bookkeeping hotspots remain hypotheses until measured on the current branch.
- The previous T21 decision to keep minimal/SUMMARY was based on a bounded single-case measurement. This specification supersedes only that default mapping, using the later repeated p02 evidence while preserving balanced/SUMMARY and every explicit level.
- Trace settings affect host wall time and evidence volume, not simulated network throughput semantics.
- Storage policy is distinct from modeled resource lifecycle. Compacting retired host state must not imply logical TP closure or automatic connection teardown.
- The untracked 2026-07-16 differential package is local evidence and must be archived or regenerated with explicit revision hashes before it becomes the final portable acceptance artifact.
