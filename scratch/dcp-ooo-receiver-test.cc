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
    Settings::dcp_duplicate_packets = 0;
    Settings::dcp_retransmitted_packets = 0;
    Settings::dcp_emsn_advancements = 0;
    Settings::dcp_completed_messages = 0;
    Settings::dcp_ack_packets = 0;
    Settings::dcp_spurious_retx = 0;
    Settings::dcp_enable_message_tracking = true;

    Ptr<RdmaRxQueuePair> rxQp = CreateObject<RdmaRxQueuePair>();
    RdmaRxQueuePair::DcpRecordResult result =
        rxQp->DcpRecordMessagePacket(1000, 1000, 0, 1, 0, 2000, 1000);
    if (result.ooo) {
        Settings::dcp_ooo_packets++;
    }

    bool ok = true;
    ok = Check(result.ooo, "single-message tail packet should be counted as OOO") && ok;
    ok = Check(!result.messageCompleted, "OOO packet must not complete the message") && ok;
    ok = Check(rxQp->m_irn_sack_.IsEmpty(), "DCP OOO packet must not create IRN SACK state") && ok;
    ok = Check(Settings::dcp_ack_packets == 0, "DCP OOO packet must not generate ACK/SACK") && ok;
    ok = Check(Settings::dcp_spurious_retx == 0, "DCP OOO packet must not trigger recovery") && ok;

    result = rxQp->DcpRecordMessagePacket(0, 1000, 0, 1, 0, 2000, 0);
    if (result.ooo) {
        Settings::dcp_ooo_packets++;
    }
    if (result.emsnAdvanced) {
        Settings::dcp_emsn_advancements++;
    }
    if (result.messageCompleted) {
        Settings::dcp_completed_messages++;
        Settings::dcp_ack_packets++;
    }

    ok = Check(!result.ooo, "message head packet should be in order") && ok;
    ok = Check(result.messageCompleted, "message should complete after all packets arrive") && ok;
    ok = Check(result.ackSeq == 2000, "message completion ACK sequence mismatch") && ok;
    ok = Check(result.emsnAdvanced && result.nextEmsn == 1, "eMSN should advance to 1") && ok;
    ok = Check(Settings::dcp_ooo_packets > 0, "missing DCP OOO counter") && ok;
    ok = Check(Settings::dcp_completed_messages > 0, "missing DCP completion counter") && ok;
    ok = Check(Settings::dcp_ack_packets > 0, "missing DCP ACK counter") && ok;
    ok = Check(Settings::dcp_spurious_retx == 0, "DCP OOO recovery should stay zero") && ok;

    result = rxQp->DcpRecordMessagePacket(0, 1000, 0, 1, 1, 2000, 0);
    if (result.duplicate) {
        Settings::dcp_duplicate_packets++;
    }
    if (result.retransmitted) {
        Settings::dcp_retransmitted_packets++;
    }
    if (result.messageCompleted) {
        Settings::dcp_completed_messages++;
        Settings::dcp_ack_packets++;
    }
    ok = Check(result.duplicate, "duplicate retransmission should be counted") && ok;
    ok = Check(result.retransmitted, "sRetryNo retransmission should be counted") && ok;
    ok = Check(!result.messageCompleted, "duplicate packet should not complete a second time") && ok;
    ok = Check(Settings::dcp_completed_messages == 1, "duplicate packet should not add completion")
         && ok;
    ok = Check(Settings::dcp_ack_packets == 1, "duplicate packet should not add ACK") && ok;

    Ptr<RdmaRxQueuePair> multiQp = CreateObject<RdmaRxQueuePair>();
    result = multiQp->DcpRecordMessagePacket(1000, 1000, 1, 2, 0, 1000, 0);
    ok = Check(result.ooo, "message 1 before message 0 should be OOO") && ok;
    ok = Check(result.messageCompleted, "message 1 single packet should complete") && ok;
    ok = Check(!result.emsnAdvanced, "eMSN should wait for lower MSN") && ok;
    result = multiQp->DcpRecordMessagePacket(0, 1000, 0, 1, 0, 1000, 0);
    ok = Check(result.messageCompleted, "message 0 should complete") && ok;
    ok = Check(result.emsnAdvanced && result.nextEmsn == 2,
               "message 0 completion should advance eMSN across completed message 1") &&
         ok;
    result = multiQp->DcpRecordMessagePacket(1000, 1000, 1, 2, 0, 1000, 0);
    ok = Check(result.duplicate, "completed message duplicate should be counted") && ok;

    Ptr<RdmaRxQueuePair> tailQp = CreateObject<RdmaRxQueuePair>();
    result = tailQp->DcpRecordMessagePacket(0, 1000, 0, 1, 0, 2000, 0);
    ok = Check(!result.messageCompleted, "tail loss should leave message incomplete") && ok;
    result = tailQp->DcpRecordMessagePacket(1000, 1000, 0, 1, 1, 2000, 1000);
    ok = Check(result.retransmitted, "tail HO retransmission should carry retry number") && ok;
    ok = Check(result.messageCompleted, "tail retransmission should complete message") && ok;
    ok = Check(result.ackSeq == 2000, "tail retransmission ACK sequence mismatch") && ok;

    Ptr<RdmaRxQueuePair> compatQp = CreateObject<RdmaRxQueuePair>();
    uint32_t ackSeq = 0;
    bool ooo = false;
    Settings::dcp_enable_message_tracking = false;
    bool compatCompleted = compatQp->DcpRecordPacket(1000, 1000, 2000, &ackSeq, &ooo);
    ok = Check(ooo && !compatCompleted, "compat flow-interval mode should still detect OOO") && ok;
    compatCompleted = compatQp->DcpRecordPacket(0, 1000, 2000, &ackSeq, &ooo);
    ok = Check(compatCompleted && ackSeq == 2000,
               "compat flow-interval mode should still complete") &&
         ok;

    if (!ok) {
        return 1;
    }

    std::cout << "dcp_ooo_receiver_unit=pass" << std::endl;
    std::cout << "dcp_ooo_packets=" << Settings::dcp_ooo_packets << std::endl;
    std::cout << "dcp_duplicate_packets=" << Settings::dcp_duplicate_packets << std::endl;
    std::cout << "dcp_retransmitted_packets=" << Settings::dcp_retransmitted_packets << std::endl;
    std::cout << "dcp_emsn_advancements=" << Settings::dcp_emsn_advancements << std::endl;
    std::cout << "dcp_completed_messages=" << Settings::dcp_completed_messages << std::endl;
    std::cout << "dcp_ack_packets=" << Settings::dcp_ack_packets << std::endl;
    std::cout << "dcp_spurious_retx=" << Settings::dcp_spurious_retx << std::endl;
    return 0;
}
