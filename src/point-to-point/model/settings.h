#ifndef __SETINGS_H__
#define __SETINGS_H__

#include <stdbool.h>
#include <stdint.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ns3/callback.h"
#include "ns3/custom-header.h"
#include "ns3/double.h"
#include "ns3/ipv4-address.h"
#include "ns3/net-device.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/string.h"
#include "ns3/tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

#define SLB_DEBUG (false)

#define PARSE_FIVE_TUPLE(ch)                                                    \
    DEPARSE_FIVE_TUPLE(std::to_string(Settings::hostIp2IdMap[ch.sip]),          \
                       std::to_string(ch.udp.sport),                            \
                       std::to_string(Settings::hostIp2IdMap[ch.dip]),          \
                       std::to_string(ch.udp.dport), std::to_string(ch.l3Prot), \
                       std::to_string(ch.udp.seq), std::to_string(ch.GetIpv4EcnBits()))
#define PARSE_REVERSE_FIVE_TUPLE(ch)                                            \
    DEPARSE_FIVE_TUPLE(std::to_string(Settings::hostIp2IdMap[ch.dip]),          \
                       std::to_string(ch.udp.dport),                            \
                       std::to_string(Settings::hostIp2IdMap[ch.sip]),          \
                       std::to_string(ch.udp.sport), std::to_string(ch.l3Prot), \
                       std::to_string(ch.udp.seq), std::to_string(ch.GetIpv4EcnBits()))
#define DEPARSE_FIVE_TUPLE(sip, sport, dip, dport, protocol, seq, ecn)                        \
    sip << "(" << sport << ")," << dip << "(" << dport << ")[" << protocol << "],SEQ:" << seq \
        << ",ECN:" << ecn << ","

#if (SLB_DEBUG == true)
#define SLB_LOG(msg) \
    std::cout << __FILE__ << "(" << __LINE__ << "):" << Simulator::Now() << "," << msg << std::endl
#else
#define SLB_LOG(msg) \
    do {             \
    } while (0)
#endif

/**
 * @brief For flowlet-routing
 */
struct Flowlet {
    Time _activeTime;     // to check creating a new flowlet
    Time _activatedTime;  // start time of new flowlet
    uint32_t _PathId;     // current pathId
    uint32_t _nPackets;   // for debugging
};

/**
 * @brief Tag for monitoring last data sending time per flow
 */
class LastSendTimeTag : public Tag {
   public:
    LastSendTimeTag() : Tag() {}
    static TypeId GetTypeId(void) {
        static TypeId tid =
            TypeId("ns3::LastSendTimeTag").SetParent<Tag>().AddConstructor<LastSendTimeTag>();
        return tid;
    }
    virtual TypeId GetInstanceTypeId(void) const { return GetTypeId(); }
    virtual void Print(std::ostream &os) const {}
    virtual uint32_t GetSerializedSize(void) const { return sizeof(m_pktType); }
    virtual void Serialize(TagBuffer i) const { i.WriteU8(m_pktType); }
    virtual void Deserialize(TagBuffer i) { m_pktType = i.ReadU8(); }
    void SetPktType(uint8_t type) { m_pktType = type; }
    uint8_t GetPktType() { return m_pktType; }

    enum pktType {
        PACKET_NULL = 0,
        PACKET_FIRST = 1,
        PACKET_LAST = 2,
        PACKET_SINGLE = 3,
    };

   private:
    uint8_t m_pktType;
};

/**
 * @brief Global setting parameters
 */

class Settings {
   public:
    Settings() {}
    virtual ~Settings() {}

    /* helper function */
    static Ipv4Address node_id_to_ip(uint32_t id);  // node_id -> ip
    static uint32_t ip_to_node_id(Ipv4Address ip);  // ip -> node_id

    /* conweave params */
    static const uint32_t CONWEAVE_CTRL_DUMMY_INDEV = 88888888;  // just arbitrary

    /* load balancer */
    // 0: flow ECMP, 2: DRILL, 3: Conga, 6: Letflow, 9: ConWeave, 10: Template, 11: AR
    static uint32_t lb_mode;

    // for common setting
    static uint32_t packet_payload;

    // DCP skeleton config. Stage 1 only exposes config/stat plumbing.
    static bool enable_dcp;
    static std::string transport_mode;

    // for statistic
    static uint32_t node_num;
    static uint32_t host_num;
    static uint32_t switch_num;
    static uint64_t cnt_finished_flows;  // number of finished flows (in qp_finish())

    /* The map between hosts' IP and ID, initial when build topology */
    static std::map<uint32_t, uint32_t> hostIp2IdMap;
    static std::map<uint32_t, uint32_t> hostId2IpMap;
    static std::map<uint32_t, uint32_t> hostIp2SwitchId;  // host's IP -> connected Switch's Id

    static uint32_t dropped_pkt_sw_ingress;
    static uint32_t dropped_pkt_sw_egress;

    static uint64_t dcp_data_packets;
    static uint64_t dcp_ack_packets;
    static uint64_t dcp_ho_packets;
    static uint64_t dcp_trim_events;
    static uint64_t dcp_ho_generated;
    static uint64_t dcp_ho_returned;
    static uint64_t dcp_ho_rx_at_receiver;
    static uint64_t dcp_ho_rx_at_sender;
    static uint64_t dcp_retransq_enqueue;
    static uint64_t dcp_retransq_dequeue;
    static uint64_t dcp_precise_retx;
    static uint64_t dcp_spurious_retx;
    static uint64_t dcp_timeout_retx;
    static uint64_t dcp_ooo_packets;
    static uint64_t dcp_duplicate_packets;
    static uint64_t dcp_retransmitted_packets;
    static uint64_t dcp_emsn_advancements;
    static uint64_t dcp_completed_messages;
    static uint64_t dcp_ho_dropped;
    static uint64_t dcp_data_dropped;
    static uint64_t dcp_non_dropped;
    static uint64_t dcp_ack_dropped;
    static uint64_t dcp_ho_bytes;
    static uint64_t dcp_data_bytes_trimmed;
    static uint64_t control_queue_len;
    static uint64_t data_queue_len;
    static uint64_t dcp_control_queue_max_len;
    static uint64_t dcp_data_queue_max_len;
    static uint64_t dcp_control_queue_sum_len;
    static uint64_t dcp_data_queue_sum_len;
    static uint64_t dcp_queue_samples;
    static uint64_t dcp_control_queue_drops;
    static uint64_t dcp_data_queue_drops;
    static uint64_t dcp_control_dequeue_packets;
    static uint64_t dcp_data_dequeue_packets;
    static uint64_t dcp_control_dequeue_bytes;
    static uint64_t dcp_data_dequeue_bytes;
    static uint64_t ar_packets;
    static uint64_t ar_path_switches;
    static uint64_t ar_used_next_hops;
    static uint64_t irn_ooo_packets;
    static uint64_t irn_nack_packets;
    static bool dcp_enable_wrr;
    static uint32_t dcp_control_weight;
    static uint32_t dcp_data_weight;
    static uint32_t dcp_inc_scale_n;
    static double dcp_ho_data_ratio_r;
    static uint32_t dcp_trim_threshold;
    static uint32_t dcp_ho_size;
    static uint32_t dcp_retrans_per_round;
    static bool dcp_enable_timeout_retx;
    static bool dcp_enable_message_tracking;
};

}  // namespace ns3

#endif
