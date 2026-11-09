// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_SPRAYCC_H
#define UB_SPRAYCC_H

#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/ub-congestion-control.h"

#include <cstdint>
#include <deque>
#include <map>
#include <tuple>
#include <vector>

namespace ns3 {

class UbTransportChannel;
class UbSwitch;
class UbPort;

/**
 * @brief SprayCC: congestion control for packet-sprayed, multi-plane UB traffic.
 *
 * One controller per RTP TP, i.e. per plane of a TP group. Rate based (paced), so the per-plane
 * rate can fall far below one MTU per RTT at high fan-in (e.g. alltoall: 31 senders x 8 planes into
 * one device) and is independent of the path RTT.
 * - Switch: marks CE when the packet's output queue represents more than KDelay of drain time at
 *   the port's *current* rate, so lane-degraded ports mark correctly. By default only device-facing
 *   (last-hop) outputs mark: in-fabric congestion is the load balancer's job, and marking every
 *   hop would penalise multi-hop flows (parking-lot unfairness).
 * - Receiver: counts marked/total data packets (including out-of-order arrivals) and returns both
 *   counters in the CETPH of the next cumulative ACK, so spray reordering does not lose feedback.
 * - Sender, once per fixed control period T: alpha <- (1-g) alpha + g F (F = marked fraction);
 *   if marked, rate *= (1 - alpha/2) (DCTCP-style); otherwise additive increase.
 * - Coupled increase across the TPs of one (src, dst, priority) group: the group gains AI of rate
 *   per period in total, split across planes by their rate share, so a multi-plane flow is as
 *   aggressive as one flow and pools load onto unmarked planes (resource pooling). Consecutive
 *   unmarked periods grow the increase geometrically (hyper increase) to refill freed capacity.
 * - An in-flight cap (rate x (base RTT + CapSlack) x WindowRttFactor) bounds how much a TP can
 *   pile up in front of a slow remote bottleneck (e.g. a lane-degraded far-end port); the TP then
 *   stops admitting segments and TPG dispatch shifts load to the other planes.
 */
class UbHostSprayCc : public UbCongestionControl
{
  public:
    static TypeId GetTypeId();
    UbHostSprayCc();
    ~UbHostSprayCc() override;

    void OnTpAttached(Ptr<UbTransportChannel> tp) override;
    uint32_t GetRestCwnd() override;
    bool IsCcLimited(uint32_t bytes) override;
    void OnSenderRetransmissionRollback(uint32_t retransmitBytes) override;
    void OnSenderRetransmissionPacketSent(uint32_t psn, uint32_t size) override;
    void OnSenderPrepareIpBasedNetworkHeader(UbIpBasedNetworkHeader& header) override;
    void OnSenderDataPacketSent(uint32_t psn, uint32_t size) override;
    /** True if a switch marked this packet (SprayCC edge marking: queueing at the last hop). */
    static bool IsMarked(const UbIpBasedNetworkHeader& header);
    void OnReceiverDataPacketReceived(uint64_t psn,
                                      uint32_t size,
                                      UbIpBasedNetworkHeader header) override;
    UbCongestionExtTph OnReceiverPrepareAckCongestionHeader(uint64_t psnStart,
                                                            uint64_t psnEnd) override;
    using UbCongestionControl::OnSenderCongestionNotification;
    void OnSenderCongestionNotification(TpOpcode opcode,
                                        uint32_t psn,
                                        UbCongestionExtTph header) override;
    TpOpcode GetAckOpcode() const override;

    double GetRateBps() const { return m_rateBps; }
    double GetAlpha() const { return m_alpha; }
    Time GetSrtt() const { return m_srtt; }
    Time GetSmoothedRtt() const override { return m_srtt; }
    uint64_t GetInflightBytes() const { return m_inflight; }
    uint64_t GetWindowCapBytes() const;

    /** Encode/decode helpers for the CETPH raw field (also used by tests). */
    static uint32_t EncodeFeedback(uint32_t marked, uint32_t total);
    static void DecodeFeedback(uint32_t raw, uint32_t& marked, uint32_t& total);

  private:
    struct SentPacket
    {
        uint32_t psn;
        uint32_t size;
        Time sendTime;
    };

    using GroupKey = std::tuple<uint32_t, uint32_t, uint16_t>;
    static std::map<GroupKey, std::vector<UbHostSprayCc*>>& Groups();

    void DoDispose() override;
    void LeaveGroup();
    double LineRateBps() const;
    double GroupRateBps() const;
    void OnPeriodEnd();
    void SchedulePacingWakeup();
    void HandlePacingWakeup();

    Ptr<UbTransportChannel> m_tp;
    Ptr<UbPort> m_port;
    bool m_inGroup{false};
    GroupKey m_groupKey{};

    // Attributes.
    Time m_period;
    double m_gain;
    double m_aiFraction;
    double m_minRateFraction;
    double m_windowRttFactor;
    Time m_capSlack;
    bool m_coupled;
    bool m_hyperIncrease;
    bool m_pacing;

    // Sender state.
    double m_rateBps{0.0};
    double m_alpha{1.0};
    uint32_t m_unmarkedPeriods{0};
    uint64_t m_inflight{0};
    std::deque<SentPacket> m_sent;
    uint64_t m_periodMarked{0};
    uint64_t m_periodTotal{0};
    Time m_lastUpdate;
    Time m_nextSend;
    EventId m_pacingEvent;
    Time m_srtt;
    Time m_minRtt;

    // Receiver state.
    uint32_t m_rxMarked{0};
    uint32_t m_rxTotal{0};
};

class UbSwitchSprayCc : public UbCongestionControl
{
  public:
    static TypeId GetTypeId();
    UbSwitchSprayCc();
    ~UbSwitchSprayCc() override = default;
    void OnSwitchAttached(Ptr<UbSwitch> sw) override;
    void OnSwitchPostEnqueue(uint32_t inPort, uint32_t outPort, Ptr<Packet> p) override;

  private:
    Ptr<UbSwitch> m_switch;
    Time m_kDelay;
    bool m_edgeOnly{true};
    std::vector<int8_t> m_deviceFacing; //!< per output: -1 unknown, 0 fabric, 1 device-facing
};

} // namespace ns3

#endif /* UB_SPRAYCC_H */
