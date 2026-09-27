# 128-host reliable rerouting acceptance

Approved single case: 128 hosts (0–127), 16 leaves (128–143), 8 spines (144–151),
152 nodes and 256 links. Eight hosts per leaf; every leaf has one link to each spine.
All links400Gbps/20ns. All16256 directed host pairs send65536B URMA_WRITE at90us,
priority7, RTP on-demand, ordinary and fast TP retransmission disabled.

Permanent bidirectional leaf0–spine0 fault: node128 port8, initially100us; detection5us.
Only fault timing may be calibrated if a first run lacks nonzero retry at either endpoint.
LastAck VL/SL1, three copies, receive timeout1s, send deadline500ms.
CBFC, CC disabled, reserve8MiB/queue, shared/headroom0. Runtime-catalog snapshot.
Routing PER_FLOW_SHORTEST_PATHS with INGRESS_PORT_STRIPE and explicit failure-only detour.
Use CSV representation to retain explicit candidate/metric provenance and preparation validation.
Necessary switch destinations and fault detours augment existing tool-generated host routes.

Success requires actual six-source LastAck sends, valid bidirectional multihop reception,
causal recovery, unique ownership transfer, no outstanding processing at first drain,
all tasks and1065353216B complete, no ordinary drop or TP retransmission.
Raw artifacts remain isolated in this case; failures preserved in the local ticket ledger.
Execution verified: all 16,256 tasks complete; last completion at 417.783760 us.
At 105 us detection, leaf128:8 has 116 retained attempts and spine144:0 has 117.
First valid LastAck arrives at 105.164520 us and 105.225480 us respectively;
233 recovery transfers complete locally at those times. Six copies each traverse three real hops.
Runtime packet drops: 0. The initial 100 us fault time needed no calibration.
Macro checker and all 17 negative mutations pass. Exact commands are in generate_case.py and README.
The first run completed business traffic but lacked state trace bindings; its raw artifacts remain
in the local ticket ledger as failed evidence, followed by the verified rerun with identical inputs.
