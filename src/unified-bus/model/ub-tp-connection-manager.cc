// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/ub-tp-connection-manager.h"
#include "ns3/ub-controller.h"
#include "ns3/node-list.h"
#include "ns3/ub-utils.h"

#include <limits>

using namespace ns3;
namespace utils {

NS_LOG_COMPONENT_DEFINE("TpConnectionManager");
NS_OBJECT_ENSURE_REGISTERED(TpConnectionManager);

// Dynamic TP lifecycle operations may touch both endpoint LPs and must share one lock.
std::mutex TpConnectionManager::g_tpConnectionControlLock;

TypeId TpConnectionManager::GetTypeId()
{
    static TypeId tid = TypeId("ns3::TpConnectionManager")
        .SetParent<Object>()
        .SetGroupName("UnifiedBus")
        .AddAttribute("RemoveUselessTp",
                      "Automatically destroy idle transport channels that have completed all WQE segments.",
                      BooleanValue(false),
                      MakeBooleanAccessor(&TpConnectionManager::m_removeUselessTp),
                      MakeBooleanChecker());
    return tid;
}

TpConnectionManager::TpConnectionManager()
{
    m_random = CreateObject<UniformRandomVariable>();
    m_random->SetAttribute("Min", DoubleValue(0.0));
    m_random->SetAttribute("Max", DoubleValue(1.0));
}

TpConnectionManager::~TpConnectionManager()
{

}

// 添加连接（不需要nodeId参数）
void TpConnectionManager::AddConnection(const Connection& conn)
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    Connection canonical = conn;
    if (canonical.ownerNodeId == UINT32_MAX)
    {
        canonical.ownerNodeId = canonical.node1;
    }
    AttachConnectionIdentityLocked(canonical);
    // 存储连接
    m_allConnections.push_back(canonical);

    // 为两个节点都建立索引
    BuildIndexesForNode(canonical.node1, canonical);
    BuildIndexesForNode(canonical.node2, canonical);
}

// 添加单边连接
void TpConnectionManager::AddUnilateralConnection(const Connection& conn, const uint32_t src)
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    Connection canonical = conn;
    if (canonical.ownerNodeId == UINT32_MAX)
    {
        canonical.ownerNodeId = canonical.node1;
    }
    AttachConnectionIdentityLocked(canonical);
    // 存储连接
    m_allConnections.push_back(canonical);
    // 如果源节点既不是conn的node1也不是node2，则判错
    if (src != canonical.node1 && src != canonical.node2) {
        NS_ASSERT_MSG(0, "conn.src != src");
    }
    // 仅为源节点建立索引
    BuildIndexesForNode(src, canonical);
}

void
TpConnectionManager::AttachConnectionIdentityLocked(Connection& conn)
{
    if (m_ownerNodeId == conn.ownerNodeId)
    {
        RegisterCanonicalConnectionLocked(conn);
        return;
    }
    if (conn.reservationGeneration != 0)
    {
        return;
    }
    const uint32_t ownerTpn = conn.ownerNodeId == conn.node1 ? conn.tpn1 : conn.tpn2;
    Ptr<TpConnectionManager> ownerManager =
        NodeList::GetNode(conn.ownerNodeId)
            ->GetObject<ns3::UbController>()
            ->GetTpConnManager();
    NS_ABORT_MSG_IF(ownerManager == nullptr, "Connection reservation owner lacks a manager");
    std::lock_guard<std::mutex> ownerLock(ownerManager->m_stateLock);
    const auto owner = ownerManager->m_canonicalConnections.find({conn.ownerNodeId, ownerTpn});
    NS_ABORT_MSG_IF(owner == ownerManager->m_canonicalConnections.end(),
                    "Connection reservation view was installed before its canonical owner");
    conn.reservationGeneration = owner->second.identity.generation;
}

void
TpConnectionManager::RegisterCanonicalConnectionLocked(Connection& conn)
{
    const uint32_t ownerTpn = conn.ownerNodeId == conn.node1 ? conn.tpn1 : conn.tpn2;
    const auto key = std::make_pair(conn.ownerNodeId, ownerTpn);
    const auto existing = m_canonicalConnections.find(key);
    if (existing != m_canonicalConnections.end())
    {
        conn.reservationGeneration = existing->second.identity.generation;
        return;
    }
    NS_ABORT_MSG_IF(m_nextConnectionGeneration == std::numeric_limits<uint64_t>::max(),
                    "Connection lifecycle generation exhausted");
    ConnectionLifecycleRecord record;
    record.connection = conn;
    record.generation = ++m_nextConnectionGeneration;
    record.identity = UbLifecycleCoordinator::GetOrCreate(NodeList::GetNode(conn.ownerNodeId))
                          ->OpenResource(UbLifecycleResourceKind::RTP_CONNECTION, ownerTpn);
    conn.reservationGeneration = record.identity.generation;
    record.connection = conn;
    m_canonicalConnections.emplace(key, record);
}

UbLifecycleResourceIdentity
TpConnectionManager::GetConnectionIdentity(const Connection& connection)
{
    const uint32_t ownerTpn = connection.ownerNodeId == connection.node1 ? connection.tpn1
                                                                         : connection.tpn2;
    return {connection.ownerNodeId,
            UbLifecycleResourceKind::RTP_CONNECTION,
            ownerTpn,
            connection.reservationGeneration};
}

bool
TpConnectionManager::MatchesCanonicalConnection(const UbLifecycleResourceIdentity& identity) const
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    const auto record = m_canonicalConnections.find(
        {identity.ownerNodeId, static_cast<uint32_t>(identity.localId)});
    return identity.kind == UbLifecycleResourceKind::RTP_CONNECTION &&
           identity.ownerNodeId == m_ownerNodeId &&
           identity.localId <= std::numeric_limits<uint32_t>::max() &&
           record != m_canonicalConnections.end() && record->second.identity == identity;
}

// 获取指定节点的连接管理器视图
Ptr<TpConnectionManager> TpConnectionManager::GetConnectionManagerByNode(uint32_t nodeId)
{
    Ptr<TpConnectionManager> nodeManager = CreateObject<TpConnectionManager>();

    // 复制与该节点相关的连接
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto it = m_nodeConnections.find(nodeId);
    if (it != m_nodeConnections.end()) {
        for (const auto& conn : it->second) {
            nodeManager->AddUnilateralConnection(conn, nodeId);
        }
    }

    return nodeManager;
}

