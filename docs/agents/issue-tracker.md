# Issue Tracker: Local Markdown

Engineering specifications and tickets for this repository are local Markdown artifacts. Do not call GitHub or GitLab issue APIs; the repository remote is GitCode and the installed engineering workflow has no native GitCode tracker adapter.

## Conventions

- One feature per directory under `.scratch/<feature-slug>/`.
- The published specification is `.scratch/<feature-slug>/PRD.md`.
- Triage state is recorded with a `Status:` line near the top of the artifact.
- Comments and later decisions are appended under a `## Comments` heading when needed.
- The installed ticket-breakdown workflow publishes an approved aggregate `tickets.md` at the repository root, in dependency order with explicit `Blocked by` edges.

## Operations

- To publish a specification, create or update the feature's `PRD.md`.
- To fetch a specification, read the referenced local Markdown file in full.
- To publish tickets, wait for the required user approval, then write the aggregate `tickets.md`.
