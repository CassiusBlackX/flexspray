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

**Jetty lifecycle authority**:
The upper-layer URMA client that decides whether a Jetty is created, reused, or explicitly destroyed.
_Avoid_: lifecycle coordinator, transport algorithm

**Jetty group**:
A target-side group of Jetties used to distribute requests across multiple receive queues or processing contexts.
_Avoid_: TPG

**JFS / JFR**:
One-sided Jetties used for sending or receiving respectively.
_Avoid_: standard Jetty

**JFC / JFCE / JFAE**:
URMA Jetties used for completion polling, completion events, and asynchronous events respectively.
_Avoid_: transport ACK path

**Transaction queue**:
The function-layer software-hardware interface used by the URMA programming model, comprising send, receive, completion, and event queues bound to Jetties or completion objects.
_Avoid_: transaction-layer context, TP queue

**Work queue element (WQE)**:
A submitted URMA work item that the simulator can lower into transaction segments.
_Avoid_: packet, TP packet

### Transaction Layer

**Transaction layer**:
The UB layer that turns upper-layer programming-model operations into transaction operations between an initiator and a target.
_Avoid_: transport layer

**Transaction operation**:
One operation identified by a transaction operation code and represented by at least one request, optionally with a response.
_Avoid_: packet

**Transaction segment**:
A TASSN-identified transaction-layer slice of a transaction operation that is completed by its corresponding transaction response.
_Avoid_: WQE segment, packet, whole WQE

**Ready transaction source**:
A local Jetty or remote-origin response source that currently has transaction work eligible for an admission attempt.
_Avoid_: queued segment copy, scheduler object, future work prediction

**RTP transport reference**:
A non-owning, generation-aware reference to either a TP channel or TPG used by an RTP transaction operation before each segment resolves one concrete TP.
_Avoid_: transport target, transport selection, network route, concrete path

**Resolved segment TP**:
The concrete TP channel selected for one transaction segment; all packets and retransmissions of that segment use this TP generation.
_Avoid_: operation-wide TP, per-packet route

**TPG member selection**:
The TPG policy decision that resolves one transaction segment to one member TP channel when its RTP transport reference contains a TPG reference.
_Avoid_: direct-TP admission, Jetty scheduling, per-packet routing

**Eligible TPG member**:
A member TP channel whose current generation can atomically admit the candidate transaction segment under its lifecycle, window, and capacity constraints.
_Avoid_: merely active TP, permanently preferred TP

**TP selection metrics**:
The bounded read-only snapshot of aggregate TP channel load and send capacity available to a TPG member-selection policy.
_Avoid_: TP internal state, queue container view, congestion-control object

**TPG member weight**:
The configured share of one TP channel within the selection policy of the TPG-member binding that contains it.
_Avoid_: TP channel capacity, direct-traffic weight, global TP weight

**Retiring TPG member**:
A TPG-member binding already excluded from new selection while its synchronous in-progress selection reference is being released.
_Avoid_: draining TP channel, migrated segment, destroyed TP

**Empty TPG**:
An active TPG with no member bindings and no admitted operation still holding a TPG-use lease.
_Avoid_: deferred TPG, destroyed TPG, memberless TPG with active work

**TP admission**:
The acceptance of a transaction segment by its already resolved TP channel subject to channel capacity and backpressure.
_Avoid_: TPG member selection, second-stage TP selection

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

**TP reservation owner**:
The node that authoritatively manages one TP reservation generation while both endpoints retain local lookup views.
_Avoid_: process-global TP owner, two authoritative endpoint copies

**Drain-only TP endpoint**:
A TP endpoint materialized or retained during connection draining solely to finish packets and protocol work from the closing reservation generation.
_Avoid_: reopened TP channel, new TP generation

**TP endpoint slot**:
One of the two lightweight endpoint positions fixed by a TP reservation, whether or not its runtime TP endpoint is materialized.
_Avoid_: TP endpoint object, dynamically added connection child

**TP backing compaction**:
A simulator-local release of reconstructible TP endpoint backing while the TP reservation, TPN, endpoint identity, and current generation remain valid.
_Avoid_: TP teardown, logical closure, endpoint destroy, paging

**Compact TP endpoint**:
A logically present TP endpoint whose live backing has been replaced by the continuity state needed to restore that same endpoint generation. The implementation may call this state `DORMANT`; it is not a protocol state and does not mean that the endpoint was paged to disk.
_Avoid_: idle TP, missing TP, closed TP, new TP generation