// 根据预设配置的规则选择获取tpns
std::vector<uint32_t> TpConnectionManager::GetTpns(GetTpnRuleT ruler,
                                bool useShortestPath,
                                bool useMultiPath,
                                uint32_t localNodeId,
                                uint32_t peerNodeId,
                                uint32_t localPort,
                                uint32_t peerPort,
                                uint32_t priority)
{
    std::lock_guard<std::mutex> controlLock(g_tpConnectionControlLock);
    std::vector<std::pair<uint32_t, uint32_t>> resWithMetrics;
    std::vector<uint32_t> tpns;
    uint32_t minMetrics = UINT32_MAX;
    switch (ruler) {
        case GetTpnRuleT::BY_PEERNODE:
            resWithMetrics = GetTpnsByPeerNode(localNodeId, peerNodeId);
            break;
        case GetTpnRuleT::BY_PEERNODE_PRIORITY:
            resWithMetrics = GetTpnsByPeerNodePriority(localNodeId, peerNodeId, priority);
            break;
        case GetTpnRuleT::BY_PEERNODE_LOCALPORT:
            resWithMetrics = GetTpnsByPeerNodeLocalPort(localNodeId, peerNodeId, localPort);
            break;
        case GetTpnRuleT::BY_PEERNODE_BOTHPORTS:
            resWithMetrics = GetTpnsByPeerNodeBothPorts(localNodeId, peerNodeId, localPort, peerPort);
            break;
        case GetTpnRuleT::BY_ALL:
            resWithMetrics = GetTpnsByFullCriteria(localNodeId, peerNodeId, localPort, peerPort, priority);
            break;
        default:
            break;
    }
    if (resWithMetrics.empty())
    {
        resWithMetrics =
            ReserveNewTpConnections(localNodeId, peerNodeId, priority, useShortestPath);
    }

    for (auto p : resWithMetrics) {
        minMetrics = std::min(minMetrics, p.second);
    }
    for (auto p : resWithMetrics) {
        if (!useShortestPath) {
            tpns.push_back(p.first);
        } else if (minMetrics == p.second) {
            tpns.push_back(p.first);
        }
    }
    NS_ABORT_MSG_IF(tpns.empty(), "Could not reserve a TP connection for the requested traffic");
    return ReconstructTPs(tpns, localNodeId, peerNodeId, priority, useShortestPath, useMultiPath);
}

void
TpConnectionManager::ReserveTpnsForTraffic(bool useShortestPath,
                                           uint32_t localNodeId,
                                           uint32_t peerNodeId,
                                           uint32_t priority)
{
    std::lock_guard<std::mutex> controlLock(g_tpConnectionControlLock);
    if (!GetTpnsByPeerNodePriority(localNodeId, peerNodeId, priority).empty())
    {
        return;
    }
    ReserveNewTpConnections(localNodeId, peerNodeId, priority, useShortestPath);
}

// 1. 获取节点相关的所有连接
std::vector<Connection> TpConnectionManager::GetNodeConnections(uint32_t nodeId) const
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto it = m_nodeConnections.find(nodeId);
    if (it != m_nodeConnections.end()) {
        return it->second;
    }
    return {};
}

// 2. 通过对端nodeId找到所有可用tpn
std::vector<std::pair<uint32_t, uint32_t>> TpConnectionManager::GetTpnsByPeerNode(
    uint32_t localNodeId, uint32_t peerNodeId) const
{
    std::vector<std::pair<uint32_t, uint32_t>> tpns;
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto key = std::make_pair(localNodeId, peerNodeId);
    auto it = m_peerNodeIndex.find(key);
    if (it != m_peerNodeIndex.end()) {
        for (const auto& conn : it->second) {
            // 获取本地节点对应的TPN
            if (conn.node1 == localNodeId) {
                tpns.push_back(std::make_pair(conn.tpn1, conn.metrics));
            } else if (conn.node2 == localNodeId) {
                tpns.push_back(std::make_pair(conn.tpn2, conn.metrics));
            }
        }
    }
    return tpns;
}

// 3. 通过对端nodeId+priority找到所有可用tpn
std::vector<std::pair<uint32_t, uint32_t>> TpConnectionManager::GetTpnsByPeerNodePriority(
    uint32_t localNodeId, uint32_t peerNodeId, uint32_t priority) const
{
    std::vector<std::pair<uint32_t, uint32_t>> tpns;
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto key = std::make_tuple(localNodeId, peerNodeId, priority);
    auto it = m_peerNodePriorityIndex.find(key);
    if (it != m_peerNodePriorityIndex.end()) {
        for (const auto& conn : it->second) {
            // 获取本地节点对应的TPN
            if (conn.node1 == localNodeId) {
                tpns.push_back(std::make_pair(conn.tpn1, conn.metrics));
            } else if (conn.node2 == localNodeId) {
                tpns.push_back(std::make_pair(conn.tpn2, conn.metrics));
            }
        }
    }
    return tpns;
}

// 4. 通过对端nodeId+本端port找到所有可用tpn
std::vector<std::pair<uint32_t, uint32_t>> TpConnectionManager::GetTpnsByPeerNodeLocalPort(
    uint32_t localNodeId, uint32_t peerNodeId, uint32_t localPort) const
{
    std::vector<std::pair<uint32_t, uint32_t>> tpns;
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto key = std::make_tuple(localNodeId, peerNodeId, localPort);
    auto it = m_peerNodeLocalPortIndex.find(key);
    if (it != m_peerNodeLocalPortIndex.end()) {
        for (const auto& conn : it->second) {
            // 获取本地节点对应的TPN
            if (conn.node1 == localNodeId) {
                tpns.push_back(std::make_pair(conn.tpn1, conn.metrics));
            } else if (conn.node2 == localNodeId) {
                tpns.push_back(std::make_pair(conn.tpn2, conn.metrics));
            }
        }
    }
    return tpns;
}

// 5. 通过对端nodeId+本端port+对端port找到所有可用tpn
std::vector<std::pair<uint32_t, uint32_t>> TpConnectionManager::GetTpnsByPeerNodeBothPorts(
    uint32_t localNodeId, uint32_t peerNodeId, uint32_t localPort, uint32_t peerPort) const
{
    std::vector<std::pair<uint32_t, uint32_t>> tpns;
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto key = std::make_tuple(localNodeId, peerNodeId, localPort, peerPort);
    auto it = m_bothPortsIndex.find(key);
    if (it != m_bothPortsIndex.end()) {
        for (const auto& conn : it->second) {
            // 获取本地节点对应的TPN
            if (conn.node1 == localNodeId) {
                tpns.push_back(std::make_pair(conn.tpn1, conn.metrics));
            } else if (conn.node2 == localNodeId) {
                tpns.push_back(std::make_pair(conn.tpn2, conn.metrics));
            }
        }
    }
    return tpns;
}

