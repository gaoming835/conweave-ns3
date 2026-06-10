#include <iostream>

#include "ns3/dcp-tag.h"
#include "ns3/switch-node.h"

using namespace ns3;

static bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

static bool CheckAction(SwitchNode::DcpAdmissionAction actual,
                        SwitchNode::DcpAdmissionAction expected, const char *message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " expected="
                  << SwitchNode::DcpAdmissionActionToString(expected)
                  << " actual=" << SwitchNode::DcpAdmissionActionToString(actual) << std::endl;
        return false;
    }
    return true;
}

int main() {
    bool ok = true;
    const uint32_t threshold = 1000;
    const uint32_t packetSize = 256;

    ok = CheckAction(SwitchNode::EvaluateDcpAdmission(
                         DcpTag::DCP_DATA, true, true, 512, packetSize, threshold),
                     SwitchNode::DCP_ADMISSION_ENQUEUE,
                     "DCP DATA below threshold should enter data queue") &&
         ok;
    ok = CheckAction(SwitchNode::EvaluateDcpAdmission(
                         DcpTag::DCP_DATA, true, true, 900, packetSize, threshold),
                     SwitchNode::DCP_ADMISSION_TRIM_TO_HO,
                     "DCP DATA above threshold should trim to HO") &&
         ok;
    ok = CheckAction(SwitchNode::EvaluateDcpAdmission(
                         DcpTag::DCP_ACK, true, true, 900, packetSize, threshold),
                     SwitchNode::DCP_ADMISSION_DROP_ACK,
                     "DCP ACK above threshold should drop") &&
         ok;
    ok = CheckAction(SwitchNode::EvaluateDcpAdmission(
                         DcpTag::DCP_NON, true, true, 900, packetSize, threshold),
                     SwitchNode::DCP_ADMISSION_DROP_NON,
                     "non-DCP data packet above threshold should drop") &&
         ok;
    ok = CheckAction(SwitchNode::EvaluateDcpAdmission(
                         DcpTag::DCP_HO, true, true, 900, packetSize, threshold),
                     SwitchNode::DCP_ADMISSION_ENQUEUE_CONTROL,
                     "DCP HO should enter control queue") &&
         ok;
    ok = CheckAction(SwitchNode::EvaluateDcpAdmission(
                         DcpTag::DCP_NON, true, false, 900, packetSize, threshold),
                     SwitchNode::DCP_ADMISSION_BYPASS,
                     "non-data control packets should bypass DCP data threshold") &&
         ok;
    ok = CheckAction(SwitchNode::EvaluateDcpAdmission(
                         DcpTag::DCP_DATA, false, true, 900, packetSize, threshold),
                     SwitchNode::DCP_ADMISSION_BYPASS,
                     "DCP disabled should bypass DCP admission") &&
         ok;

    ok = Check(SwitchNode::EvaluateDcpAdmission(DcpTag::DCP_DATA, true, true, 744, packetSize,
                                                threshold) ==
                   SwitchNode::DCP_ADMISSION_ENQUEUE,
               "threshold comparison should allow equal occupancy") &&
         ok;

    if (!ok) {
        return 1;
    }

    std::cout << "dcp_trim_semantics_unit=pass" << std::endl;
    return 0;
}
