#ifndef SWITCH_NODE_H
#define SWITCH_NODE_H

#include <ns3/node.h>

#include <unordered_map>
#include <unordered_set>

#include "bcc-tag.h"
#include "dcp-tag.h"
#include "qbb-net-device.h"
#include "switch-mmu.h"

namespace ns3 {

class Packet;

struct BccPortState {
    uint32_t queue_len{0};
    uint32_t last_queue_len{0};
    uint64_t last_update_time{0};
    double queue_slope{0.0};
    double link_utilization{0.0};
    uint8_t state{BccTag::NC};
    uint64_t last_tx_bytes{0};
};

class SwitchNode : public Node {
    static const unsigned qCnt = 8;    // Number of queues/priorities used
    static const unsigned pCnt = 128;  // port 0 is not used so + 1	// Number of ports used
    uint32_t m_ecmpSeed;
    std::unordered_map<uint32_t, std::vector<int> >
        m_rtTable;  // map from ip address (u32) to possible ECMP port (index of dev)

    // monitor uplinks
    uint64_t m_txBytes[pCnt];  // counter of tx bytes, for HPCC

   protected:
    bool m_ecnEnabled;
    uint32_t m_ccMode;
    uint32_t m_ackHighPrio;  // set high priority for ACK/NACK
    bool m_bccMarkingEnabled;
    double m_bccUtilizationThreshold;
    double m_bccSlopeThreshold;

   private:
    int GetOutDev(Ptr<Packet>, CustomHeader &ch);
    void SendToDev(Ptr<Packet> p, CustomHeader &ch);
    void SendToDevContinue(Ptr<Packet> p, CustomHeader &ch);
    static uint32_t EcmpHash(const uint8_t *key, size_t len, uint32_t seed);
    void CheckAndSendPfc(uint32_t inDev, uint32_t qIndex);
    void CheckAndSendResume(uint32_t inDev, uint32_t qIndex);

    /* Sending packet to Egress port */
    void DoSwitchSend(Ptr<Packet> p, CustomHeader &ch, uint32_t outDev, uint32_t qIndex);
    uint8_t ClassifyDcpPacket(Ptr<Packet> p, const CustomHeader &ch, DcpTag *tag) const;
    uint32_t GetDcpDataQueueIndex(const CustomHeader &ch, uint32_t qIndex) const;
    Ptr<Packet> CreateDcpHoPacket(Ptr<Packet> p, const DcpTag &dataTag) const;
    void UpdateDcpQueueStats(uint32_t outDev, uint32_t qIndex);
    void RecordDcpQueueDrop(uint8_t dcpType);
    void UpdateBccStateAndTag(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);
    uint8_t ClassifyBccState(uint32_t ifIndex, const BccPortState &state) const;

    /*----- Load balancer -----*/
    // Flow ECMP (lb_mode = 0)
    uint32_t DoLbFlowECMP(Ptr<const Packet> p, const CustomHeader &ch,
                          const std::vector<int> &nexthops);
    // DRILL (lb_mode = 2)
    uint32_t DoLbDrill(Ptr<const Packet> p, const CustomHeader &ch,
                       const std::vector<int> &nexthops);     // choose egress port
    uint32_t m_drill_candidate;                               // always 2 (power of two)
    std::map<uint32_t, uint32_t> m_previousBestInterfaceMap;  // <dip, previousBestInterface>
    uint32_t CalculateInterfaceLoad(uint32_t interface);      // Get the load of a interface
    // Conga (lb_mode = 3)
    uint32_t DoLbConga(Ptr<Packet> p, CustomHeader &ch, const std::vector<int> &nexthops);
    // Conga (lb_mode = 6)
    uint32_t DoLbLetflow(Ptr<Packet> p, CustomHeader &ch, const std::vector<int> &nexthops);
    // ConWeave (lb_mode = 9)
    uint32_t DoLbConWeave(Ptr<const Packet> p, const CustomHeader &ch,
                           const std::vector<int> &nexthops);  // dummy
    // Template load balancer (lb_mode = 10)
    uint32_t DoLbTemplate(Ptr<const Packet> p, const CustomHeader &ch,
                          const std::vector<int> &nexthops);

   public:
    enum DcpAdmissionAction {
        DCP_ADMISSION_BYPASS = 0,
        DCP_ADMISSION_ENQUEUE,
        DCP_ADMISSION_ENQUEUE_CONTROL,
        DCP_ADMISSION_TRIM_TO_HO,
        DCP_ADMISSION_DROP_NON,
        DCP_ADMISSION_DROP_ACK
    };

    // Ptr<BroadcomNode> m_broadcom;
    Ptr<SwitchMmu> m_mmu;
    bool m_isToR;                                 // true if ToR switch
    std::unordered_set<uint32_t> m_isToR_hostIP;  // host's IP connected to this ToR

    static TypeId GetTypeId(void);
    SwitchNode();
    void SetEcmpSeed(uint32_t seed);
    void AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx);
    void ClearTable();
    bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch);
    void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);
    uint64_t GetTxBytesOutDev(uint32_t outdev);
    const BccPortState &GetBccPortState(uint32_t outdev) const;
    static uint8_t TransitionBccState(uint8_t previousState, uint32_t queueLen, uint32_t k1,
                                      uint32_t k2, double queueSlope, double linkUtilization,
                                      double slopeThreshold, double utilizationThreshold);
    static const char *BccStateToString(uint8_t state);
    static DcpAdmissionAction EvaluateDcpAdmission(uint8_t dcpType, bool enableDcp,
                                                   bool dataQueueCandidate,
                                                   uint64_t dataQueueBytes,
                                                   uint32_t packetSize,
                                                   uint32_t trimThreshold);
    static const char *DcpAdmissionActionToString(DcpAdmissionAction action);

   private:
    BccPortState m_bccPortState[pCnt];
};

} /* namespace ns3 */

#endif /* SWITCH_NODE_H */