**Live TP backing observation**:
A non-mutating query of the currently materialized TP channel object. It may return no object for a compact TP endpoint even though the logical endpoint still exists.
_Avoid_: TP existence query, implicit backing restoration

**TP backing resolution**:
The owner-local operation that returns live backing for an exact endpoint identity, restoring compact backing when necessary without changing its TPN, reservation, or generation.
_Avoid_: endpoint creation, session restart, remote object borrowing

**Congestion-control backing compaction contract**:
The algorithm-owned eligibility, snapshot, restore, idle-reset, and event-drain rules that permit congestion-control state to participate in TP backing compaction without changing later control decisions. Implementation APIs may use `Dormancy` in their names.
_Avoid_: congestion-control teardown, implicit reset, generic inactivity

**TP endpoint destroy**:
An explicit simulator-local management action that ends one local TP endpoint generation after its accepted work and packets drain, while its parent reservation may remain valid.
_Avoid_: TP backing compaction, connection teardown, automatic cleanup

**TP connection teardown**:
An explicit simulator-local management action that ends a TP reservation and session generation only after both endpoint slots and all old protocol work drain.
_Avoid_: TP backing compaction, task completion, idle timeout

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

**CTP entity key**:
The node-local value key identifying one source Entity, destination node and Entity, and VL communication domain inside the node-scoped CTP service; the source node is implicit in the owner.
_Avoid_: CTP channel reference, CTP generation, lifecycle owner

**CTP congestion key**:
The node-local key identifying one destination node, destination Entity, and VL congestion-control aggregation domain.
_Avoid_: CTP entity key, source-specific congestion context, RTP CC context

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

**CFG7 LDST extension**:
A simulator-local Load/Store TP bypass packet format identified by LPH.CFG=7 and CNA24 NTH.NLP=4. It retains the compact transaction request, memory-access extension, and response headers of the CFG9 LDST format; the cited UB Base Specifications do not define this extension.
_Avoid_: standard CFG7 TP bypass, CTP, full transaction-header format

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

**Modeled resource lifecycle**:
The simulated creation, active use, draining, and closure of a UB resource as observable network or management behavior.
_Avoid_: memory cleanup, garbage collection

**Logical closure**:
The modeled end of a resource generation, after which that identity cannot accept new work and later use requires an explicitly modeled reacquisition or creation.
_Avoid_: C++ destructor, storage reclamation

**Lifecycle-managed child resource**:
A finite packet, work, event, binding, queue entry, or retained protocol record whose terminal condition is tracked independently from its parent instance.
_Avoid_: parent object lifetime, future workload use

**Layer-owned lifecycle accounting**:
The rule that each protocol layer records the finite resources it creates and owns, while cross-layer lifecycle coordination validates relationships without duplicating those records.
_Avoid_: one global ledger owning all protocol state, multi-layer counting of one packet

**Storage policy**:
The allocation and capacity-retention choice for a container after its logical elements terminate; it does not define protocol lifetime.
_Avoid_: lifecycle policy, connection teardown

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

**MegaMoE workload model**:
A simulator-local workload model that represents token-to-expert routing, variable-size dispatch
and combine transfers, token-block computation, and expert-wave scheduling over a UB system. Its
scheduling parameters are explicit model inputs; they are not claims about one vendor's native
kernel implementation.
_Avoid_: Ascend MegaMoE implementation, DeepGEMM replica, model layer

**Token block**:
The simulator-local compute unit formed by dividing one source rank's input tokens into bounded
groups before their routed assignments are grouped by expert owner and wave. A token block is not a
Transformer model layer and is not an internal GEMM tile.
_Avoid_: model block, compute tile, arbitrary repeated stage

**Expert wave**:
A simulator-local scheduling group of local experts. Within one owner-local token block, the next
wave's L1 waits for the previous wave's L2; independent blocks can still compete for the bounded
UBPU compute resource.
_Avoid_: token batch, model layer, network wave

**Compute resource**:
The bounded processing capacity of one simulated UBPU. A compute task can run only after it acquires
this resource; otherwise it waits while independent UB communication can continue.
_Avoid_: delay-only task, unlimited parallel compute, UB link capacity

**Task**:
One application-level transfer or operation in a traffic workload.
_Avoid_: packet, flow unless the discussion is explicitly flow-level

**Local task completion (`LOCAL_COMPLETE`)**:
The owner-side observation that the task's final transport work has completed. Transport latency and FCT evidence use this point; it does not by itself publish workload completion to other execution owners.
_Avoid_: globally visible completion, dependency release

