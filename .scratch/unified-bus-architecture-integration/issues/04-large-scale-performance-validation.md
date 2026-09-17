# Large-scale performance validation

状态: 已完成
阻塞于: 03

## Required evidence

Re-measure the integrated HEAD at 1024-host/NPU scale. Capture route preparation/install,
RSS and live storage, event count, `Simulator::Run()` wall/CPU, canonical outputs, and the
cost of TP reclaim/rehydration. Component-branch measurements are historical controls, not
substitutes for these results.

## Result

The final retained PR candidate was paired against current `main` on the representative
1024-device/64-switch case with 6,144 traffic records, 2,048 topology records, and 6,078 route
records. Each side completed two measured runs plus a canonical canary.

- retired instructions: `+7.862581%`
- `Simulator::Run()`: `+18.598110%`
- wall time: `+18.560432%`
- cycles: `+18.524308%`
- peak RSS: `-43.682422%`
- canonical identity: 12,288 events per side, identical identity multiset
- timestamp boundary: 299 changed timestamps, all within `-2 ns` to `+3 ns`, matching the accepted
  same-time ordering boundary

The merge gate is measurement and explicit disclosure, not equality with `main`. The memory result
is substantially better; the remaining CPU/runtime cost is a documented non-blocking follow-up.
Normalized evidence is in
`.scratch/allocator-event-ownership-fastpath/result-matrix.yaml`.
