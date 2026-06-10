#include "ns3/rdma-queue-pair.h"
#include "ns3/settings.h"

#include <iostream>

using namespace ns3;

static bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    bool ok = true;

    Settings::enable_dcp = true;
    Settings::dcp_retrans_batch_size = 2;
    Settings::dcp_retrans_quota_bytes = 2000;
    Settings::dcp_retrans_respect_win = false;

    Ptr<RdmaQueuePair> qp =
        CreateObject<RdmaQueuePair>(1, Ipv4Address("11.0.0.1"), Ipv4Address("11.0.0.2"),
                                    1000, 2000);
    qp->SetSize(4096);
    qp->snd_una = 0;
    qp->snd_nxt = 4096;

    ok = Check(qp->EnqueueDcpRetrans(0, RdmaQueuePair::DCP_RETRANS_FROM_HO),
               "first HO retrans enqueue should succeed") &&
         ok;
    ok = Check(!qp->EnqueueDcpRetrans(0, RdmaQueuePair::DCP_RETRANS_FROM_TIMEOUT),
               "duplicate PSN should not enqueue twice") &&
         ok;
    ok = Check(qp->EnqueueDcpRetrans(1000, RdmaQueuePair::DCP_RETRANS_FROM_TIMEOUT),
               "timeout retrans enqueue should succeed") &&
         ok;
    ok = Check(qp->EnqueueDcpRetrans(2000, RdmaQueuePair::DCP_RETRANS_FROM_HO),
               "third retrans enqueue should succeed") &&
         ok;

    uint32_t psn = 0;
    uint8_t source = 0;
    ok = Check(qp->DequeueDcpRetrans(&psn, &source, 1000), "first dequeue should succeed") && ok;
    ok = Check(psn == 0 && source == RdmaQueuePair::DCP_RETRANS_FROM_HO,
               "first dequeue should preserve HO source") &&
         ok;
    ok = Check(qp->DequeueDcpRetrans(&psn, &source, 1000), "second dequeue should succeed") && ok;
    ok = Check(psn == 1000 && source == RdmaQueuePair::DCP_RETRANS_FROM_TIMEOUT,
               "second dequeue should preserve timeout source") &&
         ok;
    ok = Check(!qp->DequeueDcpRetrans(&psn, &source, 1000),
               "batch limit should stop third dequeue in the same round") &&
         ok;

    ok = Check(qp->DequeueDcpRetrans(&psn, &source, 1000),
               "new round should allow the remaining retransmission") &&
         ok;
    ok = Check(psn == 2000 && source == RdmaQueuePair::DCP_RETRANS_FROM_HO,
               "remaining retransmission should be the third PSN") &&
         ok;

    Settings::dcp_retrans_batch_size = 8;
    Settings::dcp_retrans_quota_bytes = 1500;
    ok = Check(qp->EnqueueDcpRetrans(1000, RdmaQueuePair::DCP_RETRANS_FROM_HO),
               "quota test first enqueue should succeed") &&
         ok;
    ok = Check(qp->EnqueueDcpRetrans(2000, RdmaQueuePair::DCP_RETRANS_FROM_HO),
               "quota test second enqueue should succeed") &&
         ok;
    ok = Check(qp->DequeueDcpRetrans(&psn, &source, 1000),
               "quota should allow first packet") &&
         ok;
    ok = Check(!qp->DequeueDcpRetrans(&psn, &source, 1000),
               "quota should stop a second packet that would exceed quota") &&
         ok;

    Settings::dcp_retrans_quota_bytes = 0;
    Settings::dcp_retrans_respect_win = true;
    qp->SetWin(1000);
    ok = Check(!qp->CanSendDcpRetrans(), "respect-window should block when QP is window-bound") &&
         ok;

    if (!ok) {
        return 1;
    }
    std::cout << "dcp_retrans_queue_unit=pass" << std::endl;
    return 0;
}
