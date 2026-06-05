#include "ns3/bcc-tag.h"
#include "ns3/packet.h"

#include <iostream>
#include <string>

using namespace ns3;

static BccTag
MakeBccTag(uint8_t state, uint32_t switchId, uint32_t queueLen) {
    BccTag tag;
    tag.SetState(state);
    tag.SetSwitchId(switchId);
    tag.SetEgressPort(switchId + 1);
    tag.SetQueueLen(queueLen);
    tag.SetQueueSlope(queueLen / 1000.0);
    tag.SetLinkUtilization(0.5);
    tag.SetTimestampNs(switchId * 100);
    return tag;
}

static void
ApplyLocalBccMark(Ptr<Packet> p, const BccTag &localTag) {
    BccTag pathTag;
    bool hasPathTag = p->PeekPacketTag(pathTag);
    if (hasPathTag &&
        !BccTag::ShouldReplacePathState(localTag.GetState(), pathTag.GetState())) {
        return;
    }
    if (hasPathTag) {
        p->RemovePacketTag(pathTag);
    }
    p->AddPacketTag(localTag);
}

static bool
CheckMerge(const std::string &name, bool hasExisting, uint8_t existingState,
           uint8_t localState, uint8_t expectedState, uint32_t expectedSwitchId) {
    Ptr<Packet> p = Create<Packet>();
    if (hasExisting) {
        p->AddPacketTag(MakeBccTag(existingState, 10, 1000));
    }

    ApplyLocalBccMark(p, MakeBccTag(localState, 20, 2000));

    BccTag merged;
    if (!p->PeekPacketTag(merged)) {
        std::cerr << name << ": missing merged BCC tag\n";
        return false;
    }
    if (merged.GetState() != expectedState) {
        std::cerr << name << ": expected state " << BccTag::StateToString(expectedState)
                  << " got " << BccTag::StateToString(merged.GetState()) << "\n";
        return false;
    }
    if (merged.GetSwitchId() != expectedSwitchId) {
        std::cerr << name << ": expected selected switch " << expectedSwitchId
                  << " got " << merged.GetSwitchId() << "\n";
        return false;
    }
    uint32_t expectedQueueLen = expectedSwitchId == 10 ? 1000 : 2000;
    if (merged.GetQueueLen() != expectedQueueLen) {
        std::cerr << name << ": expected queue telemetry " << expectedQueueLen
                  << " got " << merged.GetQueueLen() << "\n";
        return false;
    }
    return true;
}

int
main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    bool ok = true;
    ok = ok && BccTag::GetStatePriority(BccTag::TC) > BccTag::GetStatePriority(BccTag::PC);
    ok = ok && BccTag::GetStatePriority(BccTag::PC) > BccTag::GetStatePriority(BccTag::NC);
    ok = ok && BccTag::GetStatePriority(BccTag::NC) > BccTag::GetStatePriority(BccTag::TU);
    if (!ok) {
        std::cerr << "BCC priority order must be TC > PC > NC > TU\n";
        return 1;
    }

    ok = CheckMerge("first hop TC, later hop NC", true, BccTag::TC, BccTag::NC,
                    BccTag::TC, 10) &&
         CheckMerge("first hop NC, later hop TC", true, BccTag::NC, BccTag::TC,
                    BccTag::TC, 20) &&
         CheckMerge("path has PC and TU", true, BccTag::PC, BccTag::TU,
                    BccTag::PC, 10) &&
         CheckMerge("path has TU then PC", true, BccTag::TU, BccTag::PC,
                    BccTag::PC, 20) &&
         CheckMerge("path has only TU feedback", false, BccTag::NC, BccTag::TU,
                    BccTag::TU, 20);

    if (!ok) {
        return 1;
    }

    std::cout << "bcc_priority_merge=pass\n";
    return 0;
}
