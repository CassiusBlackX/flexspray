# OpenUSim / ns-3-UB Context

This glossary defines the project language for OpenUSim work in this repository. It keeps UB protocol terms, simulator terms, simulation evidence terms, and repo-local workflow terms distinct.

## Source Boundary

**UB Base Specification**:
The canonical protocol terminology source for UnifiedBus. Use it to resolve UB terms, but do not copy the full specification into this glossary.
_Avoid_: repo implementation as the spec, pasted spec excerpt

**Spec-backed term**:
A term whose meaning comes from the UB Base Specification and should stay aligned with that document unless the simulator explicitly declares a narrower modeling boundary.
_Avoid_: implementation nickname

**Simulator-local term**:
A term used by ns-3-UB or OpenUSim to describe case files, generated artifacts, helper behavior, or a modeled subset of the UB protocol.
_Avoid_: normative UB term

## Language

### UB System And Roles

**UnifiedBus (UB)**:
The interconnect technology and protocol stack modeled by this repository, spanning function, transaction, transport, network, data-link, and physical-layer concerns.
_Avoid_: bus simulator, UB network only

**UB system**:
A computing system powered by UB, from a single server-scale deployment to a SuperPoD-scale deployment.
_Avoid_: simulator cluster

**UB domain**:
A collection of UBPUs interconnected using UB links.
_Avoid_: simulation case, topology

**UB Fabric**:
The collection of UB switches and UB links inside a UB domain.
_Avoid_: whole system, workload

**UB port**:
The physical and data-link endpoint on a UBPU that connects to a peer UB port through a UB link.
_Avoid_: UDP port, transport endpoint

**UB link**:
A full-duplex point-to-point connection between two UB ports.
_Avoid_: route, transport channel

**UB processing unit (UBPU)**:
A processing unit that supports the UB protocol stack and implements device-specific functions.
_Avoid_: host unless the discussion is simulator node placement

**Entity**:
The basic communication object inside a UB domain and the unit by which a device allocates its own resources.
_Avoid_: node, process

**Entity identifier (EID)**:
The identifier assigned to an Entity inside a UB domain.
_Avoid_: node id, IP address

**Initiator**:
The Entity that originates a transaction request.
_Avoid_: source node unless the discussion is case files

**Target**:
The Entity that receives and processes a transaction request.
_Avoid_: destination node unless the discussion is case files

**Home**:
The memory owner in the Home-User access model.
_Avoid_: receiver

**User**:
The Entity that accesses memory in the Home-User access model.
_Avoid_: sender

**Memory segment**:
A contiguous virtual or logical address range that is the basic object of memory transaction operations.
_Avoid_: packet buffer, flow data

**UB memory descriptor (UBMD)**:
The descriptor used to identify and authorize access to a Home memory segment.
_Avoid_: address only, token only

**UB memory management unit (UMMU)**:
The UB component that translates a UBMD into a Home physical address and performs permission validation.
_Avoid_: OS MMU

### Function Layer

**Function layer**:
The UB layer that provides programming models and higher-level functions above transaction operations.
_Avoid_: application layer in the simulator

**Load/Store programming model**:
The synchronous function-layer model where processor load/store instructions are converted into UB transaction operations.
_Avoid_: memory traffic generator

**Unified remote memory access (URMA)**:
The asynchronous function-layer programming model for remote memory access and two-sided message communication.
_Avoid_: RDMA synonym unless the discussion is explicitly comparative

**Jetty**:
The basic URMA communication unit used to issue and execute asynchronous transaction operations.
_Avoid_: queue, flow

**Jetty group**:
A target-side group of Jetties used to distribute requests across multiple receive queues or processing contexts.
_Avoid_: TPG

**JFS / JFR**:
One-sided Jetties used for sending or receiving respectively.
_Avoid_: standard Jetty

**JFC / JFCE / JFAE**:
URMA Jetties used for completion polling, completion events, and asynchronous events respectively.
_Avoid_: transport ACK path

**Work queue element (WQE)**:
A submitted URMA work item that the simulator can split into transaction-layer WQE segments.
_Avoid_: packet, TP packet

**WQE segment**:
The simulator's transaction-sized slice of a WQE.
_Avoid_: whole task, whole WQE