// 组合查询：通过完整条件找到所有可用tpn
std::vector<std::pair<uint32_t, uint32_t>> TpConnectionManager::GetTpnsByFullCriteria(
    uint32_t localNodeId, uint32_t peerNodeId, uint32_t localPort, uint32_t peerPort, uint32_t priority) const
{
    std::vector<std::pair<uint32_t, uint32_t>> tpns;
    std::lock_guard<std::mutex> lock(m_stateLock);

    // 遍历本节点的所有连接，找到匹配条件的
    auto it = m_nodeConnections.find(localNodeId);
    if (it != m_nodeConnections.end()) {
        for (const auto& conn : it->second) {
            // 检查是否匹配条件
            bool isMatch = false;
            uint32_t localTpn = 0;

            if (conn.node1 == localNodeId && conn.node2 == peerNodeId) {
                // localNodeId是node1
                if (conn.port1 == localPort && conn.port2 == peerPort &&
                    conn.priority == priority) {
                    isMatch = true;
                    localTpn = conn.tpn1;
                }
            } else if (conn.node2 == localNodeId && conn.node1 == peerNodeId) {
                // localNodeId是node2
                if (conn.port2 == localPort && conn.port1 == peerPort &&
                    conn.priority == priority) {
                    isMatch = true;
                    localTpn = conn.tpn2;
                }
            }

            if (isMatch) {
                tpns.push_back(std::make_pair(localTpn, conn.metrics));
            }
        }
    }
    return tpns;
}

// 获取某个节点的所有TPN
std::vector<uint32_t> TpConnectionManager::GetAllTpnsForNode(uint32_t nodeId) const
{
    std::vector<uint32_t> tpns;
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto it = m_nodeConnections.find(nodeId);
    if (it != m_nodeConnections.end()) {
        for (const auto& conn : it->second) {
            if (conn.node1 == nodeId) {
                tpns.push_back(conn.tpn1);
            } else if (conn.node2 == nodeId) {
                tpns.push_back(conn.tpn2);
            }
        }
    }
    return tpns;
}

// 获取节点的邻居节点
std::set<uint32_t> TpConnectionManager::GetNeighborNodes(uint32_t nodeId) const
{
    std::set<uint32_t> neighbors;
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto it = m_nodeConnections.find(nodeId);
    if (it != m_nodeConnections.end()) {
        for (const auto& conn : it->second) {
            if (conn.node1 == nodeId) {
                neighbors.insert(conn.node2);
            } else if (conn.node2 == nodeId) {
                neighbors.insert(conn.node1);
            }
        }
    }
    return neighbors;
}

// 获取与该tp相关的conn
Connection TpConnectionManager::GetConnection(uint32_t tpn, uint32_t src, uint32_t dst, uint32_t priority)
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto key = std::make_tuple(src, dst, priority);
    auto it = m_peerNodePriorityIndex.find(key);
    bool exists = false;
    Connection connection;
    NS_ASSERT_MSG(it != m_peerNodePriorityIndex.end(), "Could not find (src, dst, priority) record.");
    for (const auto& conn : it->second) {
        if (conn.node1 == src && conn.tpn1 == tpn) {
            exists = true;
            connection = conn;
        }
    }
    NS_ASSERT_MSG(exists, "Could not find tpn record.");
    return connection;
}

// 获取与该tp相关的conn,不限优先级
Connection TpConnectionManager::GetConnection(uint32_t tpn, uint32_t src, uint32_t dst)
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto key = std::make_pair(src, dst);
    auto it = m_peerNodeIndex.find(key);
    bool exists = false;
    Connection connection;
    NS_ASSERT_MSG(it != m_peerNodeIndex.end(), "Could not find (src, dst) record.");
    for (const auto& conn : it->second) {
        if (conn.node1 == src && conn.tpn1 == tpn) {
            exists = true;
            connection = conn;
            break;
        }
    }
    NS_ASSERT_MSG(exists, "Could not find tpn record.");
    return connection;
}

bool
TpConnectionManager::TryGetLocalConnection(uint32_t localNodeId,
                                           uint32_t localTpn,
                                           Connection& connection) const
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto it = m_localTpnIndex.find({localNodeId, localTpn});
    if (it == m_localTpnIndex.end())
    {
        return false;
    }
    connection = it->second;
    return true;
}

void
TpConnectionManager::NotifyEndpointMaterialized(uint32_t localNodeId,
                                                uint32_t localTpn,
                                                bool draining)
{
    const EndpointSlotState state =
        draining ? EndpointSlotState::DRAINING : EndpointSlotState::ACTIVE;
    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        if (NotifyEndpointMaterializedLocked(localNodeId, localTpn, state))
        {
            return;
        }
    }

    Connection connection;
    if (!TryGetLocalConnection(localNodeId, localTpn, connection))
    {
        return;
    }
    NS_ABORT_MSG_IF(connection.ownerNodeId == UINT32_MAX,
                    "Connection reservation lacks a canonical owner");
    const UbLifecycleResourceIdentity identity = GetConnectionIdentity(connection);
    Ptr<Node> ownerNode = NodeList::GetNode(connection.ownerNodeId);
    Ptr<ns3::UbController> ownerController = ownerNode->GetObject<ns3::UbController>();
    NS_ABORT_MSG_IF(ownerController == nullptr, "Connection owner lacks UbController");
    Ptr<TpConnectionManager> ownerManager = ownerController->GetTpConnManager();
    if (UbParallelLifecycleAdapter::IsExecutionOwner(connection.ownerNodeId))
    {
        NS_ABORT_MSG_IF(!ownerManager->MatchesCanonicalConnection(identity),
                        "Connection materialization references a stale canonical generation");
        std::lock_guard<std::mutex> ownerLock(ownerManager->m_stateLock);
        NS_ABORT_MSG_IF(!ownerManager->NotifyEndpointMaterializedLocked(localNodeId, localTpn, state),
                        "Connection owner lacks the canonical endpoint slot");
        return;
    }
    UbLifecycleControlRecord control;
    control.sourceNodeId = localNodeId;
    control.targetNodeId = connection.ownerNodeId;
    control.operation = UbLifecycleControlOperation::ENDPOINT_MATERIALIZED;
    control.resource = identity;
    control.nodeId = localNodeId;
    control.tpn = localTpn;
    control.value = static_cast<uint64_t>(state);
    UbParallelLifecycleAdapter::RouteControl(control);
}

