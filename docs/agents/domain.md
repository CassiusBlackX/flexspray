# Domain Docs

This is a single-context repository.

Before architecture, implementation, diagnosis, or review work:

- Read the root `CONTEXT.md` and use its canonical UnifiedBus, ns-3-UB, and OpenUSim vocabulary.
- Read relevant ADRs under `docs/adr/`.
- Treat the UB Base Specification as the canonical source for protocol terminology and keep simulator-local behavior explicitly bounded.
- Surface any ADR conflict instead of silently overriding it.
