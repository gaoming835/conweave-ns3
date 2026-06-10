#include "switch-node.h"

#include "assert.h"
#include "ns3/boolean.h"
#include "ns3/conweave-routing.h"
#include "ns3/dcp-tag.h"
#include "ns3/double.h"
#include "ns3/flow-id-tag.h"
#include "ns3/flow-id-num-tag.h"
#include "ns3/flow-stat-tag.h"
#include "ns3/int-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"
#include "ns3/letflow-routing.h"
#include "ns3/packet.h"
#include "ns3/pause-header.h"
#include "ns3/settings.h"
#include "ns3/uinteger.h"
#include "ppp-header.h"
#include "qbb-net-device.h"

namespace ns3 {

namespace {
void SetPacketDcpTypeInIpHeader(Ptr<Packet> p, uint8_t type) {
    PppHeader ppp;
    Ipv4Header h;
    p->RemoveHeader(ppp);
    p->RemoveHeader(h);
    DcpTag::SetDcpTypeInIpHeader(h, type);
    p->AddHeader(h);
    p->AddHeader(ppp);
}
}  // namespace

TypeId SwitchNode::GetTypeId(void) {
    static TypeId tid =
        TypeId("ns3::SwitchNode")
            .SetParent<Node>()
            .AddConstructor<SwitchNode>()
            .AddAttribute("EcnEnabled", "Enable ECN marking.", BooleanValue(false),
                          MakeBooleanAccessor(&SwitchNode::m_ecnEnabled), MakeBooleanChecker())
            .AddAttribute("CcMode", "CC mode.", UintegerValue(0),
                          MakeUintegerAccessor(&SwitchNode::m_ccMode),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("AckHighPrio", "Set high priority for ACK/NACK or not", UintegerValue(0),
                          MakeUintegerAccessor(&SwitchNode::m_ackHighPrio),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("BccMarkingEnabled", "Enable BCC switch-side packet tagging.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&SwitchNode::m_bccMarkingEnabled),
                          MakeBooleanChecker())
            .AddAttribute("BccUtilizationThreshold", "BCC under-utilization threshold.",
                          DoubleValue(0.9),
                          MakeDoubleAccessor(&SwitchNode::m_bccUtilizationThreshold),
                          MakeDoubleChecker<double>())
            .AddAttribute("BccSlopeThreshold", "BCC transient congestion slope threshold.",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&SwitchNode::m_bccSlopeThreshold),
                          MakeDoubleChecker<double>());
    return tid;
}

SwitchNode::SwitchNode() {
    m_ecmpSeed = m_id;
    m_isToR = false;
    m_node_type = 1;
    m_isToR = false;
    m_drill_candidate = 2;
    m_bccMarkingEnabled = false;
    m_bccUtilizationThreshold = 0.9;
    m_bccSlopeThreshold = 1.0;
    m_mmu = CreateObject<SwitchMmu>();
    // Conga's Callback for switch functions
    m_mmu->m_congaRouting.SetSwitchSendCallback(MakeCallback(&SwitchNode::DoSwitchSend, this));
    m_mmu->m_congaRouting.SetSwitchSendToDevCallback(
        MakeCallback(&SwitchNode::SendToDevContinue, this));
    // ConWeave's Callback for switch functions
    m_mmu->m_conweaveRouting.SetSwitchSendCallback(MakeCallback(&SwitchNode::DoSwitchSend, this));
    m_mmu->m_conweaveRouting.SetSwitchSendToDevCallback(
        MakeCallback(&SwitchNode::SendToDevContinue, this));

    for (uint32_t i = 0; i < pCnt; i++) {
        m_txBytes[i] = 0;
        m_bccPortState[i] = BccPortState();
    }
}

/**
 * @brief Load Balancing
 */
uint32_t SwitchNode::DoLbFlowECMP(Ptr<const Packet> p, const CustomHeader &ch,
                                  const std::vector<int> &nexthops) {
    // pick one next hop based on hash
    union {
        uint8_t u8[4 + 4 + 2 + 2];
        uint32_t u32[3];
    } buf;
    buf.u32[0] = ch.sip;
    buf.u32[1] = ch.dip;
    if (ch.l3Prot == 0x6)
        buf.u32[2] = ch.tcp.sport | ((uint32_t)ch.tcp.dport << 16);
    else if (ch.l3Prot == 0x11)  // XXX RDMA traffic on UDP
        buf.u32[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
    else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD)  // ACK or NACK
        buf.u32[2] = ch.ack.sport | ((uint32_t)ch.ack.dport << 16);
    else {
        std::cout << "[ERROR] Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
                  << "Cannot support other protoocls than TCP/UDP (l3Prot:" << ch.l3Prot << ")"
                  << std::endl;
        assert(false && "Cannot support other protoocls than TCP/UDP");
    }

    uint32_t hashVal = EcmpHash(buf.u8, 12, m_ecmpSeed);
    uint32_t idx = hashVal % nexthops.size();
    return nexthops[idx];
}

/*-----------------CONGA-----------------*/
uint32_t SwitchNode::DoLbConga(Ptr<Packet> p, CustomHeader &ch, const std::vector<int> &nexthops) {
    return DoLbFlowECMP(p, ch, nexthops);  // flow ECMP (dummy)
}

/*-----------------Letflow-----------------*/
uint32_t SwitchNode::DoLbLetflow(Ptr<Packet> p, CustomHeader &ch,
                                 const std::vector<int> &nexthops) {
    if (m_isToR && nexthops.size() == 1) {
        if (m_isToR_hostIP.find(ch.sip) != m_isToR_hostIP.end() &&
            m_isToR_hostIP.find(ch.dip) != m_isToR_hostIP.end()) {
            return nexthops[0];  // intra-pod traffic
        }
    }

    /* ONLY called for inter-Pod traffic */
    uint32_t outPort = m_mmu->m_letflowRouting.RouteInput(p, ch);
    if (outPort == LETFLOW_NULL) {
        assert(nexthops.size() == 1);  // Receiver's TOR has only one interface to receiver-server
        outPort = nexthops[0];         // has only one option
    }
    assert(std::find(nexthops.begin(), nexthops.end(), outPort) !=
           nexthops.end());  // Result of Letflow cannot be found in nexthops
    return outPort;
}

/*-----------------DRILL-----------------*/
uint32_t SwitchNode::CalculateInterfaceLoad(uint32_t interface) {
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[interface]);
    NS_ASSERT_MSG(!!device && !!device->GetQueue(),
                  "Error of getting a egress queue for calculating interface load");
    return device->GetQueue()->GetNBytesTotal();  // also used in HPCC
}

uint32_t SwitchNode::DoLbDrill(Ptr<const Packet> p, const CustomHeader &ch,
                               const std::vector<int> &nexthops) {
    // find the Egress (output) link with the smallest local Egress Queue length
    uint32_t leastLoadInterface = 0;
    uint32_t leastLoad = std::numeric_limits<uint32_t>::max();
    auto rand_nexthops = nexthops;
    std::random_shuffle(rand_nexthops.begin(), rand_nexthops.end());

    std::map<uint32_t, uint32_t>::iterator itr = m_previousBestInterfaceMap.find(ch.dip);
    if (itr != m_previousBestInterfaceMap.end()) {
        leastLoadInterface = itr->second;
        leastLoad = CalculateInterfaceLoad(itr->second);
    }

    uint32_t sampleNum =
        m_drill_candidate < rand_nexthops.size() ? m_drill_candidate : rand_nexthops.size();
    for (uint32_t samplePort = 0; samplePort < sampleNum; samplePort++) {
        uint32_t sampleLoad = CalculateInterfaceLoad(rand_nexthops[samplePort]);
        if (sampleLoad < leastLoad) {
            leastLoad = sampleLoad;
            leastLoadInterface = rand_nexthops[samplePort];
        }
    }
    m_previousBestInterfaceMap[ch.dip] = leastLoadInterface;
    return leastLoadInterface;
}

/*------------------ConWeave Dummy ----------------*/
uint32_t SwitchNode::DoLbConWeave(Ptr<const Packet> p, const CustomHeader &ch,
                                  const std::vector<int> &nexthops) {
    return DoLbFlowECMP(p, ch, nexthops);  // flow ECMP (dummy)
}

/*------------------Template Load Balancer ----------------*/
uint32_t SwitchNode::DoLbTemplate(Ptr<const Packet> p, const CustomHeader &ch,
                                  const std::vector<int> &nexthops) {
    return DoLbFlowECMP(p, ch, nexthops);  // placeholder until a policy is implemented
}

uint64_t SwitchNode::GetArFlowKey(const CustomHeader &ch) const {
    uint64_t key = ((uint64_t)ch.udp.sport << 48) ^ ((uint64_t)ch.udp.dport << 32) ^
                   ((uint64_t)(ch.sip & 0xffff) << 16) ^ (uint64_t)(ch.dip & 0xffff);
    return key;
}

uint32_t SwitchNode::DoLbAdaptiveRouting(Ptr<const Packet> p, const CustomHeader &ch,
                                         const std::vector<int> &nexthops) {
    uint32_t bestLoad = std::numeric_limits<uint32_t>::max();
    std::vector<int> bestPorts;
    for (std::vector<int>::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
        uint32_t load = CalculateInterfaceLoad(*it);
        if (load < bestLoad) {
            bestLoad = load;
            bestPorts.clear();
            bestPorts.push_back(*it);
        } else if (load == bestLoad) {
            bestPorts.push_back(*it);
        }
    }

    uint32_t outDev = bestPorts[0];
    if (bestPorts.size() > 1) {
        union {
            uint8_t u8[4 + 4 + 2 + 2 + 4];
            uint32_t u32[4];
        } buf;
        buf.u32[0] = ch.sip;
        buf.u32[1] = ch.dip;
        buf.u32[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
        buf.u32[3] = ch.udp.seq;
        outDev = bestPorts[EcmpHash(buf.u8, 16, m_ecmpSeed) % bestPorts.size()];
    }

    Settings::ar_packets++;
    uint64_t flowKey = GetArFlowKey(ch);
    std::unordered_map<uint64_t, uint32_t>::iterator last = m_arLastOutDev.find(flowKey);
    if (last != m_arLastOutDev.end() && last->second != outDev) {
        Settings::ar_path_switches++;
    }
    m_arLastOutDev[flowKey] = outDev;

    uint64_t hopKey = ((uint64_t)m_id << 56) ^ (flowKey << 8) ^ (uint64_t)outDev;
    if (m_arUsedNextHopSet.insert(hopKey).second) {
        Settings::ar_used_next_hops++;
    }

    return outDev;
}
/*----------------------------------*/

void SwitchNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex) {
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
    bool pClasses[qCnt] = {0};
    m_mmu->GetPauseClasses(inDev, qIndex, pClasses);
    for (int j = 0; j < qCnt; j++) {
        if (pClasses[j]) {
            uint32_t paused_time = device->SendPfc(j, 0);
            m_mmu->SetPause(inDev, j, paused_time);
            m_mmu->m_pause_remote[inDev][j] = true;
            /** PAUSE SEND COUNT ++ */
        }
    }

    for (int j = 0; j < qCnt; j++) {
        if (!m_mmu->m_pause_remote[inDev][j]) continue;

        if (m_mmu->GetResumeClasses(inDev, j)) {
            device->SendPfc(j, 1);
            m_mmu->SetResume(inDev, j);
            m_mmu->m_pause_remote[inDev][j] = false;
        }
    }
}
void SwitchNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex) {
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
    if (m_mmu->GetResumeClasses(inDev, qIndex)) {
        device->SendPfc(qIndex, 1);
        m_mmu->SetResume(inDev, qIndex);
    }
}

