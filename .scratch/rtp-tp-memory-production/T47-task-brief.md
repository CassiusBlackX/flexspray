# T47 Task Brief: Make Jetty Lifetime Explicit

Status: complete

## Authority And Scope

- Ticket: `tickets.md`, Task 47.
- Feature contract: `.scratch/rtp-tp-memory-production/PRD.md`.
- Starting production commit: `8c8dccc`; later ticket commits on the same branch are allowed.
- This ticket classifies Jetty lifetime and retires only application-owned one-shot Jetties after
  completion is visible. It does not destroy TP connections or infer logical lifetime from idleness.
- Existing uncommitted prototype cleanup code in `ub-app*` and `ub-traffic-gen*` belongs to the user.
  Work with it without reverting or staging unrelated prototype hunks; T50 owns prototype removal.

## Confirmed Public Test Seam

Exercise creation, completion, lookup, destroy/drain, CTP binding, and task dependency visibility
through public `UbFunction`, `UbApp`, `UbTrafficGen`, and lifecycle APIs in existing Unified Bus
tests. Do not inspect manager-private containers.

## Lifetime Contract

- Add an explicit `UbJettyLifetime` value owned by the Jetty record: `TASK_SCOPED` or `REUSABLE`.
- Existing public/default/external creation remains `REUSABLE` unless the lifecycle authority opts
  in explicitly.
- RTP Jetties created once for one application task are `TASK_SCOPED`.
- Bound and unbound CTP Jetties cached by communication key remain `REUSABLE`; temporary idleness or
  reference count never changes that classification.
- `UbApp::OnTaskCompleted` publishes the existing trace and traffic-generator completion exactly
  once at the existing timestamp before requesting task-scoped Jetty destroy. Cleanup completion is
  never awaited and cannot gate dependent work.
- The existing Jetty destroy predicate remains authoritative: accepted WQEs, active child work,
  bindings, tracked events, or lifecycle leases keep destroy pending. This ticket does not weaken
  the drain contract.
- A reusable Jetty is not destroyed automatically. A task-scoped Jetty is not returned by a cache
  after destroy starts.

## Planned TDD Slices

1. Default/external and cached CTP Jetties report `REUSABLE`; application one-shot RTP Jetties report
   `TASK_SCOPED`.
2. Successful task completion is visible before task-scoped destroy begins and dependent work is
   released without waiting for cleanup.
3. Active child work keeps destroy pending; final release completes exactly once.
4. Repeated CTP tasks reuse their cached reusable Jetty and are never retired by temporary idle.

## Validation Contract

Run the focused module build and lifecycle/function/application tests serially with the matching
non-default test runner and `build/lib` dylibs. Stage only intended T47 hunks; do not stage the
pre-existing prototype feature. No scale run or full repository test belongs to T47.

## Validation Results

- RED: `ninja -C cmake-cache unified-bus-test` failed before the lifetime API existed.
- GREEN build: `ninja -C cmake-cache scratch_ub-quick-example unified-bus-test` passed.
- `unified-bus-lifecycle`, `unified-bus-ctp-lifecycle`, and `unified-bus` passed with the matching
  non-default test runner and `DYLD_LIBRARY_PATH=build/lib`.
- `git clang-format -f c966c17` formatted the intended function/test delta; `git diff --check`
  passed.
- The implementation index was inspected independently from the dirty worktree and contained no
  prototype flag, prototype cleanup state, or task-driven connection teardown.

## Closeout

- Implementation commit: `a7acaa4` (`feat(unified-bus): classify Jetty lifetime`).
- Standards review: passed with no hard repository-standard violation or actionable Fowler smell.
- Spec review: passed. Follow-up assertions prove cleanup completion does not publish task
  completion twice and repeated CTP communication keys retain the same reusable cached Jetty.
- RTP application Jetties opt into `TASK_SCOPED`; default, external, bound CTP, and unbound CTP
  creation remain `REUSABLE`. Lifetime is stored per Jetty generation and is never inferred from
  idleness or reference count.
- `UbApp::OnTaskCompleted` publishes the existing trace and TrafficGen completion before requesting
  task-scoped retirement. A held tracked event keeps the Jetty draining without blocking task
  completion visibility; final drain progress retires it without a second completion publication.
