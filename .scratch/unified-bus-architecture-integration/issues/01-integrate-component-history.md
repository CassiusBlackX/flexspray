# Integrate component history

状态: 已完成

## Scope

Merge structured topology/routing, allocator direct completion, and lifecycle/TP compaction
into one branch based on `origin/next`, preserving component commits.

## Result

- Component histories are retained through merge commits.
- Routing, allocator, TP manager, CMake, documentation, and tools gitlink conflicts are
  resolved without dropping either side's supported behavior.
- Dependency visibility, landing pages, theory-bound validation, and dirty files from other
  worktrees were not included.

## Verification

- `git ls-files -u` returned no entries.
- Conflict marker scan returned no integration markers.
- `git diff --check` and `git diff --cached --check` passed.
