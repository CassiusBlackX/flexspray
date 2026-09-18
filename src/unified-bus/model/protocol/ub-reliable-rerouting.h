// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_RELIABLE_REROUTING_H
#define UB_RELIABLE_REROUTING_H

#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <deque>
#include <ostream>
#include <string>

namespace ns3 {

class UbLink;
class UbPort;

/**
 * @brief One reliable-rerouting observability record.
 *
 * Fields that do not apply to an event keep their documented sentinel: `attemptId == 0` means "no
 * local attempt identity", and `oldOutput`/`newOutput` use `UB_REROUTING_NO_OUTPUT`.
 */
constexpr uint32_t UB_REROUTING_NO_OUTPUT = 0xffffffffu;

/** Result of offering one retained packet to the switch recovery path. */
enum class UbRecoverySubmitResult : uint8_t
{
    TRANSFERRED,  //!< Packet entered a healthy VOQ; the caller may release ownership.
    BLOCKED,      //!< Ingress admission is full; the caller keeps ownership and retries later.
    DISCARDED,    //!< Link-local control frame disposed locally instead of rerouted.
};

struct UbReliableReroutingEvent
{
    uint32_t nodeId{0};
    uint32_t portId{0};
    std::string event;
    uint64_t attemptId{0};
    uint32_t oldOutput{UB_REROUTING_NO_OUTPUT};
    uint32_t newOutput{UB_REROUTING_NO_OUTPUT};
    uint64_t retryPackets{0};
    uint64_t retryBytes{0};
    uint64_t pendingPackets{0};
    uint64_t pendingBytes{0};
};

/**
 * @brief Direction-local reliable rerouting state for one UB port.
 *
 * The component keeps whole-packet transmit backups for one link direction, releases them when the
 * peer reports a complete receipt, and turns a physical link failure into the model's
 * detect / wait-for-LastAck / recover / drained progression. It owns the modeled timers and asks
 * the owning switch to migrate retained work; it never mutates switch queues itself.
 */
class UbReliableRerouting : public Object
{
  public:
    enum class State
    {
        DISABLED,           //!< Feature not armed on this port.
        HEALTHY,            //!< Link healthy, backups released by peer receipt.
        DETECTION_PENDING,  //!< Physical failure seen, endpoint has not detected it yet.
        WAIT_LAST_ACK,      //!< Output detected as failed; backlog frozen until LastAck.
        RECOVERING,         //!< LastAck received; retained work can move to healthy outputs.
        DRAINED             //!< Local recovery work finished; the failed output stays down.
    };

    static TypeId GetTypeId();

    UbReliableRerouting();
    ~UbReliableRerouting() override;

    /**
     * @brief Arm this port for reliable rerouting before traffic starts.
     * @param port Owning port.
     * @param link Attached link.
     * @param detectionDelay Physical failure to endpoint detection.
     * @param lastAckDelay Detection to modeled LastAck receipt.
     */
    void Arm(Ptr<UbPort> port, Ptr<UbLink> link, Time detectionDelay, Time lastAckDelay);

    bool IsArmed() const { return m_state != State::DISABLED; }
    State GetState() const { return m_state; }
    static const char* GetStateName(State state);

    /** The detected failed output must not be used for routing or transmission. */
    bool IsOutputBlocked() const
    {
        return m_state == State::WAIT_LAST_ACK || m_state == State::RECOVERING;
    }

    Time GetDetectionDelay() const { return m_detectionDelay; }
    Time GetLastAckDelay() const { return m_lastAckDelay; }

    // ---- Backup lifecycle -------------------------------------------------

    /**
     * @brief Retain an independent copy of an eligible transmit attempt.
     * @param packet Packet handed to the link.
     * @param inPort Original ingress port identity.
     * @param priority Original VL/priority.
     * @return Local monotonic attempt token; never zero.
     */
    uint64_t CreateAttempt(Ptr<const Packet> packet, uint32_t inPort, uint32_t priority);

    /**
     * @brief Release the attempt that the peer completely received.
     * @param token Attempt token; zero means "not a retained attempt".
     */
    void ConfirmAttempt(uint64_t token);

    uint64_t GetRetryPacketCount() const { return static_cast<uint64_t>(m_attempts.size()); }
    uint64_t GetRetryByteCount() const;
    uint64_t GetPeakRetryPacketCount() const { return m_peakRetryPackets; }

    // ---- Failure progression ---------------------------------------------

    /** Physical failure action (t0); arms the detection timer. */
    void OnPhysicalFailure();

    /** Detection completion (td); freezes the output and stops new attempts. */
    void OnDetection();

    /** Modeled LastAck receipt (ta); starts backlog recovery. */
    void OnLastAck();

    /** Switch reports that retained backlog for this output finished recovery. */
    void NotifyBacklogFinished();

    /**
     * @brief Retry a blocked retained attempt after real ingress capacity was released.
     *
     * Called by the switch through its coalesced space-release wakeup.
     */
    void ResumeRetryBacklog();

    /** Switch reports how much retained work it currently owns for this output. */
    void SetPendingRecoveryCounters(uint64_t packets, uint64_t bytes);

    /**
     * @brief Publish an evidence record for switch-owned retained work.
     *
     * VOQ, egress, and delayed-processing backlog entries are owned by the switch, but the
     * observability path stays single-sourced on this component so the CSV schema has one writer.
     */
    void PublishBacklogEvent(const std::string& event,
                             uint64_t entryId,
                             uint32_t oldOutput,
                             uint32_t newOutput = UB_REROUTING_NO_OUTPUT);

    void DoDispose() override;

    TracedCallback<const UbReliableReroutingEvent&> m_traceRerouting;

  private:
    struct Attempt
    {
        uint64_t token{0};
        Ptr<Packet> packet;
        uint32_t inPort{0};
        uint32_t priority{0};
    };

    void Publish(const std::string& event,
                 uint64_t attemptId = 0,
                 uint32_t oldOutput = UB_REROUTING_NO_OUTPUT,
                 uint32_t newOutput = UB_REROUTING_NO_OUTPUT);
    void TransitionTo(State state);
    void TransferRetryBacklog();
    void UpdateDrainState();
    bool m_retryBlocked{false};

    Ptr<UbPort> m_port;
    Ptr<UbLink> m_link;
    State m_state{State::DISABLED};
    Time m_detectionDelay{Time(0)};
    Time m_lastAckDelay{Time(0)};
    EventId m_detectionEvent;
    EventId m_lastAckEvent;
    uint64_t m_nextAttemptToken{1};
    std::deque<Attempt> m_attempts;
    uint64_t m_peakRetryPackets{0};
    uint64_t m_pendingPackets{0};
    uint64_t m_pendingBytes{0};
    bool m_backlogFinished{false};
};

} // namespace ns3

namespace ns3 {

inline std::ostream&
operator<<(std::ostream& os, UbReliableRerouting::State state)
{
    return os << UbReliableRerouting::GetStateName(state);
}

} // namespace ns3

#endif // UB_RELIABLE_REROUTING_H
