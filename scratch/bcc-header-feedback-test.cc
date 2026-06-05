#include "ns3/bcc-tag.h"
#include "ns3/custom-header.h"
#include "ns3/packet.h"
#include "ns3/qbb-header.h"

#include <cmath>
#include <iostream>

using namespace ns3;

static bool
Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

int
main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    bool ok = true;
    ok = ok && Check(BccTag::StateToEcnBits(BccTag::TC) == 0x00, "TC must map to ECN 00");
    ok = ok && Check(BccTag::StateToEcnBits(BccTag::NC) == 0x01, "NC must map to ECN 01");
    ok = ok && Check(BccTag::StateToEcnBits(BccTag::TU) == 0x02, "TU must map to ECN 10");
    ok = ok && Check(BccTag::StateToEcnBits(BccTag::PC) == 0x03, "PC must map to ECN 11");
    ok = ok && Check(BccTag::EcnBitsToState(0x00) == BccTag::TC, "ECN 00 must map to TC");
    ok = ok && Check(BccTag::EcnBitsToState(0x01) == BccTag::NC, "ECN 01 must map to NC");
    ok = ok && Check(BccTag::EcnBitsToState(0x02) == BccTag::TU, "ECN 10 must map to TU");
    ok = ok && Check(BccTag::EcnBitsToState(0x03) == BccTag::PC, "ECN 11 must map to PC");

    qbbHeader hdr;
    hdr.SetPG(3);
    hdr.SetSeq(12345);
    hdr.SetSport(100);
    hdr.SetDport(200);
    hdr.SetIrnNack(777);
    hdr.SetIrnNackSize(88);
    hdr.SetBccFeedback(BccTag::TU, 0.42);

    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(hdr);
    qbbHeader decoded;
    p->RemoveHeader(decoded);

    ok = ok && Check(decoded.HasBccFeedback(), "qbbHeader should preserve BCC valid flag");
    ok = ok && Check(decoded.GetBccState() == BccTag::TU, "qbbHeader should preserve BCC state");
    ok = ok && Check(decoded.GetBccUtilizationQuantized() == BccTag::QuantizeUtilization(0.42),
                     "qbbHeader should preserve quantized utilization");
    ok = ok && Check(decoded.GetIrnNack() == 777, "qbbHeader should preserve IRN NACK seq");
    ok = ok && Check(decoded.GetIrnNackSize() == 88, "qbbHeader should preserve IRN NACK size");

    qbbHeader sizeProbe;
    ok = ok && Check(CustomHeader::GetAckSerializedSize() == sizeProbe.GetSerializedSize(),
                     "CustomHeader ACK size should match qbbHeader serialized size");

    double dequantized = BccTag::DequantizeUtilization(decoded.GetBccUtilizationQuantized());
    ok = ok && Check(dequantized > 0.0 && dequantized <= 1.0,
                     "dequantized utilization should stay in (0, 1]");

    if (!ok) {
        return 1;
    }
    std::cout << "bcc_header_feedback=pass\n";
    return 0;
}
