// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/ub-spraycc.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/ub-link.h"
#include "ns3/ub-port.h"
#include "ns3/ub-queue-manager.h"
#include "ns3/ub-switch.h"
#include "ns3/ub-transport.h"

#include <algorithm>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbSprayCc");
NS_OBJECT_ENSURE_REGISTERED(UbHostSprayCc);
NS_OBJECT_ENSURE_REGISTERED(UbSwitchSprayCc);

namespace {
// SprayCC reuses the FECN network-header encoding (mode 0b100); it is never enabled together with
// DCQCN, so the meaning of the bit is unambiguous within a run.
constexpr uint8_t kSprayCcMode = 0b100;
constexpr uint8_t kSprayCcMarked = 0x1;
constexpr uint32_t kPsnMask = 0xFFFFFF;

bool
PsnLeq(uint32_t a, uint32_t b)
{
    return ((b - a) & kPsnMask) < 0x800000;
}
} // namespace

TypeId
UbHostSprayCc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::UbHostSprayCc")
            .SetParent<UbCongestionControl>()
            .SetGroupName("UnifiedBus")
            .AddConstructor<UbHostSprayCc>()
            .AddAttribute("Period",
                          "Control period T of the alpha update, decrease and increase.",
                          TimeValue(NanoSeconds(2000)),
                          MakeTimeAccessor(&UbHostSprayCc::m_period),
                          MakeTimeChecker(TimeStep(1)))
            .AddAttribute("Gain",
                          "EWMA gain g of the marked-fraction estimate alpha.",
                          DoubleValue(1.0 / 16),
                          MakeDoubleAccessor(&UbHostSprayCc::m_gain),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("AiFraction",
                          "Additive rate increase per period for the whole TP group, as a fraction "
                          "of the line rate.",
                          DoubleValue(0.05),
                          MakeDoubleAccessor(&UbHostSprayCc::m_aiFraction),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("MinRateFraction",
                          "Minimum per-TP rate as a fraction of the line rate.",
                          DoubleValue(0.002),
                          MakeDoubleAccessor(&UbHostSprayCc::m_minRateFraction),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("WindowRttFactor",
                          "In-flight cap = max(2 MTU, rate x (base RTT + CapSlack) x factor).",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&UbHostSprayCc::m_windowRttFactor),
                          MakeDoubleChecker<double>(0.5))
            .AddAttribute("CapSlack",
                          "Fixed delay slack added to the base RTT in the in-flight cap.",
                          TimeValue(NanoSeconds(8000)),
                          MakeTimeAccessor(&UbHostSprayCc::m_capSlack),
                          MakeTimeChecker(TimeStep(0)))
            .AddAttribute("Coupled",
                          "Couple the additive increase across TPs of one (src,dst,priority) "
                          "group: the group gains AI per period, split by plane rate share.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&UbHostSprayCc::m_coupled),
                          MakeBooleanChecker())
            .AddAttribute("Pacing",
                          "Pace each TP at its rate (true) or only enforce the in-flight cap "
                          "rate x base RTT x WindowRttFactor (false, window mode).",
                          BooleanValue(false),
                          MakeBooleanAccessor(&UbHostSprayCc::m_pacing),
                          MakeBooleanChecker())
            .AddAttribute("HyperIncrease",
                          "Grow the additive increase geometrically over consecutive unmarked "
                          "periods (x2 per period, up to x16).",
                          BooleanValue(false),
                          MakeBooleanAccessor(&UbHostSprayCc::m_hyperIncrease),
                          MakeBooleanChecker());
    return tid;
}

UbHostSprayCc::UbHostSprayCc() = default;

UbHostSprayCc::~UbHostSprayCc()
{
    m_pacingEvent.Cancel();
    LeaveGroup();
}

std::map<UbHostSprayCc::GroupKey, std::vector<UbHostSprayCc*>>&
UbHostSprayCc::Groups()
{
    static std::map<GroupKey, std::vector<UbHostSprayCc*>> groups;
    return groups;
}

void
UbHostSprayCc::LeaveGroup()
{
    if (!m_inGroup)
    {
        return;
    }
    auto it = Groups().find(m_groupKey);
    if (it != Groups().end())
    {
        auto& v = it->second;
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
        if (v.empty())
        {
            Groups().erase(it);
        }
    }
    m_inGroup = false;
}

void
UbHostSprayCc::DoDispose()
{
    m_pacingEvent.Cancel();
    LeaveGroup();
    m_tp = nullptr;
    m_port = nullptr;
    UbCongestionControl::DoDispose();
}

double
UbHostSprayCc::LineRateBps() const
{
    return m_port == nullptr ? 400e9 : static_cast<double>(m_port->GetDataRate().GetBitRate());
}

void
UbHostSprayCc::OnTpAttached(Ptr<UbTransportChannel> tp)
{
    m_tp = tp;
    LeaveGroup();
    m_port = nullptr;
    if (tp != nullptr)
    {
        m_port = DynamicCast<UbPort>(NodeList::GetNode(tp->GetSrc())->GetDevice(tp->GetSport()));
        m_groupKey = GroupKey{tp->GetSrc(), tp->GetDest(), tp->GetPriority()};
        Groups()[m_groupKey].push_back(this);
        m_inGroup = true;
    }
    m_rateBps = LineRateBps();
    m_nextSend = Simulator::Now();
}

uint64_t
UbHostSprayCc::GetWindowCapBytes() const
{
    // In-flight cap: a few "BDPs" at the current rate. The fixed slack absorbs ACK-path delay
    // (e.g. ACKs queued behind the peer's own data) so short-RTT flows are not throttled, while a
    // TP feeding a slow remote bottleneck still stops early and lets TPG dispatch move load.
    const double base = m_minRtt.IsStrictlyPositive() ? m_minRtt.GetSeconds() : 2e-6;
    const double cap = m_rateBps / 8.0 * (base + m_capSlack.GetSeconds()) * m_windowRttFactor;
    return std::max<uint64_t>(static_cast<uint64_t>(cap), 2 * UB_MTU_BYTE);
}

uint32_t
UbHostSprayCc::GetRestCwnd()
{
    if (!m_congestionCtrlEnabled)
    {
        return UINT32_MAX;
    }
    const uint64_t cap = GetWindowCapBytes();
    return cap > m_inflight ? static_cast<uint32_t>(std::min<uint64_t>(cap - m_inflight, UINT32_MAX))
                            : 0;
}

void
UbHostSprayCc::SchedulePacingWakeup()
{
    if (m_port == nullptr || m_pacingEvent.IsPending())
    {
        return;
    }
    const Time now = Simulator::Now();
    m_pacingEvent = Simulator::Schedule(m_nextSend > now ? m_nextSend - now : Time(0),
                                        &UbHostSprayCc::HandlePacingWakeup,
                                        this);
}

void
UbHostSprayCc::HandlePacingWakeup()
{
    if (m_port != nullptr)
    {
        m_port->TriggerTransmit();
    }
}

void
UbHostSprayCc::OnSenderRetransmissionRollback(uint32_t retransmitBytes)
{
    // A retransmission timeout declares the unacknowledged bytes lost: they are no longer in
    // flight. Without this the window stays full of packets that will never be acknowledged and
    // the retransmission itself is blocked by the window (deadlock after a hard link-down).
    if (!m_congestionCtrlEnabled)
    {
        return;
    }
    uint64_t removed = 0;
    while (!m_sent.empty() && removed < retransmitBytes)
    {
        removed += m_sent.back().size;
        m_sent.pop_back();
    }
    m_inflight = m_inflight > removed ? m_inflight - removed : 0;
    if (m_sent.empty())
    {
        m_inflight = 0;
    }
}

void
UbHostSprayCc::OnSenderRetransmissionPacketSent(uint32_t psn, uint32_t size)
{
    OnSenderDataPacketSent(psn, size);
}

bool
UbHostSprayCc::IsCcLimited(uint32_t bytes)
{
    if (!m_congestionCtrlEnabled)
    {
        return false;
    }
    if (m_inflight > 0 && m_inflight + bytes > GetWindowCapBytes())
    {
        return true; // released by the next ACK
    }
    if (m_pacing && Simulator::Now() < m_nextSend)
    {
        SchedulePacingWakeup();
        return true;
    }
    return false;
}

void
UbHostSprayCc::OnSenderPrepareIpBasedNetworkHeader(UbIpBasedNetworkHeader& header)
{
    if (!m_congestionCtrlEnabled)
    {
        return;
    }
    header.SetMode(kSprayCcMode);
    header.SetLocation(false);
    header.SetFecn(0);
}

void
UbHostSprayCc::OnSenderDataPacketSent(uint32_t psn, uint32_t size)
{
    if (!m_congestionCtrlEnabled)
    {
        return;
    }
    const Time now = Simulator::Now();
    m_sent.push_back({psn & kPsnMask, size, now});
    m_inflight += size;
    if (m_nextSend < now)
    {
        m_nextSend = now;
    }
    m_nextSend += Seconds(static_cast<double>(size) * 8.0 / std::max(m_rateBps, 1.0));
}

uint32_t
UbHostSprayCc::EncodeFeedback(uint32_t marked, uint32_t total)
{
    return (std::min<uint32_t>(marked, 0xFFFF) << 16) | std::min<uint32_t>(total, 0xFFFF);
}

void
UbHostSprayCc::DecodeFeedback(uint32_t raw, uint32_t& marked, uint32_t& total)
{
    marked = raw >> 16;
    total = raw & 0xFFFF;
}

void
UbHostSprayCc::OnReceiverDataPacketReceived(uint64_t psn, uint32_t size, UbIpBasedNetworkHeader header)
{
    (void)psn;
    (void)size;
    if (!m_congestionCtrlEnabled)
    {
        return;
    }
    ++m_rxTotal;
    if (header.GetMode() == kSprayCcMode && header.GetFecn() == kSprayCcMarked)
    {
        ++m_rxMarked;
    }
}

UbCongestionExtTph
UbHostSprayCc::OnReceiverPrepareAckCongestionHeader(uint64_t psnStart, uint64_t psnEnd)
{
    (void)psnStart;
    (void)psnEnd;
    UbCongestionExtTph cetph;
    cetph.SetRawBytes4to7(EncodeFeedback(m_rxMarked, m_rxTotal));
    m_rxMarked = 0;
    m_rxTotal = 0;
    return cetph;
}

TpOpcode
UbHostSprayCc::GetAckOpcode() const
{
    return m_congestionCtrlEnabled ? TpOpcode::TP_OPCODE_ACK_WITH_CETPH
                                   : TpOpcode::TP_OPCODE_ACK_WITHOUT_CETPH;
}

double
UbHostSprayCc::GroupRateBps() const
{
    double total = m_rateBps;
    if (m_coupled && m_inGroup)
    {
        const auto it = Groups().find(m_groupKey);
        if (it != Groups().end() && it->second.size() > 1)
        {
            total = 0.0;
            for (const UbHostSprayCc* m : it->second)
            {
                total += m->m_rateBps;
            }
        }
    }
    return std::max(total, 1.0);
}

void
UbHostSprayCc::OnPeriodEnd()
{
    const double fraction =
        m_periodTotal == 0 ? 0.0 : static_cast<double>(m_periodMarked) / m_periodTotal;
    m_alpha = (1.0 - m_gain) * m_alpha + m_gain * fraction;
    const double line = LineRateBps();
    if (m_periodMarked > 0)
    {
        m_rateBps *= (1.0 - m_alpha / 2.0);
        m_unmarkedPeriods = 0;
    }
    else if (m_periodTotal > 0)
    {
        ++m_unmarkedPeriods;
        const double boost =
            m_hyperIncrease ? std::min<double>(1u << std::min<uint32_t>(m_unmarkedPeriods - 1, 4), 16)
                            : 1.0;
        // Group gains AI*line per period, split by this plane's rate share.
        const double share = m_rateBps / GroupRateBps();
        m_rateBps += m_aiFraction * line * share * boost;
    }
    m_rateBps = std::clamp(m_rateBps, m_minRateFraction * line, line);
    m_periodMarked = 0;
    m_periodTotal = 0;
}

void
UbHostSprayCc::OnSenderCongestionNotification(TpOpcode opcode, uint32_t psn, UbCongestionExtTph header)
{
    if (!m_congestionCtrlEnabled || opcode != TpOpcode::TP_OPCODE_ACK_WITH_CETPH)
    {
        return;
    }
    const uint32_t ackPsn = psn & kPsnMask;
    uint32_t marked = 0;
    uint32_t total = 0;
    DecodeFeedback(header.GetRawBytes4to7(), marked, total);
    m_periodMarked += marked;
    m_periodTotal += total;

    uint32_t acked = 0;
    bool sampled = false;
    while (!m_sent.empty() && PsnLeq(m_sent.front().psn, ackPsn))
    {
        if (!sampled)
        {
            // RTT of the oldest newly acknowledged packet (the one that held back the cumulative
            // ACK under spray reordering, i.e. the slowest path).
            const Time sample = Simulator::Now() - m_sent.front().sendTime;
            m_srtt = m_srtt.IsStrictlyPositive() ? (m_srtt * 7 + sample) / 8 : sample;
            m_minRtt = m_minRtt.IsStrictlyPositive() ? std::min(m_minRtt, sample) : sample;
            sampled = true;
        }
        acked += m_sent.front().size;
        m_sent.pop_front();
    }
    m_inflight = acked >= m_inflight ? 0 : m_inflight - acked;

    // Fixed-period control loop (not per RTT): flows with different RTTs adapt equally often.
    const Time now = Simulator::Now();
    if (!m_lastUpdate.IsStrictlyPositive())
    {
        m_lastUpdate = now;
    }
    else if (now - m_lastUpdate >= m_period)
    {
        m_lastUpdate = now;
        OnPeriodEnd();
    }
}

TypeId
UbSwitchSprayCc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::UbSwitchSprayCc")
            .SetParent<UbCongestionControl>()
            .SetGroupName("UnifiedBus")
            .AddConstructor<UbSwitchSprayCc>()
            .AddAttribute("KDelay",
                          "Mark CE when the output queue exceeds this drain time at the port's "
                          "current rate.",
                          TimeValue(NanoSeconds(2000)),
                          MakeTimeAccessor(&UbSwitchSprayCc::m_kDelay),
                          MakeTimeChecker(TimeStep(0)))
            .AddAttribute("EdgeOnly",
                          "Mark only on device-facing (last-hop) outputs. In-fabric congestion is "
                          "left to packet spraying / adaptive routing, so multi-hop flows are not "
                          "penalised for transient fabric queues (parking-lot unfairness).",
                          BooleanValue(true),
                          MakeBooleanAccessor(&UbSwitchSprayCc::m_edgeOnly),
                          MakeBooleanChecker());
    return tid;
}