### Transaction Layer

**Transaction layer**:
The UB layer that turns upper-layer programming-model operations into transaction operations between an initiator and a target.
_Avoid_: transport layer

**Transaction operation**:
One operation identified by a transaction operation code and represented by at least one request, optionally with a response.
_Avoid_: packet

**Transaction operation code (TAOpcode)**:
The transaction-layer operation code for memory, message, maintenance, or management operations.
_Avoid_: traffic opType unless discussing case input mapping

**Memory transaction**:
A transaction type used for memory operations such as write, read, and atomic operations.
_Avoid_: MEM_STORE only

**Message transaction**:
A transaction type used for two-sided message communication.
_Avoid_: URMA write

**Transaction service mode**:
The reliability and ordering mode used by a transaction operation.
_Avoid_: transport mode

**Reliable and ordered by initiator (ROI)**:
A reliable transaction service mode where ordering is maintained by the initiator.
_Avoid_: reliable transport

**Reliable and ordered by target (ROT)**:
A reliable transaction service mode where ordering is maintained by the target.
_Avoid_: receiver ordering

**Reliable and ordered by lower layer (ROL)**:
A reliable transaction service mode where ordering is maintained by lower-layer protocol capabilities.
_Avoid_: transport mode

**Unreliable and non-ordered (UNO)**:
A transaction service mode with no reliability or ordering guarantee.
_Avoid_: UTP

**Transaction execution order (TEO)**:
The order in which transactions execute at the target.
_Avoid_: completion order

**Transaction completion order (TCO)**:
The order in which completion notifications are generated.
_Avoid_: execution order

**Basic transaction header (BTAH)**:
The base transaction request header that carries the transaction opcode and common transaction fields.
_Avoid_: TAH when the compact/full distinction matters

**Transaction header (TAH)**:
The transaction-layer header carried after transport or network-layer headers.
_Avoid_: transport header

**Memory access extended transaction header (MAETAH)**:
The transaction extension header that carries memory-access address, token, and length information.
_Avoid_: payload metadata

**Acknowledge transaction header (ATAH)**:
The transaction-layer acknowledgement or response header.
_Avoid_: TPACK

### Transport Layer

**Transport layer**:
The UB layer that provides end-to-end reliable or unreliable transmission services to the transaction layer.
_Avoid_: network routing layer

**Transport endpoint (TPEP)**:
The logical endpoint that sends or receives transport-layer packets.
_Avoid_: port

**Transport channel (TP channel)**:
An end-to-end connection between two transport endpoints.
_Avoid_: transport path

**Transport channel group (TPG)**:
A group of TP channels used for load balancing transaction packets.
_Avoid_: Jetty group, route group

**Transport packet (TP Packet)**:
A transport-layer data packet carrying transaction-layer content.
_Avoid_: WQE, transaction operation

**TP number (TPN)**:
The numeric identifier carried in transport headers and used by ns-3-UB case files to identify TP channels.
_Avoid_: route id

**TP channel key**:
The stable field set used by the simulator to identify a TP channel endpoint, including the communicating nodes, concrete ports, priority, and selected path identity.
_Avoid_: random TPN, first-touch TPN

**TP reservation**:
The pre-traffic resource preparation step that assigns TPNs to TP channel keys before packets are released.
_Avoid_: packet-time TP negotiation

**Reserved TPN**:
A TPN already assigned to a TP channel key for the local node, even if the local TP channel object has not been created yet.
_Avoid_: planned TPN, missing TP

**Invalid TPN**:
A TPN observed at a node that cannot be validated against the node's TP channel keys. Invalid TPNs are simulator errors, not prompts to create TP channels.
_Avoid_: unknown TPN, lazy TP

**Reliable transport (RTP)**:
The transport mode that provides end-to-end reliable, duplication-free service.
_Avoid_: ROI

**Compact transport (CTP)**:
The transport mode that relies on lower protocol layers for reliability and offers coarser congestion management.
_Avoid_: compressed RTP

**Unreliable transport (UTP)**:
The transport mode that provides unreliable, connectionless service.
_Avoid_: UNO

**TP bypass**:
The mode where the transaction layer accesses network-layer services without transport-layer functionality.
_Avoid_: missing transport channel