/********************************************
 *              MAIN LOGICS                 *
 *******************************************/

// This function can only be called in switch mode
bool SwitchNode::SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet,
                                         CustomHeader &ch) {
    SendToDev(packet, ch);
    return true;
}

void SwitchNode::SendToDev(Ptr<Packet> p, CustomHeader &ch) {
    /** HIJACK: hijack the packet and run DoSwitchSend internally for Conga and ConWeave.
     * Note that DoLbConWeave() and DoLbConga() are flow-ECMP function for control packets
     * or intra-ToR traffic.
     */

    // Conga
    if (Settings::lb_mode == 3) {
        m_mmu->m_congaRouting.RouteInput(p, ch);
        return;
    }

    // ConWeave
    if (Settings::lb_mode == 9) {
        m_mmu->m_conweaveRouting.RouteInput(p, ch);
        return;
    }

    // Others
    SendToDevContinue(p, ch);
}

void SwitchNode::SendToDevContinue(Ptr<Packet> p, CustomHeader &ch) {
    int idx = GetOutDev(p, ch);
    if (idx >= 0) {
        NS_ASSERT_MSG(m_devices[idx]->IsLinkUp(),
                      "The routing table look up should return link that is up");

        // determine the qIndex
        uint32_t qIndex;
        if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE ||
            (m_ackHighPrio &&
             (ch.l3Prot == 0xFD ||
              ch.l3Prot == 0xFC))) {  // QCN or PFC or ACK/NACK, go highest priority
            qIndex = 0;               // high priority
        } else {
            qIndex = (ch.l3Prot == 0x06 ? 1 : ch.udp.pg);  // if TCP, put to queue 1. Otherwise, it
                                                           // would be 3 (refer to trafficgen)
        }

        DoSwitchSend(p, ch, idx, qIndex);  // m_devices[idx]->SwitchSend(qIndex, p, ch);
        return;
    }
    std::cout << "WARNING - Drop occurs in SendToDevContinue()" << std::endl;
    return;  // Drop otherwise
}

