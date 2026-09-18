// SPDX-License-Identifier: GPL-2.0-only
#include "ub-reliable-rerouting.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/ub-link.h"
#include "ns3/ub-port.h"
#include "ns3/ub-switch.h"

#include <algorithm>
#include <string>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbReliableRerouting");
NS_OBJECT_ENSURE_REGISTERED(UbReliableRerouting);

TypeId
UbReliableRerouting::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::UbReliableRerouting")
            .SetParent<Object>()
            .SetGroupName("UnifiedBus")
            .AddConstructor<UbReliableRerouting>()
            .AddTraceSource("Rerouting",
                            "Reliable rerouting state transitions and retained-work events.",
                            MakeTraceSourceAccessor(&UbReliableRerouting::m_traceRerouting),
                            "ns3::UbReliableRerouting::Rerouting");
    return tid;
}

UbReliableRerouting::UbReliableRerouting() = default;

UbReliableRerouting::~UbReliableRerouting() = default;

const char*
UbReliableRerouting::GetStateName(State state)
{
    switch (state)
    {
    case State::DISABLED:
        return "DISABLED";
    case State::HEALTHY:
        return "HEALTHY";
    case State::DETECTION_PENDING:
        return "DETECTION_PENDING";
    case State::WAIT_LAST_ACK:
        return "WAIT_LAST_ACK";
    case State::RECOVERING:
        return "RECOVERING";
    case State::DRAINED:
        return "DRAINED";
    }
    return "UNKNOWN";
}

void
UbReliableRerouting::Arm(Ptr<UbPort> port, Ptr<UbLink> link, Time detectionDelay, Time lastAckDelay)
{
    NS_ABORT_MSG_IF(port == nullptr || link == nullptr,
                    "Reliable rerouting requires an attached port and link");
    NS_ABORT_MSG_IF(detectionDelay.IsStrictlyNegative() || lastAckDelay.IsStrictlyNegative(),
                    "Reliable rerouting delays must be non-negative");
    NS_ABORT_MSG_IF(m_state != State::DISABLED,
                    "Reliable rerouting is armed once, before traffic starts");
    m_port = port;
    m_link = link;
    m_detectionDelay = detectionDelay;
    m_lastAckDelay = lastAckDelay;
    TransitionTo(State::HEALTHY);
}

void
UbReliableRerouting::TransitionTo(State state)
{
    m_state = state;
    NS_LOG_DEBUG("[UbReliableRerouting] node " << (m_port ? m_port->GetNode()->GetId() : 0)
                                               << " port " << (m_port ? m_port->GetIfIndex() : 0)
                                               << " -> " << GetStateName(state));
    Publish(std::string("state:") + GetStateName(state));
}

uint64_t
UbReliableRerouting::CreateAttempt(Ptr<const Packet> packet, uint32_t inPort, uint32_t priority)
{
    if (m_state == State::DISABLED || packet == nullptr)
    {
        return 0;
    }

    // An independent whole-packet copy: recovery must never race the live transmission.
    Attempt attempt;
    attempt.token = m_nextAttemptToken++;
    attempt.packet = packet->Copy();
    attempt.inPort = inPort;
    attempt.priority = priority;
    m_attempts.push_back(attempt);
    m_peakRetryPackets = std::max<uint64_t>(m_peakRetryPackets, m_attempts.size());
    NS_LOG_DEBUG("[UbReliableRerouting] retained attempt " << attempt.token << " ("
                                                           << attempt.packet->GetSize()
                                                           << " bytes) retry=" << m_attempts.size());
    return attempt.token;
}

void
UbReliableRerouting::ConfirmAttempt(uint64_t token)
{
    if (token == 0 || m_state == State::DISABLED)
    {
        return;
    }
    const auto it = std::find_if(m_attempts.begin(), m_attempts.end(), [token](const Attempt& a) {
        return a.token == token;
    });
    if (it == m_attempts.end())
    {
        // Detached/unarmed senders and already-transferred attempts legitimately confirm nothing.
        NS_LOG_DEBUG("[UbReliableRerouting] confirmation for unknown attempt " << token);
        return;
    }
    m_attempts.erase(it);
}

uint64_t
UbReliableRerouting::GetRetryByteCount() const
{
    uint64_t bytes = 0;
    for (const auto& attempt : m_attempts)
    {
        bytes += attempt.packet->GetSize();
    }
    return bytes;
}

void
UbReliableRerouting::OnPhysicalFailure()
{
    if (m_state != State::HEALTHY)
    {
        NS_LOG_DEBUG("[UbReliableRerouting] ignoring repeated physical failure notification");
        return;
    }
    TransitionTo(State::DETECTION_PENDING);
    m_detectionEvent = Simulator::Schedule(m_detectionDelay,
                                           &UbReliableRerouting::OnDetection,
                                           this);
}

void
UbReliableRerouting::OnDetection()
{
    if (m_state != State::DETECTION_PENDING)
    {
        return;
    }
    TransitionTo(State::WAIT_LAST_ACK);
    Ptr<UbSwitch> sw = m_port->GetNode()->GetObject<UbSwitch>();
    NS_ABORT_MSG_IF(sw == nullptr, "Reliable rerouting requires a switch on the port node");
    sw->NotifyOutputDetectedFailed(m_port->GetIfIndex());
    m_lastAckEvent = Simulator::Schedule(m_lastAckDelay, &UbReliableRerouting::OnLastAck, this);
}