**Visible task completion (`COMPLETE_VISIBLE`)**:
The committed workload-state transition after which completion is canonically recorded and dependent work may be released. Task-scoped cleanup is scheduled only after this transition. It may share a simulation timestamp with local task completion, but it is ordered after it.
_Avoid_: completion notification submission, all-worker barrier, transport FCT

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

**Collective operation**:
One invocation of a workload primitive in which a communication group participates as logical ranks.
_Avoid_: one transfer, one phase, collective algorithm

**Collective data size**:
The complete logical data object processed by one collective operation before its algorithm divides the data into blocks and phases.
_Avoid_: one transfer size, one phase size, accumulated network traffic

**Communication group**:
The physical simulation nodes that participate as ranks in one collective operation.
_Avoid_: UB domain, topology group

**Rank placement**:
The explicit association between a communication group rank and a physical simulation node ID.
_Avoid_: rank mapping, topology role

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
One independently owned lookup at a current node for a destination node and destination UB port. One routing representation owns the whole item; candidate port sets from different representations are not merged.
_Avoid_: RoutingKey, destination node alone, merged route

**Full routing table**:
A generated or manually supplied route representation that materializes candidate output ports for every route lookup in scope. Existing CSV routing input is the compatibility form of this representation.
_Avoid_: compressed routing table, structured topology and routing

**Compressed routing table**:
A runtime route representation that interns repeated candidate port sets and stores compact identifiers for materialized route lookups. It preserves full-table forwarding semantics but does not by itself avoid generating every route lookup.
_Avoid_: topology compression, structure rule, packed index as a user-facing name

**Structured topology and routing**:
A route representation that preserves verified topology rules and derives candidate output ports without materializing every route lookup. It is the memory-priority runtime form of structured route generation.
_Avoid_: compressed routing table, topology label, rule routing as a user-facing name

**Topology rule**:
A compact runtime rule that maps physical nodes to verified coordinates or switch groups and derives legal next-hop ports from the rule plus authoritative physical links. It does not contain one record per destination lookup.
_Avoid_: expanded route table, topology label, unverified graph pattern

**Mixed routing**:
A route-generation strategy that uses structured topology and routing for verified route lookups and materialized table entries for the rest, with exactly one source responsible for each lookup. It is not a fourth runtime route representation.
_Avoid_: hybrid candidate merging, combining two candidate sets, mixed runtime backend

**Route generation**:
The offline method used to produce route candidates: `full` materializes every lookup, `structured` requires complete verified rule coverage, and `mixed` uses verified rules plus materialized entries for uncovered lookups.
_Avoid_: runtime routing mode, candidate selection algorithm

**Route runtime mode**:
The runtime method used to obtain a complete candidate set. `compressed-table-lookup` reads a
precomputed compressed route table; `structured-rule-compute` evaluates verified structure rules
and consults a compact supplemental table only for lookup items not owned by rules.
_Avoid_: route generation, HASH, round-robin, adaptive routing

**Compressed routing artifact**:
A versioned, little-endian `compressed_routing.bin` file prepared by `ns-3-ub-tools`. It contains
interned shortest/non-shortest candidate classes and packed lookup IDs, and is bound to the prepared
topology by a content fingerprint and payload checksum.
_Avoid_: C++ struct dump, CSV cache, structured topology input

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

**Observability category**:
A protocol or simulator concern whose runtime evidence can be enabled and detailed independently from other concerns.
_Avoid_: log component, output file

**Observability level**:
The amount of runtime evidence retained for one observability category, ranging from no evidence through summary and event records to detailed history.
_Avoid_: diagnostic strength, protocol behavior

**Trace source**:
A producer of typed runtime events that is independent from whether and how those events are retained.
_Avoid_: trace file, correctness ledger

**Trace sink**:
A consumer that filters, aggregates, or records events published by a trace source.
_Avoid_: protocol owner, lifecycle manager

**Identity-coherent trace sampling**:
A trace-volume reduction rule that selects complete resource generations or packet lifetime identities and retains every event for each selected identity.
_Avoid_: independent event sampling, incomplete lifecycle chain

**Lifecycle trace record**:
A versioned runtime evidence row that identifies one resource generation, one lifecycle event, and any affected child identity without becoming the protocol's correctness ledger.
_Avoid_: free-form log line, lifecycle state, packet header

**Trace schema metadata**:
A run-level description of a trace schema and its numeric enum mappings, emitted from the same definitions used by the simulator so parsers do not maintain a second semantic table.
_Avoid_: per-record metadata, parser guess, protocol configuration

**Diagnostic level**:
The amount of runtime consistency verification requested for a simulation, independently from trace detail.
_Avoid_: observability level, logging severity

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
