#pragma once

#include <cstdint>

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"
#include "ns3/nstime.h"
#include "cam-tag.h"
namespace ns3 {

class CamBroadcastApp : public Application
{
public:
  CamBroadcastApp();
  ~CamBroadcastApp() override;

  static TypeId GetTypeId();

  void Setup(uint16_t port, uint32_t pktSize, double intervalSec, bool enable);
  void SetEnable(bool en);

protected:
  void StartApplication() override;
  void StopApplication() override;

private:
  void SendOne();
  void ScheduleNext();

private:
  Ptr<Socket> m_sock;
  EventId m_ev;

  uint16_t m_port{5000};
  uint32_t m_pktSize{300};
  Time m_interval{MilliSeconds(100)};
  bool m_enabled{true};

  uint32_t m_seq{0};

  // Trace: (nodeId, seq)
  TracedCallback<uint32_t, uint32_t> m_txTrace;
};

} // namespace ns3
