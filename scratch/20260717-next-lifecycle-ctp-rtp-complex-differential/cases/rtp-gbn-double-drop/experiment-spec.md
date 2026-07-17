# Experiment Spec: rtp-gbn-double-drop

## Planning Mode

- `experiment-group`

## Goal

- Compare GBN recovery after deterministic one-time drops of PSN 1 and PSN 3.

## Fixed Controls

- Paired case-input bytes, local-single release runtime, `rng-run=1`, routing, flow control, and observability.

## Prediction

- `next` and lifecycle preserve task outcome and protocol-observable behavior; any byte difference requires exact evidence.

## Artifact Contract

- Inputs: CSV files and full `network_attribute.txt`; outputs: stdout/stderr, output/, runlog/, pair summary.
