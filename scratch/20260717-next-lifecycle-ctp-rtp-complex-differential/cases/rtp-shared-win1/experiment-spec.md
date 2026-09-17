# Experiment Spec: rtp-shared-win1

## Planning Mode

- `experiment-group`

## Goal

- Compare shared-TP ACK wake and fairness with a one-packet transport window.

## Fixed Controls

- Paired case-input bytes, local-single release runtime, `rng-run=1`, routing, flow control, and observability.

## Prediction

- `next` and lifecycle preserve task outcome and protocol-observable behavior; any byte difference requires exact evidence.

## Artifact Contract

- Inputs: CSV files and full `network_attribute.txt`; outputs: stdout/stderr, output/, runlog/, pair summary.
