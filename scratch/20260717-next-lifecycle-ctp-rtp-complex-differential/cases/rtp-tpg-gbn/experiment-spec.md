# Experiment Spec: rtp-tpg-gbn

## Planning Mode

- `experiment-group`

## Goal

- Compare TPG member pinning when task 0 retransmits while task 1 is runnable.

## Fixed Controls

- Paired case-input bytes, local-single release runtime, `rng-run=1`, routing, flow control, and observability.

## Prediction

- `next` and lifecycle preserve task outcome and protocol-observable behavior; any byte difference requires exact evidence.

## Artifact Contract

- Inputs: CSV files and full `network_attribute.txt`; outputs: stdout/stderr, output/, runlog/, pair summary.
