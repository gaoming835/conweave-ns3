#include "dcp-tag.h"

namespace ns3 {

namespace {
const uint8_t kDcpTypeShift = 2;
const uint8_t kDcpTypeMask = 0x0c;
const uint8_t kDcpTypeValueMask = 0x03;
}  // namespace

DcpTag::DcpTag()
    : m_packetType(DCP_NON),
      m_flowId(-1),
      m_psn(0),
      m_src("0.0.0.0"),
      m_dst("0.0.0.0"),
      m_srcPort(0),
      m_dstPort(0),
      m_pg(0) {}

TypeId DcpTag::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::DcpTag").SetParent<Tag>().AddConstructor<DcpTag>();
    return tid;
}

TypeId DcpTag::GetInstanceTypeId(void) const { return GetTypeId(); }

uint32_t DcpTag::GetSerializedSize(void) const {
    return sizeof(m_packetType) + sizeof(uint32_t) + sizeof(m_psn) + sizeof(uint32_t) +
           sizeof(uint32_t) + sizeof(m_srcPort) + sizeof(m_dstPort) + sizeof(m_pg);
}

void DcpTag::Serialize(TagBuffer i) const {
    i.WriteU8(m_packetType);
    i.WriteU32((uint32_t)m_flowId);
    i.WriteU32(m_psn);
    i.WriteU32(m_src.Get());
    i.WriteU32(m_dst.Get());
    i.WriteU16(m_srcPort);
    i.WriteU16(m_dstPort);
    i.WriteU16(m_pg);
}

void DcpTag::Deserialize(TagBuffer i) {
    m_packetType = i.ReadU8();
    m_flowId = (int32_t)i.ReadU32();
    m_psn = i.ReadU32();
    m_src = Ipv4Address(i.ReadU32());
    m_dst = Ipv4Address(i.ReadU32());
    m_srcPort = i.ReadU16();
    m_dstPort = i.ReadU16();
    m_pg = i.ReadU16();
}

void DcpTag::Print(std::ostream &os) const {
    os << "type=" << PacketTypeToString(m_packetType) << ",flow=" << m_flowId
       << ",psn=" << m_psn << ",src=" << m_src << ":" << m_srcPort << ",dst=" << m_dst
       << ":" << m_dstPort << ",pg=" << m_pg;
}

void DcpTag::SetPacketType(uint8_t type) { m_packetType = type; }
uint8_t DcpTag::GetPacketType(void) const { return m_packetType; }

void DcpTag::SetFlowId(int32_t flowId) { m_flowId = flowId; }
int32_t DcpTag::GetFlowId(void) const { return m_flowId; }

void DcpTag::SetPsn(uint32_t psn) { m_psn = psn; }
uint32_t DcpTag::GetPsn(void) const { return m_psn; }

void DcpTag::SetSrc(Ipv4Address src) { m_src = src; }
Ipv4Address DcpTag::GetSrc(void) const { return m_src; }

void DcpTag::SetDst(Ipv4Address dst) { m_dst = dst; }
Ipv4Address DcpTag::GetDst(void) const { return m_dst; }

void DcpTag::SetSrcPort(uint16_t srcPort) { m_srcPort = srcPort; }
uint16_t DcpTag::GetSrcPort(void) const { return m_srcPort; }

void DcpTag::SetDstPort(uint16_t dstPort) { m_dstPort = dstPort; }
uint16_t DcpTag::GetDstPort(void) const { return m_dstPort; }

void DcpTag::SetPg(uint16_t pg) { m_pg = pg; }
uint16_t DcpTag::GetPg(void) const { return m_pg; }

void DcpTag::SetOriginalData(int32_t flowId, uint32_t psn, Ipv4Address src, Ipv4Address dst,
                             uint16_t srcPort, uint16_t dstPort, uint16_t pg) {
    m_flowId = flowId;
    m_psn = psn;
    m_src = src;
    m_dst = dst;
    m_srcPort = srcPort;
    m_dstPort = dstPort;
    m_pg = pg;
}

uint8_t DcpTag::PreserveEcnAndSetDcpType(uint8_t tos, uint8_t type) {
    return (tos & ~kDcpTypeMask) | ((type & kDcpTypeValueMask) << kDcpTypeShift);
}

void DcpTag::SetDcpTypeInIpHeader(Ipv4Header &header, uint8_t type) {
    header.SetTos(PreserveEcnAndSetDcpType(header.GetTos(), type));
}

uint8_t DcpTag::GetDcpTypeFromIpHeader(const Ipv4Header &header) {
    return GetDcpTypeFromTos(header.GetTos());
}

uint8_t DcpTag::GetDcpTypeFromTos(uint8_t tos) {
    return (tos & kDcpTypeMask) >> kDcpTypeShift;
}

const char *DcpTag::PacketTypeToString(uint8_t type) {
    switch (type) {
        case DCP_NON:
            return "DCP_NON";
        case DCP_ACK:
            return "DCP_ACK";
        case DCP_DATA:
            return "DCP_DATA";
        case DCP_HO:
            return "DCP_HO";
        default:
            return "DCP_UNKNOWN";
    }
}

}  // namespace ns3
