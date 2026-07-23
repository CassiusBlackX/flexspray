# Large-scale performance validation

状态: 待评估
阻塞于: 03

## Required evidence

Re-measure the integrated HEAD at 1024-host/NPU scale. Capture route preparation/install,
RSS and live storage, event count, `Simulator::Run()` wall/CPU, canonical outputs, and the
cost of TP reclaim/rehydration. Component-branch measurements are historical controls, not
substitutes for these results.
