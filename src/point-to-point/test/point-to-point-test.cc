#include "ns3/test.h"
#include "ns3/bcc-tag.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"

namespace ns3 {

class PointToPointTest : public TestCase
{
public:
  PointToPointTest ();

  virtual void DoRun (void);

private:
  void SendOnePacket (Ptr<PointToPointNetDevice> device);
};

PointToPointTest::PointToPointTest ()
  : TestCase ("PointToPoint")
{
}

void
PointToPointTest::SendOnePacket (Ptr<PointToPointNetDevice> device)
{
  Ptr<Packet> p = Create<Packet> ();
  device->Send (p, device->GetBroadcast (), 0x800);
}


void
PointToPointTest::DoRun (void)
{
  Ptr<Node> a = CreateObject<Node> ();
  Ptr<Node> b = CreateObject<Node> ();
  Ptr<PointToPointNetDevice> devA = CreateObject<PointToPointNetDevice> ();
  Ptr<PointToPointNetDevice> devB = CreateObject<PointToPointNetDevice> ();
  Ptr<PointToPointChannel> channel = CreateObject<PointToPointChannel> ();

  devA->Attach (channel);
  devA->SetAddress (Mac48Address::Allocate ());
  devA->SetQueue (CreateObject<DropTailQueue> ());
  devB->Attach (channel);
  devB->SetAddress (Mac48Address::Allocate ());
  devB->SetQueue (CreateObject<DropTailQueue> ());

  a->AddDevice (devA);
  b->AddDevice (devB);

  Simulator::Schedule (Seconds (1.0), &PointToPointTest::SendOnePacket, this, devA);

  Simulator::Run ();

  Simulator::Destroy ();
}

static BccTag
MakeBccTag (uint8_t state, uint32_t switchId, uint32_t queueLen)
{
  BccTag tag;
  tag.SetState (state);
  tag.SetSwitchId (switchId);
  tag.SetEgressPort (switchId + 1);
  tag.SetQueueLen (queueLen);
  tag.SetQueueSlope (queueLen / 1000.0);
  tag.SetLinkUtilization (0.5);
  tag.SetTimestampNs (switchId * 100);
  return tag;
}

static void
ApplyLocalBccMark (Ptr<Packet> p, const BccTag &localTag)
{
  BccTag pathTag;
  bool hasPathTag = p->PeekPacketTag (pathTag);
  if (hasPathTag &&
      !BccTag::ShouldReplacePathState (localTag.GetState (), pathTag.GetState ()))
    {
      return;
    }
  if (hasPathTag)
    {
      p->RemovePacketTag (pathTag);
    }
  p->AddPacketTag (localTag);
}

class BccPriorityMergeTest : public TestCase
{
public:
  BccPriorityMergeTest ();

  virtual void DoRun (void);

private:
  void CheckMerge (const std::string &name, bool hasExisting, uint8_t existingState,
                   uint8_t localState, uint8_t expectedState, uint32_t expectedSwitchId);
};

BccPriorityMergeTest::BccPriorityMergeTest ()
  : TestCase ("BCC path-state priority merge")
{
}

void
BccPriorityMergeTest::CheckMerge (const std::string &name, bool hasExisting,
                                  uint8_t existingState, uint8_t localState,
                                  uint8_t expectedState, uint32_t expectedSwitchId)
{
  Ptr<Packet> p = Create<Packet> ();
  if (hasExisting)
    {
      p->AddPacketTag (MakeBccTag (existingState, 10, 1000));
    }

  ApplyLocalBccMark (p, MakeBccTag (localState, 20, 2000));

  BccTag merged;
  NS_TEST_ASSERT_MSG_EQ (p->PeekPacketTag (merged), true, name << " should retain a BCC tag");
  NS_TEST_ASSERT_MSG_EQ (merged.GetState (), expectedState,
                         name << " selected wrong BCC path state");
  NS_TEST_ASSERT_MSG_EQ (merged.GetSwitchId (), expectedSwitchId,
                         name << " selected debug fields from wrong hop");
  uint32_t expectedQueueLen = expectedSwitchId == 10 ? 1000 : 2000;
  NS_TEST_ASSERT_MSG_EQ (merged.GetQueueLen (), expectedQueueLen,
                         name << " selected queue telemetry from wrong hop");
}

void
BccPriorityMergeTest::DoRun (void)
{
  bool tcOutranksPc = BccTag::GetStatePriority (BccTag::TC) >
                      BccTag::GetStatePriority (BccTag::PC);
  bool pcOutranksNc = BccTag::GetStatePriority (BccTag::PC) >
                      BccTag::GetStatePriority (BccTag::NC);
  bool ncOutranksTu = BccTag::GetStatePriority (BccTag::NC) >
                      BccTag::GetStatePriority (BccTag::TU);
  NS_TEST_ASSERT_MSG_EQ (tcOutranksPc, true, "TC should outrank PC");
  NS_TEST_ASSERT_MSG_EQ (pcOutranksNc, true, "PC should outrank NC");
  NS_TEST_ASSERT_MSG_EQ (ncOutranksTu, true, "NC should outrank TU");

  CheckMerge ("first hop TC, later hop NC", true, BccTag::TC, BccTag::NC,
              BccTag::TC, 10);
  CheckMerge ("first hop NC, later hop TC", true, BccTag::NC, BccTag::TC,
              BccTag::TC, 20);
  CheckMerge ("path has PC and TU", true, BccTag::PC, BccTag::TU,
              BccTag::PC, 10);
  CheckMerge ("path has TU then PC", true, BccTag::TU, BccTag::PC,
              BccTag::PC, 20);
  CheckMerge ("path has only TU feedback", false, BccTag::NC, BccTag::TU,
              BccTag::TU, 20);
}
//-----------------------------------------------------------------------------
class PointToPointTestSuite : public TestSuite
{
public:
  PointToPointTestSuite ();
};

PointToPointTestSuite::PointToPointTestSuite ()
  : TestSuite ("devices-point-to-point", UNIT)
{
  AddTestCase (new PointToPointTest);
  AddTestCase (new BccPriorityMergeTest);
}

static PointToPointTestSuite g_pointToPointTestSuite;

} // namespace ns3