uint8_t SwitchNode::ClassifyDcpPacket(Ptr<Packet> p, const CustomHeader &ch, DcpTag *tag) const {
    if (!Settings::enable_dcp) {
        return DcpTag::DCP_NON;
    }
    DcpTag dcpTag;
    bool hasTag = p->PeekPacketTag(dcpTag);
    uint8_t ipType = DcpTag::GetDcpTypeFromTos(ch.m_tos);
    uint8_t dcpType = ipType;
    if (dcpType == DcpTag::DCP_NON && hasTag) {
        dcpType = dcpTag.GetPacketType();
    }
    if (dcpType == DcpTag::DCP_DATA && !hasTag && ch.l3Prot == 0x11) {
        dcpTag.SetPacketType(DcpTag::DCP_DATA);
        dcpTag.SetOriginalData(-1, ch.udp.seq, Ipv4Address(ch.sip), Ipv4Address(ch.dip),
                              ch.udp.sport, ch.udp.dport, ch.udp.pg);
        hasTag = true;
    }
    if (tag != NULL && hasTag) {
        *tag = dcpTag;
    }
    return dcpType;
}

uint32_t SwitchNode::GetDcpDataQueueIndex(const CustomHeader &ch, uint32_t qIndex) const {
    if (qIndex != 0) {
        return qIndex;
    }
    if (ch.l3Prot == 0x11) {
        return ch.udp.pg;
    }
    if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD) {
        return ch.ack.pg;
    }
    return qIndex;
}