**Packet sequence number (PSN)**:
The per-TP-channel sequence number used by reliable transport to detect loss, duplication, and ordering.
_Avoid_: task id, packet uid

**Transport acknowledgement (TPACK)**:
A positive RTP response packet indicating successful reception of transport data.
_Avoid_: transaction ACK

**Transport negative acknowledgement (TPNAK)**:
An RTP response packet indicating a transport receive error or missing expected packet.
_Avoid_: failed task

**Transport selective acknowledgement (TPSACK)**:
An RTP response packet that reports selective receive status for PSN ranges.
_Avoid_: parser summary

**Congestion notification packet (CNP)**:
A transport response packet used to carry congestion notification toward the sender.
_Avoid_: packet loss signal

**Congestion extended transport header (CETPH)**:
The transport extension header that carries congestion feedback fields.
_Avoid_: ECN mark

**Selective acknowledge extended transport header (SAETPH)**:
The transport extension header that carries selective acknowledgement bitmap state.
_Avoid_: retransmission log

**CNP CETPH**:
The CNP-specific congestion extended transport header format used by CNP packets; code may refer to this as a CNP extended transport header.
_Avoid_: SAETPH

### Network And Data Link

**Network layer**:
The UB layer that provides addressing, routing, multipath load balancing, QoS mapping, congestion marking, and isolation services.
_Avoid_: topology file

**Network header (NTH)**:
The UB network-layer header that identifies addressing, routing, service level, and next-layer protocol information.
_Avoid_: IP header

**Compact network address (CNA)**:
A shortened UB network address format, with 16-bit and 24-bit variants.
_Avoid_: IP address

**Source CNA (SCNA)**:
The source compact network address in a CNA-based network header.
_Avoid_: source node id

**Destination CNA (DCNA)**:
The destination compact network address in a CNA-based network header.
_Avoid_: destination node id

**Port network address**:
A UB network address assigned to a UB Controller port. The address may be represented by a compact network address or by an IP address format network header.
_Avoid_: UDP port

**Primary CNA**:
A compact network address assigned to a UB Controller as a whole.
_Avoid_: port address

**Port CNA**:
A compact network address assigned to a UB Controller port.
_Avoid_: UDP port, queue id

**IP address format network header**:
A UB network-header format that carries a standard IP packet after UB network-layer fields.
_Avoid_: Port CNA

**Service level (SL)**:
The network-layer priority value that maps onto virtual lanes.
_Avoid_: VL unless the mapping is already applied

**Virtual lane (VL)**:
A data-link lane used for traffic isolation, QoS scheduling, and per-lane credit flow control.
_Avoid_: queue

**Load balance factor (LBF)**:
The network-header field used as a multipath selection factor.
_Avoid_: load balance mode

**Link packet header (LPH)**:
The data-link packet header used to carry link-level packet metadata.
_Avoid_: NTH

**Data link layer control block (DLLCB)**:
A data-link control block used for link management and control.
_Avoid_: data packet

**Data link layer data packet (DLLDP)**:
A data-link data packet used to carry upper-layer payload over a UB link.
_Avoid_: transaction packet

**Cell**:
The basic credit accounting unit for credit-based flow control.
_Avoid_: flit

**Flit**:
The fixed-length data-link unit of transfer and physical-layer interface.
_Avoid_: packet

**Credit-based flow control**:
Receiver-driven credit return that limits how much traffic a sender can inject per VL.
_Avoid_: congestion control

**Congestion control**:
The mechanism that reacts to congestion signals and adjusts sender behavior.
_Avoid_: flow control

**CAQM**:
Congestion Aware Queue Management, the UB congestion-control mechanism modeled by the simulator.
_Avoid_: DCQCN

**DCQCN**:
An RDMA congestion-control algorithm implemented by the simulator for comparison and reproduction work.
_Avoid_: UB default unless the case explicitly enables it

### Simulator Domain

**OpenUSim**:
The user-facing simulation workflow built around this repository's UnifiedBus ns-3 module and its case-generation tools.
_Avoid_: openusim skill system, helper workflow

**ns-3-UB**:
The ns-3 based simulator implementation of the UnifiedBus protocol framework in this repository.
_Avoid_: OpenUSim core, UB toolchain