bool
TpConnectionManager::NotifyEndpointMaterializedLocked(uint32_t localNodeId,
                                                      uint32_t localTpn,
                                                      EndpointSlotState state)
{
    const auto it = m_canonicalConnections.find({localNodeId, localTpn});
    if (it != m_canonicalConnections.end())
    {
        if (it->second.destroyRequested)
        {
            state = EndpointSlotState::DRAINING;
        }
        it->second.slot1 = state;
        it->second.slot1Materialized = true;
        return true;
    }
    for (auto& [key, record] : m_canonicalConnections)
    {
        (void)key;
        if (record.connection.node2 == localNodeId && record.connection.tpn2 == localTpn)
        {
            if (record.destroyRequested)
            {
                state = EndpointSlotState::DRAINING;
            }
            record.slot2 = state;
            record.slot2Materialized = true;
            return true;
        }
    }
    return false;
}

bool
TpConnectionManager::IsConnectionDestroyRequested(uint32_t localNodeId, uint32_t localTpn) const
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    return m_destroyRequestedViews.find({localNodeId, localTpn}) !=
           m_destroyRequestedViews.end();
}

void
TpConnectionManager::DestroyDrainOnlyEndpoint(uint32_t localNodeId, uint32_t localTpn)
{
    Connection connection;
    if (!TryGetLocalConnection(localNodeId, localTpn, connection))
    {
        return;
    }
    const UbLifecycleResourceIdentity identity = GetConnectionIdentity(connection);
    const uint8_t slot = localNodeId == connection.ownerNodeId ? 1 : 2;
    Ptr<ns3::UbController> localController =
        NodeList::GetNode(localNodeId)->GetObject<ns3::UbController>();
    localController->GetTransportChannelManager()->DestroyTp(
        localTpn,
        UbDestroyReason::EXPLICIT,
        MakeCallback(&TpConnectionManager::OnLocalEndpointDestroyed, this).Bind(identity, slot));
}

UbDestroyStatus
TpConnectionManager::DestroyConnection(uint32_t localNodeId,
                                       uint32_t localTpn,
                                       UbDestroyReason reason,
                                       UbDestroyCallback callback)
{
    std::lock_guard<std::mutex> controlLock(g_tpConnectionControlLock);
    Connection connection;
    if (!TryGetLocalConnection(localNodeId, localTpn, connection))
    {
        return UbDestroyStatus::NOT_FOUND;
    }
    const uint32_t ownerTpn = connection.ownerNodeId == connection.node1
                                  ? connection.tpn1
                                  : connection.tpn2;
    const UbLifecycleResourceIdentity identity = GetConnectionIdentity(connection);
    Ptr<ns3::UbController> ownerController =
        NodeList::GetNode(connection.ownerNodeId)->GetObject<ns3::UbController>();
    NS_ABORT_MSG_IF(ownerController == nullptr, "Connection owner lacks UbController");
    Ptr<TpConnectionManager> ownerManager = ownerController->GetTpConnManager();
    if (UbParallelLifecycleAdapter::IsExecutionOwner(connection.ownerNodeId))
    {
        return ownerManager->DestroyConnectionOwned(identity, reason, callback);
    }

    uint64_t requestId = 0;
    if (!callback.IsNull())
    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        NS_ABORT_MSG_IF(m_nextRemoteDestroyRequestId == std::numeric_limits<uint64_t>::max(),
                        "Remote connection destroy request identity exhausted");
        requestId = ++m_nextRemoteDestroyRequestId;
        m_pendingRemoteDestroyCallbacks.emplace(requestId,
                                                PendingRemoteDestroy{identity, callback});
    }
    UbLifecycleControlRecord control;
    control.sourceNodeId = m_ownerNodeId;
    control.targetNodeId = connection.ownerNodeId;
    control.operation = UbLifecycleControlOperation::CONNECTION_DESTROY_REQUEST;
    control.resource = identity;
    control.tpn = ownerTpn;
    control.requestId = requestId;
    control.value = static_cast<uint64_t>(reason);
    UbParallelLifecycleAdapter::RouteControl(control);
    return UbDestroyStatus::PENDING;
}

UbDestroyStatus
TpConnectionManager::DestroyConnectionOwned(const UbLifecycleResourceIdentity& identity,
                                            UbDestroyReason reason,
                                            UbDestroyCallback callback,
                                            uint32_t callbackNodeId,
                                            uint64_t requestId)
{
    const uint32_t ownerTpn = static_cast<uint32_t>(identity.localId);
    Connection connection;
    bool active1 = false;
    bool active2 = false;
    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        auto it = m_canonicalConnections.find({m_ownerNodeId, ownerTpn});
        if (it == m_canonicalConnections.end() || it->second.identity != identity)
        {
            return UbDestroyStatus::NOT_FOUND;
        }
        ConnectionLifecycleRecord& record = it->second;
        if (!callback.IsNull())
        {
            record.callbacks.push_back(callback);
        }
        if (requestId != 0)
        {
            record.remoteCallbacks.emplace_back(callbackNodeId, requestId);
        }
        if (record.destroyRequested)
        {
            if (static_cast<uint8_t>(reason) > static_cast<uint8_t>(record.reason))
            {
                record.reason = reason;
            }
            return UbDestroyStatus::PENDING;
        }
        record.destroyRequested = true;
        record.reason = reason;
        connection = record.connection;
        active1 = record.slot1Materialized;
        active2 = record.slot2Materialized;
    }

    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        ConnectionLifecycleRecord& record =
            m_canonicalConnections.at({m_ownerNodeId, ownerTpn});
        record.slot1 = active1 ? EndpointSlotState::DRAINING
                               : (active2 ? EndpointSlotState::DRAINING
                                          : EndpointSlotState::RETIRED);
        record.slot2 = active2 ? EndpointSlotState::DRAINING
                               : (active1 ? EndpointSlotState::DRAINING
                                          : EndpointSlotState::RETIRED);
    }

    RequestDestroyNotice(connection, identity, 1, reason, active1);
    RequestDestroyNotice(connection, identity, 2, reason, active2);

    if (active1)
    {
        RequestEndpointDestroy(connection, identity, 1, reason);
    }
    if (active2)
    {
        RequestEndpointDestroy(connection, identity, 2, reason);
    }
    TryFinalizeConnection(identity);

    std::lock_guard<std::mutex> lock(m_stateLock);
    return m_canonicalConnections.find({m_ownerNodeId, ownerTpn}) ==
                   m_canonicalConnections.end()
               ? UbDestroyStatus::COMPLETED
               : UbDestroyStatus::PENDING;
}