Ptr<Packet> SwitchNode::CreateDcpHoPacket(Ptr<Packet> p, const DcpTag &dataTag) const {
    uint32_t minHeaderSize = CustomHeader::GetStaticWholeHeaderSize();
    uint32_t configuredSize = Settings::dcp_ho_size == 0 ? minHeaderSize : Settings::dcp_ho_size;
    uint32_t targetSize = std::max(minHeaderSize, configuredSize);
    uint32_t headerSize = std::min(p->GetSize(), targetSize);
    NS_ASSERT_MSG(headerSize > 0, "DCP HO packet cannot be created from an empty packet");
    std::vector<uint8_t> headerBytes(headerSize);
    p->CopyData(&headerBytes[0], headerSize);
    Ptr<Packet> ho = Create<Packet>(&headerBytes[0], headerSize);

    FlowIdTag flowTag;
    if (p->PeekPacketTag(flowTag)) {
        ho->AddPacketTag(flowTag);
    }
    FlowIDNUMTag flowNumTag;
    if (p->PeekPacketTag(flowNumTag)) {
        ho->AddPacketTag(flowNumTag);
    }
    FlowStatTag flowStatTag;
    if (p->PeekPacketTag(flowStatTag)) {
        ho->AddPacketTag(flowStatTag);
    }

    DcpTag hoTag = dataTag;
    hoTag.SetPacketType(DcpTag::DCP_HO);
    SetPacketDcpTypeInIpHeader(ho, DcpTag::DCP_HO);
    ho->AddPacketTag(hoTag);
    return ho;
}

void SwitchNode::UpdateDcpQueueStats(uint32_t outDev, uint32_t qIndex) {
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[outDev]);
    if (device == NULL || device->GetQueue() == NULL) {
        return;
    }
    Settings::control_queue_len =
        std::max<uint64_t>(Settings::control_queue_len, device->GetQueue()->GetNBytes(0));
    if (qIndex != 0) {
        Settings::data_queue_len =
            std::max<uint64_t>(Settings::data_queue_len,
                               m_mmu->GetEgressQueueBytes(outDev, qIndex));
    }
}

