#include "ns3/bcc-tag.h"
#include "ns3/switch-node.h"

#include <cstdlib>
#include <iostream>

using namespace ns3;

namespace {

void ExpectState(const char *name, uint8_t actual, uint8_t expected) {
    if (actual != expected) {
        std::cerr << name << " expected " << BccTag::StateToString(expected) << " got "
                  << BccTag::StateToString(actual) << std::endl;
        std::exit(1);
    }
}

uint8_t Step(uint8_t previousState, uint32_t queueLen, double queueSlope, double utilization) {
    return SwitchNode::TransitionBccState(previousState, queueLen, 1000, 2000, queueSlope,
                                          utilization, 1.0, 0.9);
}

}  // namespace

int main(int argc, char **argv) {
    ExpectState("NC stays NC", Step(BccTag::NC, 1000, 0.0, 0.95), BccTag::NC);
    ExpectState("NC to PC above K1", Step(BccTag::NC, 1001, 0.0, 0.95), BccTag::PC);
    ExpectState("NC to TU below U", Step(BccTag::NC, 999, 0.0, 0.80), BccTag::TU);
    ExpectState("NC congestion wins over TU", Step(BccTag::NC, 1001, 0.0, 0.80),
                BccTag::PC);

    ExpectState("PC stays PC at K1", Step(BccTag::PC, 1000, 0.0, 0.95), BccTag::PC);
    ExpectState("PC to NC below K1", Step(BccTag::PC, 999, 0.0, 0.95), BccTag::NC);
    ExpectState("PC to TC above K2", Step(BccTag::PC, 2001, 0.0, 0.95), BccTag::TC);
    ExpectState("PC to TC rising slope", Step(BccTag::PC, 1200, 1.1, 0.95), BccTag::TC);
    ExpectState("PC slope congestion wins over low utilization", Step(BccTag::PC, 999, 1.1, 0.80),
                BccTag::TC);

    ExpectState("TC stays TC at K2", Step(BccTag::TC, 2000, 0.5, 0.95), BccTag::TC);
    ExpectState("TC stays TC at slope threshold", Step(BccTag::TC, 1999, 1.0, 0.95),
                BccTag::TC);
    ExpectState("TC to PC recovered", Step(BccTag::TC, 1999, 0.5, 0.95), BccTag::PC);

    ExpectState("TU stays TU below U", Step(BccTag::TU, 999, 0.0, 0.90), BccTag::TU);
    ExpectState("TU to NC above U", Step(BccTag::TU, 999, 0.0, 0.91), BccTag::NC);
    ExpectState("TU to PC on queue congestion", Step(BccTag::TU, 1001, 0.0, 0.80),
                BccTag::PC);
    ExpectState("TU to TC on transient congestion", Step(BccTag::TU, 999, 1.1, 0.80),
                BccTag::TC);

    std::cout << "bcc_switch_state_machine=pass" << std::endl;
    return 0;
}