void
TpConnectionManager::RequestDestroyNotice(const Connection& connection,
                                          const UbLifecycleResourceIdentity& identity,
                                          uint8_t slot,
                                          UbDestroyReason reason,
                                          bool ownerObservedActive)
{
    UbLifecycleControlRecord control;
    control.sourceNodeId = m_ownerNodeId;
    control.targetNodeId = slot == 1 ? connection.node1 : connection.node2;
    control.operation = UbLifecycleControlOperation::CONNECTION_DESTROY_NOTICE;
    control.resource = identity;
    control.nodeId = control.targetNodeId;
    control.tpn = slot == 1 ? connection.tpn1 : connection.tpn2;
    control.requestId = slot | (ownerObservedActive ? 0x100 : 0);
    control.value = static_cast<uint64_t>(reason);
    UbParallelLifecycleAdapter::RouteControl(control);
}

void
TpConnectionManager::RequestEndpointDestroy(const Connection& connection,
                                            const UbLifecycleResourceIdentity& identity,
                                            uint8_t slot,
                                            UbDestroyReason reason)
{
    UbLifecycleControlRecord control;
    control.sourceNodeId = m_ownerNodeId;
    control.targetNodeId = slot == 1 ? connection.node1 : connection.node2;
    control.operation = UbLifecycleControlOperation::ENDPOINT_DESTROY_REQUEST;
    control.resource = identity;
    control.nodeId = control.targetNodeId;
    control.tpn = slot == 1 ? connection.tpn1 : connection.tpn2;
    control.requestId = slot;
    control.value = static_cast<uint64_t>(reason);
    UbParallelLifecycleAdapter::RouteControl(control);
}

void
TpConnectionManager::OnLocalEndpointDestroyed(UbLifecycleResourceIdentity connectionIdentity,
                                              uint8_t slot,
                                              UbLifecycleResourceIdentity,
                                              UbDestroyStatus status)
{
    NS_ABORT_MSG_IF(status != UbDestroyStatus::COMPLETED,
                    "Endpoint destroy callback must report completion");
    UbLifecycleControlRecord control;
    control.sourceNodeId = m_ownerNodeId;
    control.targetNodeId = connectionIdentity.ownerNodeId;
    control.operation = UbLifecycleControlOperation::ENDPOINT_DESTROYED;
    control.resource = connectionIdentity;
    control.value = slot;
    UbParallelLifecycleAdapter::RouteControl(control);
}

void
TpConnectionManager::OnEndpointDestroyed(const UbLifecycleResourceIdentity& identity,
                                         uint8_t slot,
                                         UbDestroyStatus status)
{
    NS_ABORT_MSG_IF(status != UbDestroyStatus::COMPLETED,
                    "Endpoint destroy callback must report completion");
    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        auto it = m_canonicalConnections.find(
            {m_ownerNodeId, static_cast<uint32_t>(identity.localId)});
        if (it == m_canonicalConnections.end() || it->second.identity != identity)
        {
            return;
        }
        ConnectionLifecycleRecord& record = it->second;
        if (slot == 1)
        {
            record.slot1 = EndpointSlotState::RETIRED;
            record.slot1Materialized = false;
            if (record.slot2 == EndpointSlotState::DRAINING && !record.slot2Materialized)
            {
                record.slot2 = EndpointSlotState::RETIRED;
            }
        }
        else
        {
            record.slot2 = EndpointSlotState::RETIRED;
            record.slot2Materialized = false;
            if (record.slot1 == EndpointSlotState::DRAINING && !record.slot1Materialized)
            {
                record.slot1 = EndpointSlotState::RETIRED;
            }
        }
    }
    TryFinalizeConnection(identity);
}

void
TpConnectionManager::TryFinalizeConnection(const UbLifecycleResourceIdentity& identity)
{
    Connection connection;
    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        auto it = m_canonicalConnections.find(
            {m_ownerNodeId, static_cast<uint32_t>(identity.localId)});
        if (it == m_canonicalConnections.end() || it->second.finalizing ||
            it->second.identity != identity ||
            it->second.slot1 != EndpointSlotState::RETIRED ||
            it->second.slot2 != EndpointSlotState::RETIRED)
        {
            return;
        }
        it->second.finalizing = true;
        connection = it->second.connection;
    }

    RequestEraseConnectionView(connection, identity, 1);
    RequestEraseConnectionView(connection, identity, 2);
}

void
TpConnectionManager::RequestEraseConnectionView(const Connection& connection,
                                                const UbLifecycleResourceIdentity& identity,
                                                uint8_t slot)
{
    UbLifecycleControlRecord control;
    control.sourceNodeId = m_ownerNodeId;
    control.targetNodeId = slot == 1 ? connection.node1 : connection.node2;
    control.operation = UbLifecycleControlOperation::ERASE_CONNECTION_VIEW;
    control.resource = identity;
    control.nodeId = control.targetNodeId;
    control.tpn = slot == 1 ? connection.tpn1 : connection.tpn2;
    control.value = slot;
    UbParallelLifecycleAdapter::RouteControl(control);
}

void
TpConnectionManager::OnConnectionViewErased(const UbLifecycleResourceIdentity& identity, uint8_t slot)
{
    bool complete = false;
    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        auto record = m_canonicalConnections.find(
            {m_ownerNodeId, static_cast<uint32_t>(identity.localId)});
        if (record == m_canonicalConnections.end() || record->second.identity != identity ||
            !record->second.finalizing)
        {
            return;
        }
        record->second.erasedViews |= slot == 1 ? 1 : 2;
        complete = record->second.erasedViews == 3;
    }
    if (complete)
    {
        CompleteFinalizeConnection(identity);
    }
}