void SwitchNode::RecordDcpQueueDrop(uint8_t dcpType) {
    switch (dcpType) {
        case DcpTag::DCP_DATA:
            Settings::dcp_data_dropped++;
            Settings::dcp_data_queue_drops++;
            break;
        case DcpTag::DCP_ACK:
            Settings::dcp_ack_dropped++;
            Settings::dcp_data_queue_drops++;
            break;
        case DcpTag::DCP_HO:
            Settings::dcp_ho_dropped++;
            Settings::dcp_control_queue_drops++;
            break;
        default:
            break;
    }
}

SwitchNode::DcpAdmissionAction SwitchNode::EvaluateDcpAdmission(
    uint8_t dcpType, bool enableDcp, bool dataQueueCandidate, uint64_t dataQueueBytes,
    uint32_t packetSize, uint32_t trimThreshold) {
    if (!enableDcp) {
        return DCP_ADMISSION_BYPASS;
    }
    if (dcpType == DcpTag::DCP_HO) {
        return DCP_ADMISSION_ENQUEUE_CONTROL;
    }
    if (!dataQueueCandidate) {
        return DCP_ADMISSION_BYPASS;
    }
    if (dataQueueBytes + packetSize <= trimThreshold) {
        return DCP_ADMISSION_ENQUEUE;
    }
    if (dcpType == DcpTag::DCP_DATA) {
        return DCP_ADMISSION_TRIM_TO_HO;
    }
    if (dcpType == DcpTag::DCP_ACK) {
        return DCP_ADMISSION_DROP_ACK;
    }
    return DCP_ADMISSION_DROP_NON;
}

const char *SwitchNode::DcpAdmissionActionToString(DcpAdmissionAction action) {
    switch (action) {
        case DCP_ADMISSION_BYPASS:
            return "bypass";
        case DCP_ADMISSION_ENQUEUE:
            return "enqueue";
        case DCP_ADMISSION_ENQUEUE_CONTROL:
            return "enqueue_control";
        case DCP_ADMISSION_TRIM_TO_HO:
            return "trim_to_ho";
        case DCP_ADMISSION_DROP_NON:
            return "drop_non";
        case DCP_ADMISSION_DROP_ACK:
            return "drop_ack";
        default:
            return "unknown";
    }
}

int SwitchNode::GetOutDev(Ptr<Packet> p, CustomHeader &ch) {
    // look up entries
    auto entry = m_rtTable.find(ch.dip);

    // no matching entry
    if (entry == m_rtTable.end()) {
        std::cout << "[ERROR] Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
                  << "No matching entry, so drop this packet at SwitchNode (l3Prot:" << ch.l3Prot
                  << ")" << std::endl;
        assert(false);
    }

    // entry found
    const auto &nexthops = entry->second;
    bool control_pkt =
        (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE || ch.l3Prot == 0xFD || ch.l3Prot == 0xFC);

    if (Settings::lb_mode == 0 || control_pkt) {  // control packet (ACK, NACK, PFC, QCN)
        return DoLbFlowECMP(p, ch, nexthops);     // ECMP routing path decision (4-tuple)
    }

    if (Settings::lb_mode == 11) {
        DcpTag dcpTag;
        uint8_t dcpType = ClassifyDcpPacket(p, ch, &dcpTag);
        if (ch.l3Prot != 0x11 || dcpType == DcpTag::DCP_HO) {
            return DoLbFlowECMP(p, ch, nexthops);
        }
        return DoLbAdaptiveRouting(p, ch, nexthops);
    }

    switch (Settings::lb_mode) {
        case 2:
            return DoLbDrill(p, ch, nexthops);
        case 3:
            return DoLbConga(p, ch, nexthops); /** DUMMY: Do ECMP */
        case 6:
            return DoLbLetflow(p, ch, nexthops);
        case 9:
            return DoLbConWeave(p, ch, nexthops); /** DUMMY: Do ECMP */
        case 10:
            return DoLbTemplate(p, ch, nexthops);
        default:
            std::cout << "Unknown lb_mode(" << Settings::lb_mode << ")" << std::endl;
            assert(false);
    }
}