UbSwitchSprayCc::UbSwitchSprayCc() = default;

void
UbSwitchSprayCc::OnSwitchAttached(Ptr<UbSwitch> sw)
{
    UbCongestionControl::OnSwitchAttached(sw);
    m_switch = sw;
}

void
UbSwitchSprayCc::OnSwitchPostEnqueue(uint32_t inPort, uint32_t outPort, Ptr<Packet> p)
{
    (void)inPort;
    if (!m_congestionCtrlEnabled || p == nullptr || m_switch == nullptr)
    {
        return;
    }
    UbDatalinkHeader dlHeader;
    p->PeekHeader(dlHeader);
    if (!dlHeader.IsPacketIpv4Header())
    {
        return;
    }
    Ptr<Node> node = m_switch->GetObject<Node>();
    Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(outPort));
    if (port == nullptr)
    {
        return;
    }
    if (m_edgeOnly)
    {
        if (outPort >= m_deviceFacing.size())
        {
            m_deviceFacing.resize(node->GetNDevices(), -1);
        }
        int8_t& facing = m_deviceFacing[outPort];
        if (facing < 0)
        {
            Ptr<UbLink> link = DynamicCast<UbLink>(port->GetChannel());
            Ptr<UbPort> peer = link == nullptr ? nullptr : link->GetDestination(port);
            Ptr<UbSwitch> peerSw =
                peer == nullptr ? nullptr : peer->GetNode()->GetObject<UbSwitch>();
            facing = (peerSw != nullptr && peerSw->GetNodeType() == UB_DEVICE) ? 1 : 0;
        }
        if (facing == 0)
        {
            return;
        }
    }
    const uint64_t backlog = m_switch->GetQueueManager()->GetOutPortQueueBacklogBytes(outPort);
    const uint64_t bps = std::max<uint64_t>(port->GetDataRate().GetBitRate(), 1);
    // Compare backlog * 8 / rate > kDelay without floating point: backlog * 8e12 > kDelay_ps * bps.
    const long double lhs = static_cast<long double>(backlog) * 8.0e12L;
    const long double rhs = static_cast<long double>(m_kDelay.GetPicoSeconds()) * bps;
    if (lhs <= rhs)
    {
        return;
    }
    UbDatalinkPacketHeader dlPacketHeader;
    UbIpBasedNetworkHeader networkHeader;
    p->RemoveHeader(dlPacketHeader);
    p->RemoveHeader(networkHeader);
    if (networkHeader.GetMode() == kSprayCcMode)
    {
        networkHeader.SetFecn(kSprayCcMarked);
    }
    p->AddHeader(networkHeader);
    p->AddHeader(dlPacketHeader);
}

} // namespace ns3