void
TpConnectionManager::CompleteFinalizeConnection(const UbLifecycleResourceIdentity& identity)
{
    std::vector<UbDestroyCallback> callbacks;
    std::vector<std::pair<uint32_t, uint64_t>> remoteCallbacks;
    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        auto record = m_canonicalConnections.find(
            {m_ownerNodeId, static_cast<uint32_t>(identity.localId)});
        if (record == m_canonicalConnections.end() || record->second.identity != identity ||
            record->second.erasedViews != 3)
        {
            return;
        }
        callbacks = record->second.callbacks;
        remoteCallbacks = record->second.remoteCallbacks;
    }
    UbLifecycleCoordinator::GetOrCreate(NodeList::GetNode(identity.ownerNodeId))
        ->RetireResource(identity);

    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        m_canonicalConnections.erase(
            {m_ownerNodeId, static_cast<uint32_t>(identity.localId)});
    }
    for (const UbDestroyCallback& callback : callbacks)
    {
        callback(identity, UbDestroyStatus::COMPLETED);
    }
    for (const auto& [callbackNodeId, requestId] : remoteCallbacks)
    {
        UbLifecycleControlRecord control;
        control.sourceNodeId = m_ownerNodeId;
        control.targetNodeId = callbackNodeId;
        control.operation = UbLifecycleControlOperation::CONNECTION_DESTROY_COMPLETE;
        control.resource = identity;
        control.requestId = requestId;
        UbParallelLifecycleAdapter::RouteControl(control);
    }
}

void
TpConnectionManager::CompleteRemoteDestroy(const UbLifecycleResourceIdentity& identity,
                                           uint64_t requestId)
{
    UbDestroyCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_stateLock);
        auto pending = m_pendingRemoteDestroyCallbacks.find(requestId);
        if (pending == m_pendingRemoteDestroyCallbacks.end() || pending->second.identity != identity)
        {
            return;
        }
        callback = pending->second.callback;
        m_pendingRemoteDestroyCallbacks.erase(pending);
    }
    callback(identity, UbDestroyStatus::COMPLETED);
}

bool
TpConnectionManager::ApplyLifecycleControl(const UbLifecycleControlRecord& control)
{
    Ptr<ns3::UbController> controller =
        NodeList::GetNode(control.targetNodeId)->GetObject<ns3::UbController>();
    if (controller == nullptr)
    {
        return false;
    }
    Ptr<TpConnectionManager> manager = controller->GetTpConnManager();
    if (manager == nullptr)
    {
        return false;
    }

    const auto matchesLocalView = [&]() {
        Connection connection;
        return manager->TryGetLocalConnection(control.nodeId, control.tpn, connection) &&
               GetConnectionIdentity(connection) == control.resource;
    };

    switch (control.operation)
    {
    case UbLifecycleControlOperation::ENDPOINT_MATERIALIZED:
    {
        if (!manager->MatchesCanonicalConnection(control.resource))
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(manager->m_stateLock);
        return manager->NotifyEndpointMaterializedLocked(
            control.nodeId,
            control.tpn,
            static_cast<EndpointSlotState>(control.value));
    }
    case UbLifecycleControlOperation::CONNECTION_DESTROY_REQUEST:
        return manager->DestroyConnectionOwned(control.resource,
                                               static_cast<UbDestroyReason>(control.value),
                                               {},
                                               control.sourceNodeId,
                                               control.requestId) != UbDestroyStatus::NOT_FOUND;
    case UbLifecycleControlOperation::CONNECTION_DESTROY_NOTICE:
    {
        if (!matchesLocalView())
        {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(manager->m_stateLock);
            manager->m_destroyRequestedViews.insert({control.nodeId, control.tpn});
        }
        const bool ownerObservedActive = (control.requestId & 0x100) != 0;
        const uint8_t slot = static_cast<uint8_t>(control.requestId & 0xff);
        if (!ownerObservedActive && controller->GetTransportChannelManager()->GetTp(control.tpn) != nullptr)
        {
            controller->GetTransportChannelManager()->DestroyTp(
                control.tpn,
                static_cast<UbDestroyReason>(control.value),
                MakeCallback(&TpConnectionManager::OnLocalEndpointDestroyed, PeekPointer(manager))
                    .Bind(control.resource, slot));
        }
        return true;
    }
    case UbLifecycleControlOperation::ENDPOINT_DESTROY_REQUEST:
    {
        if (!matchesLocalView())
        {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(manager->m_stateLock);
            manager->m_destroyRequestedViews.insert({control.nodeId, control.tpn});
        }
        const uint8_t slot = static_cast<uint8_t>(control.requestId);
        const UbDestroyStatus status = controller->GetTransportChannelManager()->DestroyTp(
            control.tpn,
            static_cast<UbDestroyReason>(control.value),
            MakeCallback(&TpConnectionManager::OnLocalEndpointDestroyed, PeekPointer(manager))
                .Bind(control.resource, slot));
        if (status == UbDestroyStatus::NOT_FOUND)
        {
            manager->OnLocalEndpointDestroyed(control.resource,
                                              slot,
                                              {},
                                              UbDestroyStatus::COMPLETED);
        }
        return true;
    }
    case UbLifecycleControlOperation::ENDPOINT_DESTROYED:
        if (!manager->MatchesCanonicalConnection(control.resource))
        {
            return false;
        }
        manager->OnEndpointDestroyed(control.resource,
                                     static_cast<uint8_t>(control.value),
                                     UbDestroyStatus::COMPLETED);
        return true;
    case UbLifecycleControlOperation::ERASE_CONNECTION_VIEW:
    {
        if (!matchesLocalView())
        {
            return false;
        }
        manager->EraseConnectionView(control.nodeId, control.tpn);
        UbLifecycleControlRecord complete;
        complete.sourceNodeId = control.targetNodeId;
        complete.targetNodeId = control.resource.ownerNodeId;
        complete.operation = UbLifecycleControlOperation::ERASE_CONNECTION_VIEW_COMPLETE;
        complete.resource = control.resource;
        complete.value = control.value;
        UbParallelLifecycleAdapter::RouteControl(complete);
        return true;
    }
    case UbLifecycleControlOperation::ERASE_CONNECTION_VIEW_COMPLETE:
        if (!manager->MatchesCanonicalConnection(control.resource))
        {
            return false;
        }
        manager->OnConnectionViewErased(control.resource, static_cast<uint8_t>(control.value));
        return true;
    case UbLifecycleControlOperation::CONNECTION_DESTROY_COMPLETE:
        manager->CompleteRemoteDestroy(control.resource, control.requestId);
        return true;
    default:
        return false;
    }
}

