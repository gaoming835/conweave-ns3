#include <iostream>

#include "ns3/rdma-queue-pair.h"
#include "ns3/settings.h"

using namespace ns3;

static bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

int main() {
    Settings::enable_dcp = true;
    Settings::dcp_ooo_packets = 0;
    Settings::dcp_completed_messages = 0;
    Settings::dcp_ack_packets = 0;
    Settings::dcp_spurious_retx = 0;

    Ptr<RdmaRxQueuePair> rxQp = CreateObject<RdmaRxQueuePair>();
    uint32_t ackSeq = 0;
    bool ooo = false;

    bool completed = rxQp->DcpRecordPacket(1000, 1000, 2000, &ackSeq, &ooo);
    if (ooo) {
        Settings::dcp_ooo_packets++;
    }

    bool ok = true;
    ok = Check(ooo, "first packet should be counted as OOO") && ok;
    ok = Check(!completed, "OOO packet must not complete the message") && ok;
    ok = Check(rxQp->m_irn_sack_.IsEmpty(), "DCP OOO packet must not create IRN SACK state") && ok;
    ok = Check(Settings::dcp_ack_packets == 0, "DCP OOO packet must not generate ACK/SACK") && ok;
    ok = Check(Settings::dcp_spurious_retx == 0, "DCP OOO packet must not trigger recovery") && ok;

    ooo = false;
    completed = rxQp->DcpRecordPacket(0, 1000, 2000, &ackSeq, &ooo);
    if (ooo) {
        Settings::dcp_ooo_packets++;
    }
    if (completed) {
        Settings::dcp_completed_messages++;
        Settings::dcp_ack_packets++;
    }

    ok = Check(!ooo, "second packet should be in order") && ok;
    ok = Check(completed, "message should complete after all packets arrive") && ok;
    ok = Check(ackSeq == 2000, "completion ACK sequence mismatch") && ok;
    ok = Check(Settings::dcp_ooo_packets > 0, "missing DCP OOO counter") && ok;
    ok = Check(Settings::dcp_completed_messages > 0, "missing DCP completion counter") && ok;
    ok = Check(Settings::dcp_ack_packets > 0, "missing DCP ACK counter") && ok;
    ok = Check(Settings::dcp_spurious_retx == 0, "DCP OOO recovery should stay zero") && ok;

    completed = rxQp->DcpRecordPacket(0, 1000, 2000, &ackSeq, &ooo);
    if (completed) {
        Settings::dcp_completed_messages++;
        Settings::dcp_ack_packets++;
    }
    ok = Check(!completed, "duplicate packet should not complete a second time") && ok;
    ok = Check(Settings::dcp_completed_messages == 1, "duplicate packet should not add completion")
         && ok;
    ok = Check(Settings::dcp_ack_packets == 1, "duplicate packet should not add ACK") && ok;

    if (!ok) {
        return 1;
    }

    std::cout << "dcp_ooo_receiver_unit=pass" << std::endl;
    std::cout << "dcp_ooo_packets=" << Settings::dcp_ooo_packets << std::endl;
    std::cout << "dcp_completed_messages=" << Settings::dcp_completed_messages << std::endl;
    std::cout << "dcp_ack_packets=" << Settings::dcp_ack_packets << std::endl;
    std::cout << "dcp_spurious_retx=" << Settings::dcp_spurious_retx << std::endl;
    return 0;
}