**Reference implementation**:
A replaceable simulator policy or algorithm supplied by this repository where the UB Base Specification does not prescribe one exact behavior.
_Avoid_: normative UB behavior

**Simulation boundary**:
The set of claims that this simulator can support with its modeled protocol behavior and emitted evidence.
_Avoid_: hardware truth, real cluster measurement

**Protocol layer**:
One of the UB modeling layers: function, transaction, transport, network, data link, or physical.
_Avoid_: stack module

**Function-layer operation**:
An application-facing operation such as Load/Store or URMA traffic that is later represented as simulation tasks.
_Avoid_: packet

**Routing policy**:
The rule used to choose forwarding candidates or paths for packets.
_Avoid_: topology

### Simulation Cases

**Simulation case**:
One runnable OpenUSim simulation with one topology, workload, parameter set, and observability choice.
_Avoid_: run case, single run

**Case directory**:
The directory that contains one simulation case's inputs and outputs.
_Avoid_: case root

**Case input**:
A file or declared setting consumed before the simulation starts.
_Avoid_: run output

**Network attributes**:
The case-level parameter values and global switches that configure simulator behavior before a run.
_Avoid_: config patch, parameter diff

**Node inventory**:
The list of hosts and switches in a simulation case.
_Avoid_: topology

**Topology**:
The links and connectivity rules between nodes in a simulation case.
_Avoid_: routing table

**Topology graph**:
The concrete nodes and links produced after a topology description has been expanded.
_Avoid_: topology family, routing table

**Routing table**:
The per-node forwarding information used to move packets toward destinations.
_Avoid_: topology, traffic

**Traffic workload**:
The set of application-level tasks injected into a simulation.
_Avoid_: topology, packet trace

**Task**:
One application-level transfer or operation in a traffic workload.
_Avoid_: packet, flow unless the discussion is explicitly flow-level

**Phase**:
A workload grouping that controls which tasks can run together and which tasks wait for earlier work to finish.
_Avoid_: batch

**Fault scenario**:
An intentional loss, delay, congestion, error, disconnect, or lane-reduction condition applied to a case.
_Avoid_: bug

### Topology And Workload Planning

**Topology family**:
A named planning shorthand such as ring, full mesh, two-layer Clos, or fat tree. A custom graph does not need a topology-family label.
_Avoid_: case type, required custom-graph label

**Two-layer Clos**:
A leaf-spine Clos topology parameterized by host and leaf-switch counts.
_Avoid_: fat tree unless the user gave a `k`-style fat-tree parameter

**Fat tree**:
A canonical Clos-family topology parameterized by `k`.
_Avoid_: generic leaf-spine

**Custom graph**:
A bounded topology given directly as nodes and links, without requiring a topology-family label.
_Avoid_: unsupported topology, named topology family

**Structured topology input**:
A Python-built NetworkX graph, or a JSON/YAML adapter input that builds the same graph, used as the authoritative topology for structured compilation. The routing representation is a separate preparation choice.
_Avoid_: topology-family declaration, generated CSV, separate declarative compiler

**Prepared structured case**:
A simulation directory containing `structured_topology.txt`, produced after structure verification and route compilation. The runner consumes the directory without importing NetworkX or recompiling routes.
_Avoid_: runtime Python integration, diagnostic CSV, user-maintained topology source

**Node group**:
A named collection of nodes with the same type and default physical attributes.
_Avoid_: routing group, topology family

**Group connection**:
A compact instruction that creates repeated physical connections within or between node groups.
_Avoid_: layered connection, routing rule, verified structure

**Layered connection**:
A compact instruction that connects an ordered set of node layers across one or more planes without assigning fixed topology roles to those layers.
_Avoid_: Clos label, leaf-spine role declaration, routing rule

**Explicit connection**:
One physical connection whose endpoint nodes and ports are stated individually.
_Avoid_: group connection, routing entry

**Group-local node reference**:
A node reference made from a node-group name and a zero-based index within that group.
_Avoid_: user-computed global node ID, topology role

**Connectivity rule**:
The rule that says which node groups connect and how many links are created.
_Avoid_: routing policy

**Allocation delay**:
The time consumed by one switch allocation round.
_Avoid_: forward delay, link delay