/*
 * The (possible) callback point when conweave dequeues packets from buffer
 */
void SwitchNode::DoSwitchSend(Ptr<Packet> p, CustomHeader &ch, uint32_t outDev, uint32_t qIndex) {
    // admission control
    FlowIdTag t;
    p->PeekPacketTag(t);
    uint32_t inDev = t.GetFlowId();
    DcpTag dcpTag;
    uint8_t dcpType = ClassifyDcpPacket(p, ch, &dcpTag);

    /** NOTE:
     * ConWeave control packets have the high priority as ACK/NACK/PFC/etc with qIndex = 0.
     */
    if (inDev == Settings::CONWEAVE_CTRL_DUMMY_INDEV) { // sanity check
        // ConWeave reply is on ACK protocol with high priority, so qIndex should be 0
        assert(qIndex == 0 && m_ackHighPrio == 1 && "ConWeave's reply packet follows ACK, so its qIndex should be 0");
    }

    uint32_t dataQueueIndex = GetDcpDataQueueIndex(ch, qIndex);
    bool dataQueueCandidate = (qIndex != 0 || dcpType == DcpTag::DCP_DATA ||
                               dcpType == DcpTag::DCP_ACK);
    uint32_t dataQueueBytes =
        dataQueueIndex == 0 ? 0 : m_mmu->GetEgressQueueBytes(outDev, dataQueueIndex);
    DcpAdmissionAction dcpAdmission =
        EvaluateDcpAdmission(dcpType, Settings::enable_dcp, dataQueueCandidate, dataQueueBytes,
                             p->GetSize(), Settings::dcp_trim_threshold);

    if (dcpAdmission == DCP_ADMISSION_ENQUEUE_CONTROL) {
        qIndex = 0;
    } else if (dcpAdmission == DCP_ADMISSION_TRIM_TO_HO) {
        Ptr<Packet> ho = CreateDcpHoPacket(p, dcpTag);
        Settings::dcp_trim_events++;
        Settings::dcp_ho_generated++;
        Settings::dcp_ho_packets++;
        Settings::dcp_ho_bytes += ho->GetSize();
        Settings::dcp_data_bytes_trimmed +=
            p->GetSize() > ho->GetSize() ? p->GetSize() - ho->GetSize() : 0;
        Settings::data_queue_len = std::max<uint64_t>(Settings::data_queue_len, dataQueueBytes);
        Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[outDev]);
        if (device != NULL && device->GetQueue() != NULL) {
            Settings::control_queue_len =
                std::max<uint64_t>(Settings::control_queue_len,
                                   device->GetQueue()->GetNBytes(0) + ho->GetSize());
        }
        if (!m_devices[outDev]->SwitchSend(0, ho, ch)) {
            Settings::dcp_ho_dropped++;
        }
        UpdateDcpQueueStats(outDev, 0);
        return;
    } else if (dcpAdmission == DCP_ADMISSION_DROP_NON) {
        Settings::dcp_non_dropped++;
        Settings::dcp_data_queue_drops++;
        Settings::dropped_pkt_sw_egress++;
        return;
    } else if (dcpAdmission == DCP_ADMISSION_DROP_ACK) {
        Settings::dcp_ack_dropped++;
        Settings::dcp_data_queue_drops++;
        Settings::dropped_pkt_sw_egress++;
        return;
    }

    if (qIndex != 0) {  // not highest priority
        if (m_mmu->CheckEgressAdmission(outDev, qIndex,
                                        p->GetSize())) {  // Egress Admission control
            if (m_mmu->CheckIngressAdmission(inDev, qIndex,
                                             p->GetSize())) {  // Ingress Admission control
                m_mmu->UpdateIngressAdmission(inDev, qIndex, p->GetSize());
                m_mmu->UpdateEgressAdmission(outDev, qIndex, p->GetSize());
            } else { /** DROP: At Ingress */
#if (0)
                // /** NOTE: logging dropped pkts */
                // std::cout << "LostPkt ingress - Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
                //           << "L3Prot:" << ch.l3Prot
                //           << ",Size:" << p->GetSize()
                //           << ",At " << Simulator::Now() << std::endl;
#endif
                Settings::dropped_pkt_sw_ingress++;
                RecordDcpQueueDrop(dcpType);
                return;  // drop
            }
        } else { /** DROP: At Egress */
#if (0)
            // /** NOTE: logging dropped pkts */
            // std::cout << "LostPkt egress - Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
            //           << "L3Prot:" << ch.l3Prot << ",Size:" << p->GetSize() << ",At "
            //           << Simulator::Now() << std::endl;
#endif
            Settings::dropped_pkt_sw_egress++;
            RecordDcpQueueDrop(dcpType);
            return;  // drop
        }

        CheckAndSendPfc(inDev, qIndex);
    }

    if (!m_devices[outDev]->SwitchSend(qIndex, p, ch)) {
        RecordDcpQueueDrop(dcpType);
    }
    UpdateDcpQueueStats(outDev, qIndex);
}

