# Ticket 03 selection-stage egress guard re-verification

Date: 2026-07-25

Working directory:

```text
/Users/ytxing/workspace/ns-3-ub-architecture-integration
```

## Added oracle

The existing RR/DWRR selection-stage reentrant-stop test now preloads one packet into the physical
egress queue before triggering allocator work. A counting fault callback observes any attempted
egress transmission.

After `Dispose()` is called from `IsFcLimited`, both scheduler variants must retain the pre-existing
egress packet byte-for-byte and leave the transmit count at zero. These assertions directly reject
post-stop `NotifyAllocationFinish`, dequeue, or transmit behavior. The original ingress packet,
VOQ accounting, callback-safety, pending-event, active-owner, and forced-residue assertions remain
unchanged.

## Validation

All commands ran serially after confirming no other `ninja`, `test-runner`, or `ns3` process.

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
PASS unified-bus-lifecycle 0.154 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-tp-reservation
exit 0
PASS unified-bus-tp-reservation 0.081 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-ctp-lifecycle
exit 0
PASS unified-bus-ctp-lifecycle 0.011 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-transport-retrans
exit 0
PASS unified-bus-transport-retrans 0.007 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-parallel-lifecycle
exit 0
PASS unified-bus-parallel-lifecycle 0.000 s

./build/utils/ns3.44-test-runner --test-name=unified-bus
exit 0
PASS unified-bus 0.161 s
```

Expected child-process negative tests printed abort diagnostics, but all suite roots and process
exit statuses passed. No performance experiment or canonical canary was created or run by this
test implementation.
