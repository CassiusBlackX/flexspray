# Ticket 03 regression coverage implementation

Date: 2026-07-25

Working directory:

```text
/Users/ytxing/workspace/ns-3-ub-architecture-integration
```

## Scope

Only `src/unified-bus/test/ub-test.cc` was changed for implementation. Production allocator,
scheduling, reclaim, flow-control, and transport behavior was not changed.

The added regressions cover the four previously missing Ticket 03 matrix cells:

1. A single owned retrigger blocks reclaim while pending, removes its handle before callback entry,
   and leaves no pending or active owner after resolution.
2. Two output ports retain independent pending owners. The first callback clears only its local
   owner while the second remains blocked; the second callback clears the remaining owner.
3. A real reserved TP that is otherwise safe for backing reclaim is rejected while its output port
   has allocator scheduling work, retains the same backing object, and reclaims after that work
   resolves.
4. Disposal from inside a running allocator callback does not cancel the running callback, reports
   exactly one active-work residue rather than a pending event, clears active ownership, and exits
   without a dangling access.

All tests reuse the existing allocator probe pattern, local TP topology, multi-port ownership model,
and `NS_TEST_ASSERT_MSG_*` assertion style.

## Validation

Before each build or test command, process inspection found no existing `ninja`, `test-runner`, or
`ns3` process other than the inspection command itself.

```text
git diff --check -- src/unified-bus/test/ub-test.cc
exit 0

ninja -C cmake-cache unified-bus-test
exit 0

./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq
exit 0
PASS unified-bus-switch-voq 0.003 s
```

The build emitted two pre-existing unused-function warnings for `WriteCanonicalClosNodeFile` and
`ExtractCanonicalTaskTimes`; no new warning was emitted by the added tests.

No performance experiment or canonical canary was created or run.
