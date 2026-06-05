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

    ok = ok && Check("BCC TCM should suppress Mellanox/DCQCN timers",
                     RdmaHw::ShouldSuppressBccMlxTimer(10, true, RdmaQueuePair::BCC_TCM));
    ok = ok && Check("BCC PCM should allow Mellanox/DCQCN timers",
                     !RdmaHw::ShouldSuppressBccMlxTimer(10, true, RdmaQueuePair::BCC_PCM));
    ok = ok && Check("disabled BCC should allow Mellanox/DCQCN timers",
                     !RdmaHw::ShouldSuppressBccMlxTimer(10, false, RdmaQueuePair::BCC_TCM));
    ok = ok && Check("plain DCQCN should allow Mellanox/DCQCN timers",
                     !RdmaHw::ShouldSuppressBccMlxTimer(1, false, RdmaQueuePair::BCC_TCM));

    if (!ok) {
        return 1;
    }

    std::cout << "bcc_mode_handoff=pass" << std::endl;
    return 0;
}
