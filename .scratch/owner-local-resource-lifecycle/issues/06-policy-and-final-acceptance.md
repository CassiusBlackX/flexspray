# 06 — Expose opt-in destroy policy and complete compatibility and performance acceptance

**What to build:** Expose narrow, default-off standard-driver controls for task-scoped Jetty and
planned driver-owned TP connection destroy, then complete the feature-wide byte-level correctness
and performance acceptance package.

**Blocked by:** 02 — Complete explicit TP connection close across execution owners; 05 — Remove TP
dormancy and obsolete global lifecycle machinery.

**Status:** complete

## Batch reuse map

Pre-refactor commit: `01a642d0`

### Batch 1 - default-off Jetty policy and legacy switch removal

- Old active path: `UbApp::OnTaskCompleted` always submits owner-context cleanup for every RTP
  `TASK_SCOPED` Jetty; `CleanupTaskScopedJetty` then invokes the obsolete `RemoveUselessTps()` path
  before explicit Jetty destroy.
- Reuse mode: retain the existing task-completion visibility, MTP causal owner delivery, lifetime
  check, and `DestroyJetty(PROTOCOL_COMPLETE)` call. Gate only convenience cleanup submission with
  `EnableTaskScopedJettyDestroy`, default `false`.
- Explicit behavior change: policy-off standard-driver tasks retain their task-scoped Jetty;
  policy-on preserves the existing completion-before-destroy order. Direct explicit destroy is
  unchanged.
- Removal: delete the `RemoveUselessTp` manager attribute, API, backing field, abort path, obsolete
  tests, and generated case attributes that name it.
- Focused validation: lifecycle and TP-reservation QUICK suites, policy default/off/on assertions,
  and configuration loading smoke.

### Batch 2 - exact standard-driver-owned planned TP close

- Old active path: policy-off `ActivateTrafficFromConfig` reserves RTP connections before accepting
  each traffic row; `UbApp::SendTraffic` later selects from those reservations and materializes the
  selected endpoint through the unchanged `GetTpns`/`ReconstructTPs` path. Policy-on defers its
  owned reservation until the task row has been accepted, so rejected rows cannot orphan ownership.
- Reuse mode: keep reservation creation, random single-path selection, multipath selection,
  materialization, packet processing, and `DestroyConnection` close semantics unchanged.
- Ownership rule: the standard driver's initial-load reservation API returns only exact identities
  created by that call. Those identities are driver-owned; a pre-existing set returns no
  convenience ownership. The manager does not retain a parallel ownership label.
- Plan rule: every accepted task conservatively references the exact driver-owned candidate set for
  its source, destination, and priority. This may retain an unselected candidate until the final
  route-level planned use, but cannot close a candidate before a future runtime selection. It does
  not preassign a TPN or alter RNG behavior.
- Completion rule: after task completion becomes visible, decrement only the precomputed plan
  references. `EnablePlannedTpConnectionDestroy`, default `false`, authorizes explicit close only
  for exact identities whose final declared reference completed. No scan, idle heuristic, queue
  inference, or packet state participates.
- Generation rule: the convenience call validates the recorded connection generation before close;
  it cannot target a later connection occupying the same local number.
- Focused validation: default/off/on policy, shared planned reservation retained until final use,
  preconfigured reservation exclusion, exact generation rejection, and policy independence.

- [x] `EnableTaskScopedJettyDestroy` and `EnablePlannedTpConnectionDestroy` are independent Boolean
      controls and both default to false.
- [x] Direct explicit Jetty and TP connection destroy remains available regardless of policy state.
- [x] Task-scoped Jetty policy touches only Jetties created with declared task-scoped lifetime after
      visible task completion.
- [x] Planned TP connection policy touches only exact reservations created and owned by the standard
      driver after the last accepted planned task referencing each connection completes.
- [x] Reusable, preconfigured, externally created, and customer-managed resources are never touched
      by the convenience policy.
- [x] The legacy task-inference transport cleanup switch and its aborting path are removed rather than
      repurposed.
- [x] Policy-off and policy-on canonical network outputs are byte-identical when scoped resources
      have no later declared use.
