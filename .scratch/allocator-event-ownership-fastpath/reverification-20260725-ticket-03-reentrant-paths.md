# Ticket 03 reentrant allocator-path re-verification

Date: 2026-07-25

Working directory:

```text
/Users/ytxing/workspace/ns-3-ub-architecture-integration
```

## Added production-path coverage

Both new scenarios run against real switch fixtures and are parameterized for RR and DWRR.

The selection-stage scenario enters through `TriggerAllocator`, `RunAllocation`, and the production
RR `SelectNextIngressQueue` or DWRR `FindNextEligibleIngressQueue` path. Flow control calls
`Dispose()` reentrantly from `IsFcLimited` and returns false. The assertions require:

- callback continuation without dangling access;
- no ingress dequeue or egress enqueue;
- unchanged VOQ packet and byte accounting;
- no active owner or newly retained event after stop;
- exact forced residue: RR 1 for its deadlock event, DWRR 0.

The commit-stage scenario places two packets in one VOQ and calls `Dispose()` reentrantly from
`OnEgressEnqueued` for the first packet. The assertions require:

- exactly one egress commit and no second packet move;
- completion of `NotifySwitchDequeue`, `OnIngressReleased`, and ingress credit accounting for the
  committed packet;
- one packet and matching byte occupancy left in VOQ, with one packet in egress;
- no notification, retrigger, or later allocation event after stop;
- exact forced residue: RR 2 for deadlock plus active queue, DWRR 1 for active queue only.

All earlier Ticket 03 tests remain registered and passing.

## Validation

Every build and suite ran serially after confirming no other `ninja`, `test-runner`, or `ns3`
process.

```text
git diff --check -- src/unified-bus/test/ub-test.cc
exit 0

ninja -C cmake-cache unified-bus-test
exit 0

./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq --verbose
exit 0
PASS unified-bus-switch-voq 0.002 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle
exit 0
PASS unified-bus-lifecycle 0.163 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-tp-reservation
exit 0
PASS unified-bus-tp-reservation 0.085 s

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
PASS unified-bus 0.155 s
```

Expected child-process negative tests print abort diagnostics in lifecycle, TP reservation, CTP
lifecycle, and aggregate execution; every suite root and process exit status passed.

No performance experiment or canonical canary was created or run by this test implementation.
