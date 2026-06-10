#include <iostream>

#include "ns3/dcp-tag.h"
#include "ns3/ipv4-header.h"
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
    ok = Check(tag.GetMsn() == 7, "MSN mismatch") && ok;
    ok = Check(tag.GetEmsn() == 8, "eMSN mismatch") && ok;
    ok = Check(tag.GetSRetryNo() == 2, "sRetryNo mismatch") && ok;
    ok = Check(tag.GetMessageSize() == 4096, "message size mismatch") && ok;
    ok = Check(tag.GetMessageOffset() == 1024, "message offset mismatch") && ok;
    return ok;
}

static bool CheckIpType(uint8_t type, uint8_t ecnBits) {
    Ipv4Header header;
    header.SetTos(0xa0 | ecnBits);
    DcpTag::SetDcpTypeInIpHeader(header, type);
    uint8_t expectedTos = (0xa0 & 0xf3) | ((type & 0x03) << 2) | (ecnBits & 0x03);
    bool ok = true;
    ok = Check(header.GetTos() == expectedTos, "DCP ToS layout mismatch") && ok;
    ok = Check((header.GetTos() & 0x03) == (ecnBits & 0x03), "ECN bits not preserved") && ok;
    ok = Check(DcpTag::GetDcpTypeFromIpHeader(header) == type, "DCP IP header type mismatch") &&
         ok;
    ok = Check(DcpTag::GetDcpTypeFromTos(header.GetTos()) == type, "DCP ToS type mismatch") &&
         ok;
    return ok;
}

int main() {
    bool ok = true;
    ok = Check(DcpTag::DCP_NON == 0, "DCP_NON value") && ok;
    ok = Check(DcpTag::DCP_ACK == 1, "DCP_ACK value") && ok;
    ok = Check(DcpTag::DCP_DATA == 2, "DCP_DATA value") && ok;
    ok = Check(DcpTag::DCP_HO == 3, "DCP_HO value") && ok;
    ok = Check(DcpTag::GetDcpTypeFromTos(0) == DcpTag::DCP_NON, "DCP_NON ToS decode") && ok;
    ok = CheckIpType(DcpTag::DCP_ACK, 0x01) && ok;
    ok = CheckIpType(DcpTag::DCP_DATA, 0x02) && ok;
    ok = CheckIpType(DcpTag::DCP_HO, 0x03) && ok;

    DcpTag dataTag;
    dataTag.SetPacketType(DcpTag::DCP_DATA);
    dataTag.SetOriginalData(42, 12345, Ipv4Address("11.0.0.1"), Ipv4Address("11.0.1.1"), 10000,
                            20000, 3);
    dataTag.SetMessageMetadata(7, 8, 2, 4096, 1024);
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
    hoTag.SetMessageMetadata(7, 8, 2, 4096, 1024);
    ok = CheckTag(hoTag, DcpTag::DCP_HO) && ok;

    if (!ok) {
        return 1;
    }

    std::cout << "dcp_packet_type_unit=pass" << std::endl;
    return 0;
}