static void SetBccEcnBits(Ptr<Packet> p, uint8_t state) {
    PppHeader ppp;
    Ipv4Header h;
    p->RemoveHeader(ppp);
    p->RemoveHeader(h);
    h.SetEcn((Ipv4Header::EcnType)BccTag::StateToEcnBits(state));
    p->AddHeader(h);
    p->AddHeader(ppp);
}

void SwitchNode::UpdateBccStateAndTag(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p) {
    if (!m_bccMarkingEnabled || qIndex == 0) {
        return;
    }

    BccPortState &state = m_bccPortState[ifIndex];
    uint64_t now = Simulator::Now().GetNanoSeconds();
    uint64_t elapsed = now > state.last_update_time ? now - state.last_update_time : 0;
    uint32_t queueLen = m_mmu->GetEgressPortBytes(ifIndex);
    uint64_t txBytes = m_txBytes[ifIndex];

    state.last_queue_len = state.queue_len;
    state.queue_len = queueLen;

    if (elapsed > 0 && state.last_update_time > 0) {
        Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
        double capacityBytes = dev->GetDataRate().GetBitRate() * (double)elapsed / 8e9;
        if (capacityBytes > 0) {
            state.queue_slope = ((double)state.queue_len - (double)state.last_queue_len) /
                                capacityBytes;
            state.link_utilization =
                std::min(1.0, (double)(txBytes - state.last_tx_bytes) / capacityBytes);
        }
    }

    state.state = ClassifyBccState(ifIndex, state);
    state.last_update_time = now;
    state.last_tx_bytes = txBytes;

    BccTag pathTag;
    bool hasPathTag = p->PeekPacketTag(pathTag);
    if (hasPathTag && !BccTag::ShouldReplacePathState(state.state, pathTag.GetState())) {
        SetBccEcnBits(p, pathTag.GetState());
        return;
    }
    if (hasPathTag) {
        p->RemovePacketTag(pathTag);
    }

    BccTag tag;
    tag.SetState(state.state);
    tag.SetSwitchId(GetId());
    tag.SetEgressPort(ifIndex);
    tag.SetQueueLen(state.queue_len);
    tag.SetQueueSlope(state.queue_slope);
    tag.SetLinkUtilization(state.link_utilization);
    tag.SetTimestampNs(now);
    p->AddPacketTag(tag);
    SetBccEcnBits(p, state.state);
}

void SwitchNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p) {
    FlowIdTag t;
    p->PeekPacketTag(t);
    if (qIndex != 0) {
        uint32_t inDev = t.GetFlowId();
        UpdateBccStateAndTag(ifIndex, qIndex, p);
        if (inDev != Settings::CONWEAVE_CTRL_DUMMY_INDEV) {
            // NOTE: ConWeave's probe/reply does not need to pass inDev interface,
            // so skip for conweave's queued packets
            m_mmu->RemoveFromIngressAdmission(inDev, qIndex, p->GetSize());
        }
        m_mmu->RemoveFromEgressAdmission(ifIndex, qIndex, p->GetSize());
        if (m_ecnEnabled && !m_bccMarkingEnabled) {
            bool egressCongested = m_mmu->ShouldSendCN(ifIndex, qIndex);
            if (egressCongested) {
                PppHeader ppp;
                Ipv4Header h;
                p->RemoveHeader(ppp);
                p->RemoveHeader(h);
                h.SetEcn((Ipv4Header::EcnType)0x03);
                p->AddHeader(h);
                p->AddHeader(ppp);
            }
        }
        // NOTE: ConWeave's probe/reply does not need to pass inDev interface
        if (inDev != Settings::CONWEAVE_CTRL_DUMMY_INDEV) {
            CheckAndSendResume(inDev, qIndex);
        }
    }

    // HPCC's INT
    if (1) {
        uint8_t *buf = p->GetBuffer();
        if (buf[PppHeader::GetStaticSize() + 9] == 0x11) {  // udp packet
            IntHeader *ih = (IntHeader *)&buf[PppHeader::GetStaticSize() + 20 + 8 +
                                              6];  // ppp, ip, udp, SeqTs, INT
            Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
            if (m_ccMode == 3) {  // HPCC
                ih->PushHop(Simulator::Now().GetTimeStep(), m_txBytes[ifIndex],
                            dev->GetQueue()->GetNBytesTotal(), dev->GetDataRate().GetBitRate());
            }
        }
    }
    m_txBytes[ifIndex] += p->GetSize();
}

