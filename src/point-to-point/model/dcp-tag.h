#ifndef DCP_TAG_H
#define DCP_TAG_H

#include "ns3/ipv4-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/tag.h"
#include "ns3/type-id.h"

namespace ns3 {

class DcpTag : public Tag {
   public:
    enum PacketType : uint8_t {
        DCP_NON = 0,
        DCP_ACK = 1,
        DCP_DATA = 2,
        DCP_HO = 3,
    };

    DcpTag();

    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    virtual void Print(std::ostream &os) const;
    virtual uint32_t GetSerializedSize(void) const;
    virtual void Serialize(TagBuffer i) const;
    virtual void Deserialize(TagBuffer i);

    void SetPacketType(uint8_t type);
    uint8_t GetPacketType(void) const;

    void SetFlowId(int32_t flowId);
    int32_t GetFlowId(void) const;

    void SetPsn(uint32_t psn);
    uint32_t GetPsn(void) const;

    void SetSrc(Ipv4Address src);
    Ipv4Address GetSrc(void) const;

    void SetDst(Ipv4Address dst);
    Ipv4Address GetDst(void) const;

    void SetSrcPort(uint16_t srcPort);
    uint16_t GetSrcPort(void) const;

    void SetDstPort(uint16_t dstPort);
    uint16_t GetDstPort(void) const;

    void SetPg(uint16_t pg);
    uint16_t GetPg(void) const;

    void SetOriginalData(int32_t flowId, uint32_t psn, Ipv4Address src, Ipv4Address dst,
                         uint16_t srcPort, uint16_t dstPort, uint16_t pg);

    static uint8_t PreserveEcnAndSetDcpType(uint8_t tos, uint8_t type);
    static void SetDcpTypeInIpHeader(Ipv4Header &header, uint8_t type);
    static uint8_t GetDcpTypeFromIpHeader(const Ipv4Header &header);
    static uint8_t GetDcpTypeFromTos(uint8_t tos);
    static const char *PacketTypeToString(uint8_t type);

   private:
    uint8_t m_packetType;
    int32_t m_flowId;
    uint32_t m_psn;
    Ipv4Address m_src;
    Ipv4Address m_dst;
    uint16_t m_srcPort;
    uint16_t m_dstPort;
    uint16_t m_pg;
};

}  // namespace ns3

#endif  // DCP_TAG_H
