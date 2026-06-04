#include "bcc-tag.h"

namespace ns3 {

BccTag::BccTag()
    : m_state(NC),
      m_switchId(0),
      m_egressPort(0),
      m_queueLen(0),
      m_queueSlope(0.0),
      m_linkUtilization(0.0),
      m_timestampNs(0) {}

TypeId BccTag::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::BccTag").SetParent<Tag>().AddConstructor<BccTag>();
    return tid;
}

TypeId BccTag::GetInstanceTypeId(void) const { return GetTypeId(); }

uint32_t BccTag::GetSerializedSize(void) const {
    return sizeof(m_state) + sizeof(m_switchId) + sizeof(m_egressPort) + sizeof(m_queueLen) +
           sizeof(m_queueSlope) + sizeof(m_linkUtilization) + sizeof(m_timestampNs);
}

void BccTag::Serialize(TagBuffer i) const {
    i.WriteU8(m_state);
    i.WriteU32(m_switchId);
    i.WriteU32(m_egressPort);
    i.WriteU32(m_queueLen);
    i.WriteDouble(m_queueSlope);
    i.WriteDouble(m_linkUtilization);
    i.WriteU64(m_timestampNs);
}

void BccTag::Deserialize(TagBuffer i) {
    m_state = i.ReadU8();
    m_switchId = i.ReadU32();
    m_egressPort = i.ReadU32();
    m_queueLen = i.ReadU32();
    m_queueSlope = i.ReadDouble();
    m_linkUtilization = i.ReadDouble();
    m_timestampNs = i.ReadU64();
}

void BccTag::Print(std::ostream &os) const {
    os << "state=" << StateToString(m_state) << ",switch=" << m_switchId
       << ",egressPort=" << m_egressPort << ",queueLen=" << m_queueLen
       << ",queueSlope=" << m_queueSlope << ",linkUtilization=" << m_linkUtilization
       << ",timestampNs=" << m_timestampNs;
}

void BccTag::SetState(uint8_t state) { m_state = state; }
uint8_t BccTag::GetState(void) const { return m_state; }

void BccTag::SetSwitchId(uint32_t switchId) { m_switchId = switchId; }
uint32_t BccTag::GetSwitchId(void) const { return m_switchId; }

void BccTag::SetEgressPort(uint32_t egressPort) { m_egressPort = egressPort; }
uint32_t BccTag::GetEgressPort(void) const { return m_egressPort; }

void BccTag::SetQueueLen(uint32_t queueLen) { m_queueLen = queueLen; }
uint32_t BccTag::GetQueueLen(void) const { return m_queueLen; }

void BccTag::SetQueueSlope(double queueSlope) { m_queueSlope = queueSlope; }
double BccTag::GetQueueSlope(void) const { return m_queueSlope; }

void BccTag::SetLinkUtilization(double linkUtilization) {
    m_linkUtilization = linkUtilization;
}
double BccTag::GetLinkUtilization(void) const { return m_linkUtilization; }

void BccTag::SetTimestampNs(uint64_t timestampNs) { m_timestampNs = timestampNs; }
uint64_t BccTag::GetTimestampNs(void) const { return m_timestampNs; }

const char *BccTag::StateToString(uint8_t state) {
    switch (state) {
        case NC:
            return "NC";
        case PC:
            return "PC";
        case TC:
            return "TC";
        case TU:
            return "TU";
        default:
            return "UNKNOWN";
    }
}

}  // namespace ns3
