# Experiment Spec: ctp-win128

## Planning Mode

- `experiment-group`

## Goal

- Compare four concurrent same-Entity/VL CTP WRITEs with Jetty segment window 128.

## Fixed Controls

- Paired case-input bytes, local-single release runtime, `rng-run=1`, routing, flow control, and observability.

## Prediction

- `next` and lifecycle preserve task outcome and protocol-observable behavior; any byte difference requires exact evidence.

## Artifact Contract

- Inputs: CSV files and full `network_attribute.txt`; outputs: stdout/stderr, output/, runlog/, pair summary.