**Forward delay**:
The input-port processing time before a packet becomes eligible for switch allocation.
_Avoid_: allocation delay, link delay

**Convergence ratio**:
The ratio of downstream bandwidth to upstream bandwidth at a topology boundary.
_Avoid_: utilization

**Workload primitive**:
A communication pattern such as AllReduce, All-to-All, point-to-point, broadcast, incast, or permutation traffic.
_Avoid_: application workflow

**Custom traffic skeleton**:
A planned task-and-phase structure for a workload that cannot be mapped cleanly to a built-in generator.
_Avoid_: arbitrary CSV

**Reference traffic**:
An existing traffic definition used as the workload anchor for a new case.
_Avoid_: old case

**Operation type**:
The task-level operation semantics used by the workload, such as URMA write, URMA read, memory store, or memory load.
_Avoid_: traffic pattern

### Routing And Transport Setup

**Routing intent**:
The planning choice that captures the desired routing behavior, such as hash, adaptive, shortest-path-only, generated routes, or manual routes.
_Avoid_: topology intent

**Route lookup**:
A request for candidate output ports at the current node for a destination and routing constraints. It is separate from the final choice of one output port.
_Avoid_: port selection, path generation

**Route lookup item**:
One independently owned lookup at a current node for a destination node and destination UB port. Table routing or rule routing owns the whole item; their candidate port sets are not merged.
_Avoid_: RoutingKey, destination node alone, merged route

**Table routing**:
A route representation that stores candidate output ports for individual destinations or destination ranges.
_Avoid_: full route table when range compression is included, rule routing

**Rule routing**:
A route representation that uses verified structure, destination ranges, and local port groups to return candidate output ports.
_Avoid_: generated route table, topology detection

**Topology rule**:
A compact runtime rule that maps physical nodes to verified coordinates or switch groups and derives legal next-hop ports from the rule plus authoritative physical links. It does not contain one record per destination lookup.
_Avoid_: expanded route table, topology label, unverified graph pattern

**Mixed routing**:
A route representation that uses rules for some route lookups and table entries for the rest, with exactly one source responsible for each lookup.
_Avoid_: hybrid candidate merging, combining two candidate sets

**Verified structure**:
A compact description of repeated groups, coordinates, layers, and destination coverage that has been checked against the topology graph.
_Avoid_: guessed topology family, confidence-based match

**Pair-design network**:
A verified two-level incidence structure where each pair of lower nodes shares exactly one upper neighbor. Balanced Sparse Tree is one topology that can produce this relation; users do not declare this name in their input.
_Avoid_: required BST label, ordinary tree, unverified BIBD parameters

**Dimension network**:
A verified Cartesian-product structure in which every physical edge changes one coordinate. Each dimension uses line, ring, or full-mesh connectivity; Ring, Mesh, Torus, Hypercube, and N-dimensional Dimension FullMesh are instances of this one structure.
_Avoid_: required product topology label, two-dimensional-only full mesh

**Classic Dragonfly**:
A verified router-group structure with equal-size full-mesh groups, one logical global router pair between every pair of groups, uniform global degree, and uniform terminal attachment to one router. The name does not include Dragonfly+, random global wiring, or non-minimal routing.
_Avoid_: arbitrary grouped network, UGAL routing, Dragonfly+

**Local port group**:
The output ports on one node that are eligible under the same routing rule before hash or adaptive selection chooses one port.
_Avoid_: final output port, global port group

**Destination coverage**:
The destination nodes represented by a structured node group or reachable through a structured boundary.
_Avoid_: route table, physical connectivity alone

**Diagnostic routing export**:
An optional CSV snapshot of compiled table entries, exact/range rules, topology rules, and local port groups produced only when inspection is requested.
_Avoid_: default simulation input, topology source

**Rule coverage**:
The route lookups handled by rule routing in a simulation case.
_Avoid_: topology coverage, detected-node count

**Routing equivalence**:
The condition where two route representations return the same shortest and non-shortest candidate port sets for every route lookup in scope.
_Avoid_: graph similarity, sampled confidence

