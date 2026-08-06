#include "cam-broadcast-app.h"
#include "cam-header.h"

#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/random-variable-stream.h"
#include "ns3/abort.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("CamBroadcastApp");
NS_OBJECT_ENSURE_REGISTERED(CamBroadcastApp);

TypeId
CamBroadcastApp::GetTypeId()
{
  static TypeId tid = TypeId("ns3::CamBroadcastApp")
    .SetParent<Application>()
    .SetGroupName("SumoPer")
    .AddConstructor<CamBroadcastApp>()
    .AddTraceSource("Tx",
                    "Fired when this app transmits a CAM-like packet. Args: nodeId, seq.",
                    MakeTraceSourceAccessor(&CamBroadcastApp::m_txTrace),
                    "ns3::TracedCallback::Uint32Uint32");
  return tid;
}

CamBroadcastApp::CamBroadcastApp() = default;
CamBroadcastApp::~CamBroadcastApp() = default;

void
CamBroadcastApp::Setup(uint16_t port, uint32_t pktSize, double intervalSec, bool enable)
{
  m_port = port;
  m_pktSize = pktSize;
  m_interval = Seconds(intervalSec);
  m_enabled = enable;
}

void
CamBroadcastApp::SetEnable(bool en)
{
  m_enabled = en;
}

void
CamBroadcastApp::StartApplication()
{
  if (!m_sock)
    {
      m_sock = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
      m_sock->Bind();                 // ephemeral source port
      m_sock->SetAllowBroadcast(true);
      m_sock->SetIpTtl(1);
    }

  // One-time startup jitter to avoid global synchronization collisions
  Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
  Time jitter = MilliSeconds(uv->GetInteger(0, 100));
  m_ev = Simulator::Schedule(jitter, &CamBroadcastApp::SendOne, this);
}

void
CamBroadcastApp::StopApplication()
{
  if (m_ev.IsPending())
    {
      Simulator::Cancel(m_ev);
    }
  if (m_sock)
    {
      m_sock->Close();
      m_sock = nullptr;
    }
}

void
CamBroadcastApp::SendOne()
{
  if (m_enabled && m_sock)
    {
      CamHeader h;
      NS_ABORT_MSG_IF(m_pktSize < h.GetSerializedSize(),
                      "CAM packet size is smaller than CamHeader");
      Ptr<Packet> p =
          Create<Packet>(m_pktSize - h.GetSerializedSize());
      h.SetSrcId(GetNode()->GetId());
      h.SetSeq(m_seq);
      p->AddHeader(h);
      p->AddByteTag(CamTag(h.GetSrcId(), h.GetSeq()));

      // subnet broadcast for 10.0.0.0/16
      InetSocketAddress bcast = InetSocketAddress(Ipv4Address("10.0.255.255"), m_port);
      const int sent = m_sock->SendTo(p, 0, bcast);
      if (sent >= 0)
        {
          m_txTrace(GetNode()->GetId(), m_seq);
          ++m_seq;
        }
      else
        {
          NS_LOG_WARN("CAM socket transmission failed on node "
                      << GetNode()->GetId());
        }
    }

  ScheduleNext();

}
void
CamBroadcastApp::ScheduleNext()
{
  m_ev = Simulator::Schedule(m_interval,
                             &CamBroadcastApp::SendOne,
                             this);
}
} // namespace ns3
