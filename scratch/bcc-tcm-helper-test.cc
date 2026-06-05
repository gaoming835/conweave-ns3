#include "ns3/rdma-hw.h"

#include <iostream>

using namespace ns3;

namespace {

bool Check(const char *name, bool ok) {
    if (!ok) {
        std::cerr << name << std::endl;
    }
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    bool ok = true;

    ok = ok && Check("pause should be I/R_hat - Tb",
                     RdmaHw::ComputeBccPauseNs(25000, DataRate("10Gb/s"), 10000) == 10000);
    ok = ok && Check("pause should floor at zero",
                     RdmaHw::ComputeBccPauseNs(1000, DataRate("10Gb/s"), 10000) == 0);

    ok = ok && Check("inflight bound should be R_hat * Tb",
                     RdmaHw::ComputeBccInflightBoundBytes(DataRate("10Gb/s"), 10000, 1000,
                                                          1000000) == 12500);
    ok = ok && Check("inflight bound should keep at least one MTU",
                     RdmaHw::ComputeBccInflightBoundBytes(DataRate("100Mb/s"), 1000, 1000,
                                                          1000000) == 1000);
    ok = ok && Check("inflight bound should not exceed flow size",
                     RdmaHw::ComputeBccInflightBoundBytes(DataRate("100Gb/s"), 1000000, 1000,
                                                          2000) == 2000);

    ok = ok && Check("TU should divide R_hat by utilization",
                     RdmaHw::ComputeBccTuRate(DataRate("10Gb/s"), 0.5).GetBitRate() ==
                         20000000000ULL);
    ok = ok && Check("TU utilization should be clamped",
                     RdmaHw::ComputeBccTuRate(DataRate("10Gb/s"), 0.0).GetBitRate() ==
                         200000000000ULL);

    if (!ok) {
        return 1;
    }

    std::cout << "bcc_tcm_helper=pass" << std::endl;
    return 0;
}