**Auto path finder**:
A route-generation approach that derives shortest-path candidates from topology connectivity. The default structured compiler emits topology rules for a complete safe structure composition; otherwise it uses one destination DAG at a time. Neither default path materializes a complete baseline table.
_Avoid_: manual routing

**Custom path finder**:
A user-provided route-generation function that derives forwarding paths from a topology graph and defines the complete baseline routes for equivalence checks and caching.
_Avoid_: topology recognizer, routing representation

**Manual route table**:
A user-specified forwarding table for cases where generated routes are not the intended behavior.
_Avoid_: topology override

**Transport-channel mode**:
The choice between precomputing transport-channel data and creating TP channels on demand.
_Avoid_: transport setup mode

**On-demand transport-channel mode**:
The transport-channel mode where TP channels are created as needed and the explicit transport-channel file may be absent.
_Avoid_: on_demand

**Precomputed transport-channel mode**:
The transport-channel mode where explicit transport-channel data is expected before the run.
_Avoid_: preconfigured transport-channel mode

### Observability And Evidence

**Observability**:
The chosen trace, debug, and parser posture for a simulation case.
_Avoid_: logging level

**Trace output**:
Raw emitted runtime evidence such as packet, task, port, flow-control, or congestion-control traces.
_Avoid_: analysis result

**Parser summary**:
A post-processed table derived from trace output.
_Avoid_: raw trace

**Throughput evidence**:
Throughput information with a declared source, such as per-port Rx/Tx evidence or per-task completion evidence.
_Avoid_: line rate proof

**Line-rate claim**:
A claim that measured throughput approaches configured port capacity.
_Avoid_: high throughput

**Evidence source**:
The artifact or code path that supports a claim.
_Avoid_: evidence criteria

**Proxy metric**:
A substitute measurement that can support part of a claim but cannot prove the whole claim by itself.
_Avoid_: direct measurement

**Negative result**:
A completed result that contradicts the prediction or shows no effect.
_Avoid_: failed experiment

**Evidence gap**:
A missing artifact, ambiguous metric, or simulator boundary mismatch that prevents a conclusion.
_Avoid_: theory gap filled by explanation

### Experiment Design

**Planning mode**:
The internal classification that decides whether the workflow is preparing one simulation case or a comparison across cases.
_Avoid_: package mode, workflow mode

**Single case**:
A planning mode for one simulation case, used for smoke runs, old-case reproduction, debugging, or one concrete topology/workload/parameter combination.
_Avoid_: one-off package, simple package

**Experiment group**:
A planning mode for a comparison across simulation cases under one claim.
_Avoid_: experiment package mode, batch run

**Control case**:
The baseline simulation case in an experiment group.
_Avoid_: baseline field

**Treatment case**:
A simulation case compared against the control case.
_Avoid_: compared configuration field

**Changed variable**:
The main variable intentionally changed between a control case and a treatment case.
_Avoid_: delta, tweak

**Fixed controls**:
The topology, workload, routing, transport-channel mode, observability, runtime path, and metric definition that must stay unchanged inside a comparison block.
_Avoid_: constants, unchanged stuff

**Prediction**:
The expected result and metric direction written before the simulation runs.
_Avoid_: expected behavior, expected result field

**Falsification signal**:
The result pattern that would weaken or reject a prediction.
_Avoid_: failure signal

**Evidence plan**:
The artifacts, metrics, and source labels needed to judge whether the prediction matched the result.
_Avoid_: proof plan

**Checkpoint policy**:
The rule that decides whether an experiment group pauses or continues after early evidence, failures, or manual gates.
_Avoid_: checkpoint behavior

**Run ledger**:
The durable execution record for an experiment group.
_Avoid_: run log, run diary

### Repo-Local Workflow

**Package root**:
The directory that contains the durable artifacts for a planning mode.
_Avoid_: experiment folder

**Handoff**:
The transfer of durable facts from one OpenUSim stage skill to the next.
_Avoid_: handover

**Case artifact generation**:
The step that writes case inputs from the approved plan.
_Avoid_: materialization

**Startup readiness**:
The known state of the current worktree, build outputs, submodules, and required tools before generation or execution.
_Avoid_: environment vibes

**Knowledge card**:
A reusable project-local note that captures a verified interpretation rule, root cause, or modeling lesson.
_Avoid_: chat summary