void
TpConnectionManager::EraseConnectionView(uint32_t localNodeId, uint32_t localTpn)
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    auto local = m_localTpnIndex.find({localNodeId, localTpn});
    if (local == m_localTpnIndex.end())
    {
        return;
    }
    const Connection oriented = local->second;
    const auto removeView = [&](auto& index, const auto& key) {
        auto entry = index.find(key);
        if (entry == index.end())
        {
            return;
        }
        auto& container = entry->second;
        container.erase(std::remove_if(container.begin(),
                                       container.end(),
                                       [&](const Connection& item) {
                                           return item.ownerNodeId == oriented.ownerNodeId &&
                                                  item.node1 == oriented.node1 &&
                                                  item.tpn1 == oriented.tpn1;
                                       }),
                        container.end());
        if (container.empty())
        {
            index.erase(entry);
        }
    };
    removeView(m_nodeConnections, localNodeId);
    removeView(m_peerNodeIndex, std::make_pair(localNodeId, oriented.node2));
    removeView(m_peerNodePriorityIndex,
               std::make_tuple(localNodeId,
                               oriented.node2,
                               static_cast<uint32_t>(oriented.priority)));
    removeView(m_peerNodeLocalPortIndex,
               std::make_tuple(localNodeId, oriented.node2, oriented.port1));
    removeView(m_bothPortsIndex,
               std::make_tuple(localNodeId, oriented.node2, oriented.port1, oriented.port2));
    m_localTpnIndex.erase(local);
    m_allConnections.erase(std::remove_if(m_allConnections.begin(),
                                          m_allConnections.end(),
                                          [&](const Connection& item) {
                                              return item.ownerNodeId == oriented.ownerNodeId &&
                                                     ((item.node1 == localNodeId &&
                                                       item.tpn1 == localTpn) ||
                                                      (item.node2 == localNodeId &&
                                                       item.tpn2 == localTpn));
                                          }),
                           m_allConnections.end());
    m_tpnList.erase(localTpn);
    m_reservedTpnList.erase(localTpn);
    m_destroyRequestedViews.erase({localNodeId, localTpn});
    m_nextTpn = std::min(m_nextTpn, localTpn);
}

bool
TpConnectionManager::GetConnectionLifecycleSnapshot(uint32_t localTpn,
                                                     ConnectionLifecycleSnapshot& snapshot) const
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    const auto it = m_canonicalConnections.find({m_ownerNodeId, localTpn});
    if (it == m_canonicalConnections.end())
    {
        return false;
    }
    const ConnectionLifecycleRecord& record = it->second;
    snapshot.ownerNodeId = m_ownerNodeId;
    snapshot.generation = record.generation;
    snapshot.node1 = record.connection.node1;
    snapshot.tpn1 = record.connection.tpn1;
    snapshot.node2 = record.connection.node2;
    snapshot.tpn2 = record.connection.tpn2;
    snapshot.slot1 = record.slot1;
    snapshot.slot2 = record.slot2;
    snapshot.destroyRequested = record.destroyRequested;
    snapshot.reason = record.reason;
    return true;
}

size_t
TpConnectionManager::GetCanonicalConnectionCountForTest() const
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    return m_canonicalConnections.size();
}

// 清空指定节点的连接
void TpConnectionManager::ClearNodeConnections(uint32_t nodeId)
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    m_nodeConnections.erase(nodeId);
    ClearNodeFromIndexes(nodeId);

    // 从allConnections_中移除相关连接
    m_allConnections.erase(
        std::remove_if(m_allConnections.begin(), m_allConnections.end(),
            [nodeId](const Connection& conn) {
                return conn.node1 == nodeId || conn.node2 == nodeId;
            }),
        m_allConnections.end()
    );
}

// 清空所有连接
void TpConnectionManager::Clear()
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    m_allConnections.clear();
    m_nodeConnections.clear();
    m_peerNodeIndex.clear();
    m_peerNodePriorityIndex.clear();
    m_peerNodeLocalPortIndex.clear();
    m_bothPortsIndex.clear();
    m_localTpnIndex.clear();
    m_tpnList.clear();
    m_reservedTpnList.clear();
}

// 为指定节点建立各种索引
void TpConnectionManager::BuildIndexesForNode(uint32_t localNodeId, Connection conn)
{
    uint32_t peerNodeId;
    uint32_t localPort;
    uint32_t peerPort;
    uint32_t localTpn;
    uint32_t peerTpn;

    // 确定对端节点和端口
    if (conn.node1 == localNodeId) {
        peerNodeId = conn.node2;
        localPort = conn.port1;
        peerPort = conn.port2;
        localTpn = conn.tpn1;
        peerTpn = conn.tpn2;
    } else {
        peerNodeId = conn.node1;
        localPort = conn.port2;
        peerPort = conn.port1;
        localTpn = conn.tpn2;
        peerTpn = conn.tpn1;
    }
    conn.node1 = localNodeId;
    conn.node2 = peerNodeId;
    conn.port1 = localPort;
    conn.port2 = peerPort;
    conn.tpn1 = localTpn;
    conn.tpn2 = peerTpn;
    // 建立各种索引

    // 索引1: 添加到节点连接列表
    m_nodeConnections[localNodeId].push_back(conn);

    // 索引2: 对端节点
    m_peerNodeIndex[{localNodeId, peerNodeId}].push_back(conn);

    // 索引3: 对端节点+优先级
    m_peerNodePriorityIndex[{localNodeId, peerNodeId, static_cast<uint32_t>(conn.priority)}].push_back(conn);

    // 索引4: 对端节点+本端端口
    m_peerNodeLocalPortIndex[{localNodeId, peerNodeId, localPort}].push_back(conn);

    // 索引5: 对端节点+本端端口+对端端口
    m_bothPortsIndex[{localNodeId, peerNodeId, localPort, peerPort}].push_back(conn);

    // 索引6: 本地 TPN
    m_localTpnIndex[{localNodeId, localTpn}] = conn;

    if (m_reservedTpnList.erase(localTpn) == 0) {
        NS_ASSERT_MSG(m_tpnList.find(localTpn) == m_tpnList.end(), "Tpn already exists!");
    }
    m_tpnList.insert(localTpn);
}