- [x] The complete frozen local, MTP, MPI, and hybrid manifests are raw-byte-identical to baseline.
- [x] Reversed-order approximately 30-second measurements report retired instructions as primary and
      wall time as supporting evidence; the prototype's median `3.89%` reduction is directional only.
- [x] Throwaway prototype and microprobe source files are excluded from the production commit while
      their decision-relevant findings remain documented.

## Implementation evidence

- Targeted build: `ninja -C cmake-cache unified-bus-test test-runner` passed.
- Focused suites: `unified-bus-lifecycle`, `unified-bus-tp-reservation`, and `unified-bus` passed.
- Policy off/on canonical output SHA-256:
  `2e1581e95949ae744b0abcd2bb2acdc3653c654e40f6e3bc496b9bbb5ec69d2f`.
- Ticket 05 baseline versus Ticket 06 default-off binary was byte-identical for local
  (`2e1581e95949ae744b0abcd2bb2acdc3653c654e40f6e3bc496b9bbb5ec69d2f`) and MTP
  (`9253ed1564a66f1319e1ec3c4e15dd5f7734a0a0dcaf9fa823ed946cbfb51636`).
- The 16 by 768 MiB long workload was byte-identical in every completed run
  (`5cf35e54dba2219ec480e80f29d9052c103af2295376026304de04a7ab6aba95`).
- `git diff --check` passed; all 271 modified resolved `network_attribute.txt` snapshots remove the
  legacy attribute and contain both new default-false attributes.
- Final source review corrected policy independence: planned TP destroy now submits cleanup for a
  reusable Jetty, while the Jetty itself remains intact.
- Stable-commit review restored the original direct `ReserveTpnsForTraffic()` implementation for
  policy-off startup, so identity vector construction and generation lookup remain opt-in costs.

## Final acceptance evidence

- MPI-only (`--mtp-threads=1`) and hybrid (`--mtp-threads=2`) both completed with exit status 0.
  Both ranks reported `PASSED`, including ACK, task completion, stale request rejection, explicit
  connection close, and final sender/receiver view retirement.
- OpenMPI per-rank output was captured separately for Ticket 05 baseline and Ticket 06. The raw bytes
  matched for both MPI-only and hybrid: rank 0 SHA-256
  `609529d52bed74d81273e1bea421525f4263341084a6371fe4e6901a0c431dcb`; rank 1 SHA-256
  `54495d57bc6e3e74fb6ff599aba9edb84eec0817d5570668a55b5a2c08b110b6`.
- The valid performance comparison built both revisions from the same module example source with
  identical release, MPI, MTP, examples, tests, and native-optimization settings. Two reversed-order
  16 by 768 MiB pairs measured Ticket 06 minus Ticket 05 retired instructions at `-0.0057%` and
  `+0.0119%` (median `+0.0031%`), establishing no measurable default-off regression. Median simulator
  wall time was `-1.04%` and median peak RSS was `-0.57%`; these remain supporting metrics.
- Canonical identity output was exact across all four valid performance runs. Evidence is stored at
  `/tmp/ticket06-perf-evidence/20260730T040245Z-measure/summary.json`.
- An earlier run that compared the scratch executable without MPI to the module example linked with
  MPI produced an invalid `+56%` RSS difference. That run is retained for diagnosis but excluded from
  acceptance because its executable and linkage boundary differed.
- A final same-build comparison measured the complete `next-speedup` result at `67f981f0` against
  pre-Ticket-05 commit `82b22954`, where per-packet lifecycle bookkeeping was still active. The two
  reversed-order pairs reduced retired instructions by `3.8904%` and `3.8994%` (median `3.8949%`),
  cycles by a median `6.25%`, and simulator wall time by a median `6.04%`. Canonical identity output
  remained exact in all four runs. Evidence is stored at
  `/tmp/ticket06-perf-evidence/20260730T065104Z-measure/summary.json`.
- Ticket 06 therefore adds opt-in explicit cleanup policy without giving back the packet-bookkeeping
  removal gain when the policy is disabled.
- The final implementation is committed on `next-speedup` as `67f981f0`; target-worktree rebuild,
  focused suites, MPI-only, and hybrid were rerun successfully after migration.
