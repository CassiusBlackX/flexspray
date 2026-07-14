# Implementation Workflow

This repository extends the Matt `implement` workflow for engineering tickets. Matt's workflow
still supplies the base requirements: use TDD at pre-agreed seams, run focused tests and builds
regularly, perform code review, and commit completed work to the current branch.

## Authority

Implementation policy is layered as follows:

1. `AGENTS.md` provides the mandatory entry point.
2. This document defines the repository-wide workflow, approval model, and templates.
3. A feature PRD defines feature-specific baselines, milestones, and stricter constraints.
4. A feature's `tickets.md` defines execution summaries, dependencies, and acceptance suites.
5. The current task brief is a local operational ledger for the active ticket's batch state and
   evidence. It is not a long-term authority.

A feature policy may tighten this workflow. A task brief may not relax this document, its PRD, or
its tickets, and a temporary agent conclusion may not override tracked authoritative documents.

## Ticket Ownership

- One execution subagent normally owns a ticket from start through completion.
- The root agent schedules work, approves batches, independently reviews changes, and runs final
  verification.
- Multiple subagents must not modify the same worktree concurrently.
- The next ticket cannot start until the current ticket has both its implementation commit and its
  closeout commit.

The root agent may appoint a replacement only when the original agent exits unexpectedly, repeats
the same failure twice, makes no actual diff/build/test progress for about 2 minutes, or has
unrecoverable context. At 60 seconds, request a concise progress report; do not interrupt solely
because that checkpoint was reached. An active build/test with continuing output, or an agent that
has already produced a relevant diff and is validating it, counts as progress. Before continuing,
the replacement must read the entire task brief, inspect the dirty diff, approved Batch Reuse Maps,
and recorded validation, then continue from the current frontier without rollback, repetition, or
scope expansion.

The task brief must be sufficient for replacement. Chat history is not its only source of approved
scope or evidence.

## Semantic Batch Approval

Approval applies to a semantic batch, not an individual symbol, caller, or line. Typical batches
include packet copy semantics, control-packet birth, packet terminal paths, retransmission events,
congestion-control events, and cleanup closure. A ticket commonly has 2-4 batches, but that is not a
limit: use the number required by independent semantics, risk, and validation boundaries.

Before a batch starts, its execution agent submits:

```text
## Batch Reuse Map

Pre-refactor commit:
Semantic batch:
Old active symbols:
Old call paths:
Continuous code blocks reused:
Reuse mode:
Required lifecycle adaptations:
Preserved ordering and observable behavior:
Existing characterization:
Explicit behavior changes:
Excluded paths:
Focused validation:
Re-approval triggers:
```

The root agent approves the old active path, batch boundary, and test seam once. Approval remains
valid while changes stay within the listed symbols and paths, use the approved owner/helper/seam,
make only the recorded behavior delta, preserve protocol algorithms, and validate through the
approved production boundaries.

After approval, immediately record the approved Batch Reuse Map, current diff scope, and validation
results in the task brief. Update that ledger as evidence changes. Mechanical caller migration,
equivalent typed-terminal insertion, lifecycle assertions in an existing fixture, formatting and
include ordering, removal of batch-introduced duplication, direct compile/assertion fixes, and
rerunning affected focused tests do not require another approval and must not expand scope.

## Batch Execution

After approval, the agent may continuously:

1. reuse or minimally extract the old implementation;
2. migrate the existing characterization caller first;
3. run the narrowest focused test;
4. migrate the remaining approved callers;
5. run one targeted build and the batch's focused suites;
6. report the complete batch diff and evidence;
7. stop at the root review gate.

## Reuse And Characterization

Reuse priority is mandatory: call the old implementation directly; minimally extract a shared
helper from the old active path; copy the smallest continuous old block and adjust it; only then add
new implementation. For extraction, copy the old function unchanged, migrate the old caller, prove
its behavior with focused characterization, and only then connect a new caller.

Do not create parallel packet builders, parsers, protocol state machines, ACK/RTO algorithms,
queue/schedulers, congestion accounting, or cleanup algorithms. If reuse is impossible, report the
old symbols and paths inspected, the concrete semantic mismatch, and why adaptation is riskier.

Tests first reuse old fixtures, production parsers, traces, fault cases, and real scenarios. Add only
identity, generation, balance, typed-terminal, counter, or causal-order assertions needed by the
lifecycle delta. Do not define a protocol or packet oracle from the new implementation. Delay,
backpressure, retention, and retry are not drops.

## Re-approval Triggers

Stop and return to the root agent when a caller's classification differs from the Batch Reuse Map;
an old builder/parser/state machine/ACK/RTO/queue/scheduler/CC algorithm must change; an unrecorded
old behavior difference appears; a public API, owner model, or lifecycle semantic must be added; an
excluded path is required; delay/backpressure/retention versus drop is unclear; a test failure has
no known cause; a difference may affect milestone bytes; the same failure occurs twice; or about
2 minutes pass without actual diff/build/test progress. At the 60-second checkpoint, report status
and continue when a relevant diff exists or a build/test is actively producing progress.

Report the current diff, completed build/tests, blocker, and smallest options promptly.

## Validation And Review

Validate by batch, not by line. During implementation run only the narrow focused test; after the
batch, run one targeted build and its agreed focused suites. Do not repeat a suite unaffected by a
later diff, and do not run builds/tests concurrently.

At ticket start, resolve the ticket-defined full acceptance suite from the PRD and tickets into
concrete commands, scenarios, and expected artifacts in the task brief. If those documents do not
fully specify it, the Batch Reuse Map must supply the missing items and the root agent must approve
them. Matt's end-of-work full suite means this ticket-defined suite, not automatically repository
wide `./test.py`. Repository-wide testing is reserved for base-module, build-system, public-interface,
or final-integration changes. Record unrelated blockers precisely; never report an unrun part PASS.

Ordinary batches do not run frozen byte comparison. Run it only at a feature-defined milestone.
Exact bytes are the default; a known, explained, acceptable difference requires a narrow approved
exception, while an unexplained difference is escalated to the user.

At batch end, the root agent compares the implementation with its pre-refactor source and checks
old branches and errors, state-update order, header/tag order, callback/trace/timer timing,
ACK/RTO/queue/scheduler/CC behavior, duplicate implementations, missed reuse, birth/terminal
balance, drop classification, and undeclared observable changes. Return only the defective portion.
