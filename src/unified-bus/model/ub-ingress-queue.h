// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_INGRESS_QUEUE_H
#define UB_INGRESS_QUEUE_H

#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ub-small-fifo-queue.h"

#include <cstddef>

namespace ns3 {

enum class IngressQueueType {
    VOQ,        // Virtual Output Queue - 转发数据包队列
    TP,         // Transport Channel - 传输层可靠通道
    BASE,       // 基类默认值 - 不应出现在运行时
    CTP         // Compact Transport Protocol - 本地完整报文发送队列
};

/**
 * @brief port的收包缓存队列
 */
class UbIngressQueue : public Object {
public:
    UbIngressQueue();
    virtual ~UbIngressQueue();
    static TypeId GetTypeId(void);

    virtual IngressQueueType GetIngressQueueType()
    {
        return IngressQueueType::BASE;
    }

    virtual bool IsEmpty();
    virtual bool IsLimited() { return false; }
    virtual Ptr<Packet> GetNextPacket();
    virtual uint32_t GetNextPacketSize();
    void SetInPortId(uint32_t inPortId) { m_inPortId = inPortId; }
    void SetIngressPriority(uint32_t priority) { m_ingressPriority = priority; }
    void SetOutPortId(uint32_t outPortId) { m_outPortId = outPortId; }
    uint32_t GetInPortId() { return m_inPortId; }
    uint32_t GetIngressPriority() { return m_ingressPriority; }
    uint32_t GetOutPortId() { return m_outPortId; }
    Time GetHeadArrivalTime() { return m_headArrivalTime; }
    bool IsControlFrame();
    bool IsForwardedDataPacket();
    bool IsGeneratedDataPacket();

protected:
    Time m_headArrivalTime = Seconds(0);

private:
    uint32_t m_ingressPriority;
    uint32_t m_inPortId;
    uint32_t m_outPortId;
};

/**
 * @brief VOQ (Virtual Output Queue) implementation of ingress queue
 */
class UbPacketQueue final : public UbIngressQueue {
public:
    UbPacketQueue();
    ~UbPacketQueue() final;
    static TypeId GetTypeId(void);

    bool IsEmpty() override;
    Ptr<Packet> GetNextPacket() override;
    Ptr<Packet> Front();
    void Pop();
    void Push(Ptr<Packet> p);
    IngressQueueType GetIngressQueueType() override;
    uint32_t GetNextPacketSize() override;
    size_t GetPacketCount() const { return m_packetCount; }

private:
    Ptr<Packet> m_frontPacket;
    UbSmallFifoQueue<Ptr<Packet>, 1> m_overflowQueue;
    size_t m_packetCount = 0;
    IngressQueueType m_ingressQueueType = IngressQueueType::VOQ;
};

} // namespace ns3

#endif /* UB_INGRESS_QUEUE_H */
