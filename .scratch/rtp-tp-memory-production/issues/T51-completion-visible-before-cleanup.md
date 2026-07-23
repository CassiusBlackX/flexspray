# T51: Publish Task Completion Before Task-Scoped Cleanup

Status: complete

Blocked by: none

## Objective

Preserve two distinct completion events: local WQE/traffic completion remains the network-performance
timestamp, while workload completion becomes visible only when `ApplyTaskCompletion` commits the
completed state. Task-scoped Jetty cleanup must begin only after that visible completion commit.

## Public Test Seams

- `UbApp::OnTaskCompleted` plus the public `UbTrafficGen` task-state and completion-event behavior.
- The named `unified-bus-lifecycle` suite for Jetty lifecycle ordering.
- The named `unified-bus-parallel-lifecycle` suite for owner-local MTP ordering when applicable.

## Required Behavior

- `OnTaskCompleted` records/submits local completion without immediately draining the task-scoped
  Jetty.
- `ApplyTaskCompletion` commits `COMPLETED`, records `COMPLETE_VISIBLE`, and releases dependencies
  before cleanup can begin.
- Cleanup is then delivered to the Jetty owner through an owner-local event/mailbox round. Do not add
  a per-task global barrier or wait for worker acknowledgements.
- Local completion and visible completion may have the same simulation timestamp, but their event
  order must be deterministic.
- Cleanup completion must not gate dependent-task release and must execute at most once.
- Reusable Jetties and CTP behavior remain unchanged.

## Invariants

- Do not change packet/network timing, TPN allocation, endpoint generation, routing, flow control,
  case schemas, user commands, or canonical event timestamps.
- Do not borrow or mutate a remote LP directly.
- Do not re-enable legacy `RemoveUselessTps` teardown.
- Preserve unrelated existing worktree changes and artifacts.

## TDD Acceptance

1. Add a public-seam regression test that fails on the current ordering: after local completion but
   before `ApplyTaskCompletion`, the task-scoped Jetty has not begun cleanup.
2. Make the smallest implementation change that passes it.
3. Add coverage proving visible completion precedes cleanup, dependent release does not await cleanup,
   and cleanup happens exactly once on the owner path.
4. Build serially and run at least `unified-bus-lifecycle`, `unified-bus-parallel-lifecycle`, and
   `unified-bus` with the matching non-default test runner.
5. Return the RED evidence, GREEN commands/results, modified files, diff summary, and residual risks.

## Implementation Record

- RED: `unified-bus-lifecycle` failed because the task-scoped Jetty was already `DRAINING`
  immediately after `UbApp::OnTaskCompleted`; the required state before visible completion was
  `ACTIVE`.
- GREEN: `UbTrafficGen` now retains one task cleanup request until `ApplyTaskCompletion` commits
  `COMPLETED`, records the canonical completion, and releases ready dependencies. It then uses
  `Simulator::ScheduleWithContext` at zero delay to deliver cleanup to the source app's owner
  context. Under MTP, the existing cross-LP scheduling path places this event in the owner mailbox.
- Coverage: the lifecycle regression holds a Jetty event lease, proves cleanup has not started at
  local completion, proves cleanup starts only after visible completion, and proves a dependent
  task starts while Jetty retirement is still blocked by the lease.
- Validation:
  - `ninja -C cmake-cache unified-bus-test`
  - `./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle --fullness=QUICK`
  - `./build/utils/ns3.44-test-runner --test-name=unified-bus-parallel-lifecycle --fullness=QUICK --verbose`
  - `./build/utils/ns3.44-test-runner --test-name=unified-bus --fullness=QUICK`
  - `git diff --check`
- No MTP public interface changed. CTP and non-app completion callers keep the existing completion
  path and do not request task-scoped RTP cleanup.
- Reviewer follow-up: only RTP task-scoped Jetties attach cleanup metadata; CTP and reusable Jetties
  retain the completion-only event path. Duplicate identical requests schedule once, while a
  conflicting owner/Jetty request fails fast. Pending requests store node/application indexes rather
  than `Ptr<UbApp>`, so a bounded stop before visible completion does not make the Singleton retain
  the application.
- A true partitioned cleanup system test requires extending
  `src/unified-bus/examples/ub-lifecycle-local-mtp-control.cc` and its current lifecycle-only harness,
  which is outside T51's allowed files. T51 relies on the existing MTP `ScheduleWithContext` remote
  path, which places the zero-delay event in the target LP mailbox; T55 retains the end-to-end MTP
  canonical acceptance responsibility.
- Final MTP hardening: cleanup delivery remains zero-delay outside partitioned MTP. From the public
  LP in a partitioned run it uses `MtpInterface::GetCausalDeliveryDelay(ownerNodeId)` before
  `ScheduleWithContext`, matching `UbParallelLifecycleAdapter::Send` and preventing an event from
  being inserted behind the owner LP's current timestamp. `LOCAL_COMPLETE` and `COMPLETE_VISIBLE`
  timestamps are unchanged; only asynchronous cleanup may arrive one causal delay later.
- Owner-local resolution: the public completion path schedules only stable integer values
  (`ownerNodeId`, application index, task ID, and Jetty number). A static owner-side thunk resolves
  `Node` and `UbApp` only after the event reaches the owner context, so the public LP neither borrows
  nor retains a remote application object.
