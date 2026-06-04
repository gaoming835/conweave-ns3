#ifndef BCC_TAG_H
#define BCC_TAG_H

#include "ns3/tag.h"
#include "ns3/type-id.h"

namespace ns3 {

class BccTag : public Tag {
   public:
    enum State : uint8_t {
        NC = 0,
        PC = 1,
        TC = 2,
        TU = 3,
    };

    BccTag();

    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    virtual void Print(std::ostream &os) const;
    virtual uint32_t GetSerializedSize(void) const;
    virtual void Serialize(TagBuffer i) const;
    virtual void Deserialize(TagBuffer i);

    void SetState(uint8_t state);
    uint8_t GetState(void) const;

    void SetSwitchId(uint32_t switchId);
    uint32_t GetSwitchId(void) const;

    void SetEgressPort(uint32_t egressPort);
    uint32_t GetEgressPort(void) const;

    void SetQueueLen(uint32_t queueLen);
    uint32_t GetQueueLen(void) const;

    void SetQueueSlope(double queueSlope);
    double GetQueueSlope(void) const;

    void SetLinkUtilization(double linkUtilization);
    double GetLinkUtilization(void) const;

    void SetTimestampNs(uint64_t timestampNs);
    uint64_t GetTimestampNs(void) const;

    static const char *StateToString(uint8_t state);

   private:
    uint8_t m_state;
    uint32_t m_switchId;
    uint32_t m_egressPort;
    uint32_t m_queueLen;
    double m_queueSlope;
    double m_linkUtilization;
    uint64_t m_timestampNs;
};

}  // namespace ns3

#endif  // BCC_TAG_H
