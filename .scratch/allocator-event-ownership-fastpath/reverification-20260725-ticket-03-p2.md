# Ticket 03 P2 re-verification

Date: 2026-07-25

Working directory:

```text
/Users/ytxing/workspace/ns-3-ub-architecture-integration
```

## Reviewer closure

- Multi-port isolation retains its internal callback-entry oracle and now also uses a real
  three-port switch fixture. The public `ScheduleTriggerAllocator(Ptr<UbPort>)` entry point is
  invoked for two physical output ports, and each physical owner exposes an independent pending
  reclaim gate.
- Running forced stop now enters through `TriggerAllocator`, `ScheduleAllocation`, `RunAllocation`,
  and an overridden `AllocateNextPacket` that delegates to the RR production dequeue path. Disposal
  occurs from `OnIngressReleased` while the production-selected ingress queue is active. The test
  asserts active ownership before disposal, exact active residue of one, zero pending events,
  callback continuation and exit, and a cleared active gate.
- TP reclaim now includes a no-pending control on the same endpoint generation. The quiescent
  control reclaims successfully, rehydrates the same generation, is rejected only after allocator
  work is scheduled, and reclaims again after that work resolves.

The running forced-stop test uses a 20 ns simulator stop boundary so the fixture's unrelated switch
deadlock timer cannot make the test run unbounded. Its standalone allocator probe cancels its own
unrelated deadlock timer before treatment, keeping the forced residue oracle specific to the one
active allocation owner.

## Validation

Before every build or test invocation, process inspection found no other `ninja`, `test-runner`, or
`ns3` process. Commands were executed serially.

```text
git diff --check
exit 0

ninja -C cmake-cache unified-bus-test
exit 0

./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq --verbose
exit 0
PASS unified-bus-switch-voq 0.002 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle
exit 0
PASS unified-bus-lifecycle 0.170 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-tp-reservation
exit 0
PASS unified-bus-tp-reservation 0.082 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-ctp-lifecycle
exit 0
PASS unified-bus-ctp-lifecycle 0.014 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-transport-retrans
exit 0
PASS unified-bus-transport-retrans 0.007 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-parallel-lifecycle
exit 0
PASS unified-bus-parallel-lifecycle 0.000 s

./build/utils/ns3.44-test-runner --test-name=unified-bus
exit 0
PASS unified-bus 0.183 s
```

The abort messages printed by lifecycle, TP reservation, CTP lifecycle, and aggregate execution
come from expected child-process negative tests; each suite root and process exit status passed.

No performance experiment or canonical canary was created or run by this test implementation.