// 从索引中清理指定节点
void TpConnectionManager::ClearNodeFromIndexes(uint32_t nodeId)
{
    // 清理各种索引中包含该节点的条目
    for (auto it = m_peerNodeIndex.begin(); it != m_peerNodeIndex.end();) {
        if (it->first.first == nodeId) {
            it = m_peerNodeIndex.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_peerNodePriorityIndex.begin(); it != m_peerNodePriorityIndex.end();) {
        if (std::get<0>(it->first) == nodeId) {
            it = m_peerNodePriorityIndex.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_peerNodeLocalPortIndex.begin(); it != m_peerNodeLocalPortIndex.end();) {
        if (std::get<0>(it->first) == nodeId) {
            it = m_peerNodeLocalPortIndex.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_bothPortsIndex.begin(); it != m_bothPortsIndex.end();) {
        if (std::get<0>(it->first) == nodeId) {
            it = m_bothPortsIndex.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_localTpnIndex.begin(); it != m_localTpnIndex.end();)
    {
        if (it->first.first == nodeId)
        {
            it = m_localTpnIndex.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::vector<std::pair<uint32_t, uint32_t>>
TpConnectionManager::ReserveNewTpConnections(uint32_t src,
                                             uint32_t dst,
                                             uint32_t priority,
                                             bool useShortestPath)
{
    std::vector<std::pair<uint32_t, uint32_t>> tpns;
    // 目标节点+端口的ip地址，出端口和跳数pair 的 vector
    std::map<Ipv4Address, std::vector<std::pair<uint16_t, uint32_t>>> routingEntries;
    // 查找路由，寻找从本节点到目的节点的路径
    Ptr<UbRoutingProcess> rt = NodeList::GetNode(src)->GetObject<UbSwitch>()->GetRoutingProcess();
    for (uint32_t dstPort = 0; dstPort < NodeList::GetNode(dst)->GetNDevices(); dstPort++) {
        // 查找去往该ip的出端口，若非空则记录
        Ipv4Address dstPortIp = NodeIdToIp(dst, dstPort);
        const auto shortestOutPorts = rt->GetRoutingTable().LookupShortest(dstPortIp.Get());
        const auto otherOutPorts = rt->GetRoutingTable().LookupOther(dstPortIp.Get());
        if (!shortestOutPorts.empty()) {
            routingEntries[dstPortIp] = std::vector<std::pair<uint16_t, uint32_t>>();
            for (uint16_t outPort : shortestOutPorts) {
                routingEntries[dstPortIp].push_back(std::make_pair(outPort, 0));
            }
        }
        // 仅在开启非最短路模式的情况下加入非最短路由
        if (!otherOutPorts.empty() && !useShortestPath) {
            routingEntries[dstPortIp] = std::vector<std::pair<uint16_t, uint32_t>>();
            for (uint16_t outPort : otherOutPorts) {
                routingEntries[dstPortIp].push_back(std::make_pair(outPort, 1));
            }
        }
    }
    // 如果为空，则判错，路由有问题。不考虑省略host路由的情况
    if (routingEntries.empty()) {
        NS_ASSERT_MSG(0, "Invalid routing! Traffic can't arrive!");
    }
    for (auto it = routingEntries.begin(); it != routingEntries.end(); it++) {
        Ipv4Address dstIp = it->first;
        uint32_t dstPort = IpToPortId(dstIp);
        for (auto outPortIt = it->second.begin(); outPortIt != it->second.end(); outPortIt++) {
            // 无论是否创建TP，都建立connection记录，以供后续所有源目的节点相同的流使用
            Ptr<ns3::UbController> sendCtrl = NodeList::GetNode(src)->GetObject<ns3::UbController>();
            Ptr<ns3::UbController> recvCtrl = NodeList::GetNode(dst)->GetObject<ns3::UbController>();
            Connection conn;
            conn.node1 = src;
            conn.port1 = outPortIt->first;
            conn.tpn1 = GetNextTpn();
            conn.node2 = dst;
            conn.port2 = dstPort;
            conn.tpn2 = recvCtrl->GetTpConnManager()->GetNextTpn();
            conn.priority = priority;
            conn.metrics = outPortIt->second;
            // connection添加tpnConn
            AddUnilateralConnection(conn, src);
            recvCtrl->GetTpConnManager()->AddUnilateralConnection(conn, dst);
            tpns.emplace_back(conn.tpn1, conn.metrics);
        }
    }
    return tpns;
}

std::vector<uint32_t> TpConnectionManager::ReconstructTPs(std::vector<uint32_t> tpns, uint32_t src, uint32_t dst,
    uint32_t priority, bool useShortestPath, bool useMultiPath)
{
    std::vector<uint32_t> res;
    if (!useMultiPath) { // 单路径模式下仅创建一个
        uint32_t idx = (uint32_t)(m_random->GetValue() * tpns.size());
        NS_LOG_DEBUG("Connection num: " << tpns.size() << " Reconstruct idx: " << idx);
        Connection conn = GetConnection(tpns[idx], src, dst, priority);
        res.push_back(ReconstructTp(conn));
    } else { // 多路径模式下全部创建
        NS_LOG_DEBUG("Connection num: " << tpns.size() << " all reconstruct.");
        for (uint32_t i = 0; i < tpns.size(); i++) {
            Connection conn = GetConnection(tpns[i], src, dst, priority);
            res.push_back(ReconstructTp(conn));
        }
    }
    return res;
}

uint32_t TpConnectionManager::ReconstructTp(Connection conn)
{
    Ptr<UbController> sendCtrl = NodeList::GetNode(conn.node1)->GetObject<UbController>();
    if (!sendCtrl->GetTransportChannelManager()->Contains(conn.tpn1)) { // 不存在，创建
        auto sendHostCaqm = UbCongestionControl::Create(UB_DEVICE);
        sendCtrl->GetTransportChannelManager()->CreateTp(conn.node1, conn.node2, conn.port1, conn.port2,
                           conn.priority, conn.tpn1, conn.tpn2, sendHostCaqm);
    }
    return conn.tpn1;
}

void TpConnectionManager::RemoveUselessTps(uint32_t jettyNum, uint32_t src, uint32_t dst, uint32_t priority)
{
    if (!m_removeUselessTp) { // 仅在开启了删除无用tp模式下才进行此操作
        return;
    }
    (void)jettyNum;
    (void)src;
    (void)dst;
    (void)priority;
    NS_ABORT_MSG("RemoveUselessTp task-driven teardown is unsupported; use explicit TP lifecycle management");
}

uint32_t TpConnectionManager::GetNextTpn()
{
    std::lock_guard<std::mutex> lock(m_stateLock);
    return ReserveNextTpnLocked();
}

uint32_t TpConnectionManager::ReserveNextTpnLocked()
{
    while (m_tpnList.find(m_nextTpn) != m_tpnList.end() ||
           m_reservedTpnList.find(m_nextTpn) != m_reservedTpnList.end()) {
        ++m_nextTpn;
    }

    const uint32_t reservedTpn = m_nextTpn;
    m_reservedTpnList.insert(reservedTpn);
    ++m_nextTpn;
    return reservedTpn;
}

}
