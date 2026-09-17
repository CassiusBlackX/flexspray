# Ticket 03 direct-completion and DWRR VL-scan re-verification

Date: 2026-07-25

Working directory:

```text
/Users/ytxing/workspace/ns-3-ub-architecture-integration
```

## Final reviewer coverage

The parameterized RR/DWRR selection-stage tests now explicitly set
`EnableDirectAllocationCompletion=true`. Each fixture preloads a physical egress packet and uses a
counting fault callback. Reentrant disposal from `IsFcLimited` must retain that packet and leave the
transmit count at zero, directly covering the synchronous completion branch.

A dedicated DWRR test places ingress packets in two non-empty VLs after an initially empty VL.
`IsFcLimited` disposes the allocator while evaluating the first candidate. The test requires exactly
one eligibility query, both packets and their accounting to remain in their original VOQs, the
pre-existing egress packet to remain untouched, zero egress transmission, zero pending or active
residue, and safe callback exit. This directly rejects continued scanning into the later non-empty
VL after stop.

All earlier Ticket 03 tests remain registered and passing.

## Validation

All commands ran serially after confirming no other `ninja`, `test-runner`, or `ns3` process.

```text
git diff --check -- src/unified-bus/test/ub-test.cc
exit 0

ninja -C cmake-cache unified-bus-test
exit 0

./build/utils/ns3.44-test-runner --test-name=unified-bus-switch-voq --verbose
exit 0
PASS unified-bus-switch-voq 0.003 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-lifecycle
exit 0
PASS unified-bus-lifecycle 0.155 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-tp-reservation
exit 0
PASS unified-bus-tp-reservation 0.079 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-ctp-lifecycle
exit 0
PASS unified-bus-ctp-lifecycle 0.013 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-transport-retrans
exit 0
PASS unified-bus-transport-retrans 0.007 s

./build/utils/ns3.44-test-runner --test-name=unified-bus-parallel-lifecycle
exit 0
PASS unified-bus-parallel-lifecycle 0.000 s

./build/utils/ns3.44-test-runner --test-name=unified-bus
exit 0
PASS unified-bus 0.154 s
```

Expected child-process negative tests printed abort diagnostics, but all suite roots and process
exit statuses passed. No performance experiment or canonical canary was created or run by this
test implementation.