void
UbReliableRerouting::OnLastAck()
{
    if (m_state != State::WAIT_LAST_ACK)
    {
        return;
    }
    TransitionTo(State::RECOVERING);
    Ptr<UbSwitch> sw = m_port->GetNode()->GetObject<UbSwitch>();
    NS_ABORT_MSG_IF(sw == nullptr, "Reliable rerouting requires a switch on the port node");
    // The switch migrates its own retained backlog (VOQ, egress, delayed processing) before the
    // endpoint replays its retry attempts, so the LastAck gate applies to both owners.
    sw->StartOutputRecovery(m_port->GetIfIndex());
    TransferRetryBacklog();
}

void
UbReliableRerouting::TransferRetryBacklog()
{
    if (m_state != State::RECOVERING)
    {
        return;
    }
    Ptr<UbSwitch> sw = m_port->GetNode()->GetObject<UbSwitch>();
    NS_ABORT_MSG_IF(sw == nullptr, "Reliable rerouting requires a switch on the port node");
    const uint32_t outPort = m_port->GetIfIndex();

    // Retained attempts keep recovery ownership until the switch confirms admission into a healthy
    // VOQ, so a blocked attempt stays here and is retried on the next space-release wakeup.
    while (!m_attempts.empty())
    {
        Attempt& attempt = m_attempts.front();
        const uint64_t token = attempt.token;
        const uint32_t inPort = attempt.inPort;
        const uint32_t priority = attempt.priority;
        Ptr<Packet> packet = attempt.packet;
        const UbRecoverySubmitResult result =
            sw->SubmitRecoveryPacket(packet, inPort, priority, outPort, token);
        if (result == UbRecoverySubmitResult::BLOCKED)
        {
            // No drop and no loss: the attempt stays owned here and is retried when real ingress
            // capacity is released, keeping the alternate output the switch already selected.
            m_retryBlocked = true;
            Publish("admission_block", token, outPort);
            break;
        }
        if (result == UbRecoverySubmitResult::DISCARDED)
        {
            // Link-local credit/PFC frames are not rerouted; their disposal is not data loss.
            Publish("control_frame_discarded", token, outPort);
            m_attempts.pop_front();
            continue;
        }
        Publish("transfer", token, outPort, sw->GetLastRecoveryOutput());
        m_attempts.pop_front();
    }
    UpdateDrainState();
}

void
UbReliableRerouting::ResumeRetryBacklog()
{
    if (m_state != State::RECOVERING || !m_retryBlocked)
    {
        return;
    }
    m_retryBlocked = false;
    Publish("admission_resume");
    TransferRetryBacklog();
}

void
UbReliableRerouting::NotifyBacklogFinished()
{
    m_backlogFinished = true;
    UpdateDrainState();
}

void
UbReliableRerouting::PublishBacklogEvent(const std::string& event,
                                         uint64_t entryId,
                                         uint32_t oldOutput,
                                         uint32_t newOutput)
{
    Publish(event, entryId, oldOutput, newOutput);
}

void
UbReliableRerouting::SetPendingRecoveryCounters(uint64_t packets, uint64_t bytes)
{
    m_pendingPackets = packets;
    m_pendingBytes = bytes;
    if (packets != 0)
    {
        m_backlogFinished = false;
    }
    UpdateDrainState();
}

void
UbReliableRerouting::UpdateDrainState()
{
    if (m_state != State::RECOVERING)
    {
        return;
    }
    // Local drain means: no retained attempt here, no retained packet in the switch's recovery
    // storage, and no egress backlog still owned by the failed output.
    const bool egressBacklog = m_port != nullptr && m_port->GetUbQueue() != nullptr &&
                               !m_port->GetUbQueue()->IsEmpty();
    if (!m_attempts.empty() || m_pendingPackets != 0 || egressBacklog)
    {
        return;
    }
    m_backlogFinished = true;
    TransitionTo(State::DRAINED);
}

void
UbReliableRerouting::Publish(const std::string& event,
                             uint64_t attemptId,
                             uint32_t oldOutput,
                             uint32_t newOutput)
{
    UbReliableReroutingEvent record;
    record.nodeId = m_port != nullptr ? m_port->GetNode()->GetId() : 0;
    record.portId = m_port != nullptr ? m_port->GetIfIndex() : 0;
    record.event = event;
    record.attemptId = attemptId;
    record.oldOutput = oldOutput;
    record.newOutput = newOutput;
    record.retryPackets = GetRetryPacketCount();
    record.retryBytes = GetRetryByteCount();
    record.pendingPackets = m_pendingPackets;
    record.pendingBytes = m_pendingBytes;
    m_traceRerouting(record);
}

void
UbReliableRerouting::DoDispose()
{
    m_detectionEvent.Cancel();
    m_lastAckEvent.Cancel();
    m_attempts.clear();
    m_pendingPackets = 0;
    m_pendingBytes = 0;
    m_link = nullptr;
    m_port = nullptr;
    m_state = State::DISABLED;
    Object::DoDispose();
}

} // namespace ns3
