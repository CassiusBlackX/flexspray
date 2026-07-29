# 06 — Expose opt-in destroy policy and complete compatibility and performance acceptance

**What to build:** Expose narrow, default-off standard-driver controls for task-scoped Jetty and
planned driver-owned TP connection destroy, then complete the feature-wide byte-level correctness
and performance acceptance package.

**Blocked by:** 02 — Complete explicit TP connection close across execution owners; 05 — Remove TP
dormancy and obsolete global lifecycle machinery.

**Status:** ready-for-agent

- [ ] `EnableTaskScopedJettyDestroy` and `EnablePlannedTpConnectionDestroy` are independent Boolean
      controls and both default to false.
- [ ] Direct explicit Jetty and TP connection destroy remains available regardless of policy state.
- [ ] Task-scoped Jetty policy touches only Jetties created with declared task-scoped lifetime after
      visible task completion.
- [ ] Planned TP connection policy touches only exact reservations created and owned by the standard
      driver after the last accepted planned task referencing each connection completes.
- [ ] Reusable, preconfigured, externally created, and customer-managed resources are never touched
      by the convenience policy.
- [ ] The legacy task-inference transport cleanup switch and its aborting path are removed rather than
      repurposed.
- [ ] Policy-off and policy-on canonical network outputs are byte-identical when scoped resources
      have no later declared use.
- [ ] The complete frozen local, MTP, MPI, and hybrid manifests are raw-byte-identical to baseline.
- [ ] Reversed-order approximately 30-second measurements report retired instructions as primary and
      wall time as supporting evidence; the prototype's median `3.89%` reduction is directional only.
- [ ] Throwaway prototype and microprobe source files are excluded from the production commit while
      their decision-relevant findings remain documented.
