# Global packet lifecycle removal tickets

Status: ready-for-agent

Source specification: `spec.md`

## Objective

Remove simulator-global packet lifecycle bookkeeping and TP backing dormancy while preserving
byte-identical UnifiedBus network behavior and making Jetty, TP connection, and RTP TPG explicit
destroy depend only on authoritative owner-local protocol state.

## Dependency Graph

```text
01 owner-local TP ingress and retired identity
  +-> 02 correct TP connection close across execution owners
  +-> 03 migrate remaining correctness users to owner-local state

02 + 03
  -> 04 remove TP dormancy and automatic backing reclaim
  -> 05 remove packet lifecycle bookkeeping and obsolete global machinery

02 + 05
  -> 06 expose opt-in destroy policy and complete integration acceptance
```

Ticket 03 depends on Ticket 01 because TPG and Jetty readiness must resolve TP endpoint identity
through the owner-local live/retired model introduced there. Tickets are implemented by one writer
in dependency order even when a conceptual subpart could be investigated independently.

## Tickets

| ID | Title | Blocked by |
| --- | --- | --- |
| 01 | Resolve live, closing, retired, and invalid TP ingress owner-locally | None |
| 02 | Complete explicit TP connection close across execution owners | 01 |
| 03 | Migrate Jetty, RTP TPG, CTP, and LDST lifecycle dependencies | 01 |
| 04 | Remove TP dormancy and automatic backing reclaim | 02, 03 |
| 05 | Remove packet birth, transfer, terminal, origin-return, and obsolete lifecycle machinery | 04 |
| 06 | Expose opt-in destroy policy and complete compatibility and performance acceptance | 02, 05 |

## Feature Acceptance

- Canonical output is raw-byte-identical to baseline commit
  `68bbce1310dad539a0c053cac6cd14ac69dda5cb` for the frozen local, MTP, MPI, and hybrid manifests.
- A delayed-destroy scenario completes without rejecting valid closing traffic, retiring either
  endpoint early, or reusing a retired TPN.
- Product packet paths contain no lifecycle packet tag, attempt bitmap, global in-flight packet
  count, transfer observation, terminal accounting, or origin-return terminal control.
- Jetty, TP connection, and RTP TPG explicit destroy use only authoritative owner-local state.
- CTP and LDST retain their externally visible task, packet, ACK, and completion behavior without
  global packet ownership records.
- TP backing dormancy, automatic backing reclaim, dormant snapshots, and rehydration are absent.
- Customer convenience destroy is default-off and never affects direct explicit interfaces.
- After correctness passes, the approximately 30-second reversed-order measurement reports retired
  instructions as the primary performance result.

## Execution Rule

Work only the frontier ticket whose blockers are complete. Each ticket receives its own task brief,
semantic Batch Reuse Map, focused validation, separate review pass, implementation commit, and
closeout commit before the next ticket begins.
