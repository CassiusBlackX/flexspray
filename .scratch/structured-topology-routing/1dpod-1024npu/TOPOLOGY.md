# 8-plane 1024-NPU topology

Generated files:

- `node.csv`
- `topology.csv`

Counts:

- NPU hosts: 1024
- Planes: 8
- Pods: 16
- L1 switches: 1024
- L2 switches: 256
- 5808 switches: 32
- Total nodes: 2336
- Physical links: 19968

Node ID ranges:

- NPU: 0..1023
- L1: 1024..2047
- L2: 2048..2303
- 5808: 2304..2335

Port model:

- NPU: 15 ports. Ports 0..7 go to the eight planes; ports 8..14 form the
  full mesh inside the local 8-NPU group.
- L1: ports 0..7 down to NPUs, 8..11 to L2, 12..15 to 5808.
- L2: 16 ports, two links from each of 8 L1 in one pod/plane.
- 5808: 128 ports, one link from each L1 in one plane.

5808 interpretation:

- Per plane, four physical 5808 switches form two logical pairs.
- Each L1 connects to both logical pairs.
- Equivalently, each L1 reaches all four physical 5808 switches with one 400G port each.

Routing note:

- This script intentionally does not emit full `routing_table.csv`.
- For 1024 hosts, all-pairs route generation can be large.
- Generate route entries from the actual workload or restore `scratch/ns-3-ub-tools`
  and use its route generator when full all-pairs routing is really needed.