uint32_t SwitchNode::EcmpHash(const uint8_t *key, size_t len, uint32_t seed) {
    uint32_t h = seed;
    if (len > 3) {
        const uint32_t *key_x4 = (const uint32_t *)key;
        size_t i = len >> 2;
        do {
            uint32_t k = *key_x4++;
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h += (h << 2) + 0xe6546b64;
        } while (--i);
        key = (const uint8_t *)key_x4;
    }
    if (len & 3) {
        size_t i = len & 3;
        uint32_t k = 0;
        key = &key[i - 1];
        do {
            k <<= 8;
            k |= *key--;
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

void SwitchNode::SetEcmpSeed(uint32_t seed) { m_ecmpSeed = seed; }

void SwitchNode::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx) {
    uint32_t dip = dstAddr.Get();
    m_rtTable[dip].push_back(intf_idx);
}

void SwitchNode::ClearTable() { m_rtTable.clear(); }

uint64_t SwitchNode::GetTxBytesOutDev(uint32_t outdev) {
    assert(outdev < pCnt);
    return m_txBytes[outdev];
}

const BccPortState &SwitchNode::GetBccPortState(uint32_t outdev) const {
    assert(outdev < pCnt);
    return m_bccPortState[outdev];
}

uint8_t SwitchNode::ClassifyBccState(uint32_t ifIndex, const BccPortState &state) const {
    uint32_t k1 = m_mmu->kmin[ifIndex];
    uint32_t k2 = m_mmu->kmax[ifIndex];
    return TransitionBccState(state.state, state.queue_len, k1, k2, state.queue_slope,
                              state.link_utilization, m_bccSlopeThreshold,
                              m_bccUtilizationThreshold);
}

uint8_t SwitchNode::TransitionBccState(uint8_t previousState, uint32_t queueLen, uint32_t k1,
                                       uint32_t k2, double queueSlope, double linkUtilization,
                                       double slopeThreshold, double utilizationThreshold) {
    bool aboveK1 = queueLen > k1;
    bool belowK1 = queueLen < k1;
    bool aboveK2 = queueLen > k2;
    bool belowK2 = queueLen < k2;
    bool risingFast = queueSlope > slopeThreshold;
    bool fallingBelowTransient = queueSlope < slopeThreshold;
    bool underUtilized = linkUtilization < utilizationThreshold;
    bool utilizationRecovered = linkUtilization > utilizationThreshold;

    switch (previousState) {
        case BccTag::NC:
            if (aboveK1) {
                return BccTag::PC;
            }
            if (underUtilized) {
                return BccTag::TU;
            }
            return BccTag::NC;
        case BccTag::PC:
            if (aboveK2 || risingFast) {
                return BccTag::TC;
            }
            if (belowK1) {
                return BccTag::NC;
            }
            return BccTag::PC;
        case BccTag::TC:
            if (fallingBelowTransient && belowK2) {
                return BccTag::PC;
            }
            return BccTag::TC;
        case BccTag::TU:
            if (aboveK2 || risingFast) {
                return BccTag::TC;
            }
            if (aboveK1) {
                return BccTag::PC;
            }
            if (utilizationRecovered) {
                return BccTag::NC;
            }
            return BccTag::TU;
        default:
            return TransitionBccState(BccTag::NC, queueLen, k1, k2, queueSlope, linkUtilization,
                                      slopeThreshold, utilizationThreshold);
    }
}

const char *SwitchNode::BccStateToString(uint8_t state) { return BccTag::StateToString(state); }

} /* namespace ns3 */
