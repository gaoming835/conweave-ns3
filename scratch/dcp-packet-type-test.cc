#include <iostream>

#include "ns3/dcp-tag.h"
#include "ns3/packet.h"

using namespace ns3;

static bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

static bool CheckTag(const DcpTag &tag, uint8_t type) {
    bool ok = true;
    ok = Check(tag.GetPacketType() == type, "packet type mismatch") && ok;
    ok = Check(tag.GetFlowId() == 42, "flow id mismatch") && ok;
    ok = Check(tag.GetPsn() == 12345, "PSN mismatch") && ok;
    ok = Check(tag.GetSrc().Get() == Ipv4Address("11.0.0.1").Get(), "src mismatch") && ok;
    ok = Check(tag.GetDst().Get() == Ipv4Address("11.0.1.1").Get(), "dst mismatch") && ok;
    ok = Check(tag.GetSrcPort() == 10000, "src port mismatch") && ok;
    ok = Check(tag.GetDstPort() == 20000, "dst port mismatch") && ok;
    ok = Check(tag.GetPg() == 3, "PG mismatch") && ok;
    return ok;
}

int main() {
    bool ok = true;
    ok = Check(DcpTag::DCP_NON == 0, "DCP_NON value") && ok;
    ok = Check(DcpTag::DCP_ACK == 1, "DCP_ACK value") && ok;
    ok = Check(DcpTag::DCP_DATA == 2, "DCP_DATA value") && ok;
    ok = Check(DcpTag::DCP_HO == 3, "DCP_HO value") && ok;

    DcpTag dataTag;
    dataTag.SetPacketType(DcpTag::DCP_DATA);
    dataTag.SetOriginalData(42, 12345, Ipv4Address("11.0.0.1"), Ipv4Address("11.0.1.1"), 10000,
                            20000, 3);
    ok = CheckTag(dataTag, DcpTag::DCP_DATA) && ok;

    Ptr<Packet> packet = Create<Packet>(128);
    packet->AddPacketTag(dataTag);
    DcpTag decodedData;
    ok = Check(packet->PeekPacketTag(decodedData), "missing serialized DATA tag") && ok;
    ok = CheckTag(decodedData, DcpTag::DCP_DATA) && ok;

    DcpTag hoTag;
    hoTag.SetPacketType(DcpTag::DCP_HO);
    hoTag.SetOriginalData(42, 12345, Ipv4Address("11.0.0.1"), Ipv4Address("11.0.1.1"), 10000,
                          20000, 3);
    ok = CheckTag(hoTag, DcpTag::DCP_HO) && ok;

    if (!ok) {
        return 1;
    }

    std::cout << "dcp_packet_type_unit=pass" << std::endl;
    return 0;
}
