#include "ns3/settings.h"

namespace ns3 {
/* helper function */
Ipv4Address Settings::node_id_to_ip(uint32_t id) {
    return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}
uint32_t Settings::ip_to_node_id(Ipv4Address ip) {
    return (ip.Get() >> 8) & 0xffff;
}

/* others */
uint32_t Settings::lb_mode = 0;

std::map<uint32_t, uint32_t> Settings::hostIp2IdMap;
std::map<uint32_t, uint32_t> Settings::hostId2IpMap;

/* statistics */
uint32_t Settings::node_num = 0;
uint32_t Settings::host_num = 0;
uint32_t Settings::switch_num = 0;
uint64_t Settings::cnt_finished_flows = 0;
uint32_t Settings::packet_payload = 1000;

bool Settings::enable_dcp = false;
std::string Settings::transport_mode = "rdma";

uint32_t Settings::dropped_pkt_sw_ingress = 0;
uint32_t Settings::dropped_pkt_sw_egress = 0;

uint64_t Settings::dcp_data_packets = 0;
uint64_t Settings::dcp_ack_packets = 0;
uint64_t Settings::dcp_ho_packets = 0;
uint64_t Settings::dcp_trim_events = 0;
uint64_t Settings::dcp_ho_generated = 0;
uint64_t Settings::dcp_ho_returned = 0;
uint64_t Settings::dcp_ho_rx_at_receiver = 0;
uint64_t Settings::dcp_ho_rx_at_sender = 0;
uint64_t Settings::dcp_retransq_enqueue = 0;
uint64_t Settings::dcp_retransq_dequeue = 0;
uint64_t Settings::dcp_precise_retx = 0;
uint64_t Settings::dcp_spurious_retx = 0;
uint64_t Settings::dcp_timeout_retx = 0;
uint64_t Settings::dcp_ooo_packets = 0;
uint64_t Settings::dcp_completed_messages = 0;
uint64_t Settings::dcp_ho_dropped = 0;
uint64_t Settings::dcp_data_dropped = 0;
uint64_t Settings::dcp_non_dropped = 0;
uint64_t Settings::dcp_ack_dropped = 0;
uint64_t Settings::dcp_ho_bytes = 0;
uint64_t Settings::dcp_data_bytes_trimmed = 0;
uint64_t Settings::control_queue_len = 0;
uint64_t Settings::data_queue_len = 0;
uint64_t Settings::dcp_control_queue_max_len = 0;
uint64_t Settings::dcp_data_queue_max_len = 0;
uint64_t Settings::dcp_control_queue_sum_len = 0;
uint64_t Settings::dcp_data_queue_sum_len = 0;
uint64_t Settings::dcp_queue_samples = 0;
uint64_t Settings::dcp_control_queue_drops = 0;
uint64_t Settings::dcp_data_queue_drops = 0;
uint64_t Settings::dcp_control_dequeue_packets = 0;
uint64_t Settings::dcp_data_dequeue_packets = 0;
uint64_t Settings::dcp_control_dequeue_bytes = 0;
uint64_t Settings::dcp_data_dequeue_bytes = 0;
bool Settings::dcp_enable_wrr = false;
uint32_t Settings::dcp_control_weight = 1;
uint32_t Settings::dcp_data_weight = 1;
uint32_t Settings::dcp_inc_scale_n = 1;
double Settings::dcp_ho_data_ratio_r = 0.057;
uint32_t Settings::dcp_trim_threshold = 0xffffffff;
uint32_t Settings::dcp_ho_size = 0;
uint32_t Settings::dcp_retrans_per_round = 1;
bool Settings::dcp_enable_timeout_retx = false;

/* for load balancer */
std::map<uint32_t, uint32_t> Settings::hostIp2SwitchId;

}  // namespace ns3
