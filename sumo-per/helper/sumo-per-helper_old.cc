#include "sumo-per-helper.h"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"

#include <iostream>
#include <cmath>

using namespace ns3;

SumoPerHelper::SumoPerHelper(const Config& cfg) : m_cfg(cfg) {}

Ptr<Node>
SumoPerHelper::GetOrCreateNode(const std::string& vehId)
{
  auto it = m_nodesById.find(vehId);
  if (it != m_nodesById.end())
    {
      return it->second;
    }

  Ptr<Node> n = CreateObject<Node>();
  m_nodesById[vehId] = n;

  // Attach constant-position mobility (updated each step from SUMO)
  Ptr<ConstantPositionMobilityModel> mob = CreateObject<ConstantPositionMobilityModel>();
  n->AggregateObject(mob);

  return n;
}

void
SumoPerHelper::UpdateMobilityFromSumo(const std::vector<SumoVehicleState>& vehicles)
{
  for (const auto& v : vehicles)
    {
      Ptr<Node> n = GetOrCreateNode(v.id);
      auto mob = n->GetObject<MobilityModel>();
      if (mob)
        {
          // SUMO uses meters in its x/y coordinate system.
          mob->SetPosition(Vector(v.x, v.y, 0.0));
        }
      m_speedById[v.id] = v.speed;
    }
}

void
SumoPerHelper::EnsureWifiAndApps()
{
  if (m_wifiInstalled)
    {
      return;
    }

  NodeContainer nodes;
  for (const auto& kv : m_nodesById)
    {
      nodes.Add(kv.second);
    }

  // 802.11p-like ad-hoc baseline (you can swap to 802.11p / 802.11ax as needed)
  WifiHelper wifi;
  wifi.SetStandard(WIFI_STANDARD_80211a); // placeholder; swap to 802.11p model if you use wave/802.11p module

  YansWifiPhyHelper phy = YansWifiPhyHelper::Default();
  YansWifiChannelHelper chan = YansWifiChannelHelper::Default();
  phy.SetChannel(chan.Create());

  WifiMacHelper mac;
  mac.SetType("ns3::AdhocWifiMac");

  NetDeviceContainer devs = wifi.Install(phy, mac, nodes);

  InternetStackHelper internet;
  internet.Install(nodes);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.0.0.0", "255.255.0.0");
  ipv4.Assign(devs);

  // Simple UDP "beacons" to create packet events (Tx/RxOk counters)
  // For dataset realism, replace with CAM-like periodic broadcast or application layer message generator.
  uint16_t port = 5000;

  for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
      // Receiver on every node
      UdpServerHelper server(port);
      ApplicationContainer apps = server.Install(nodes.Get(i));
      apps.Start(Seconds(0.0));
      apps.Stop(Seconds(m_cfg.simEndSec + 1.0));
    }

  // One-to-many: each node sends to all others periodically (O(N^2) traffic; keep N modest)
  for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
      for (uint32_t j = 0; j < nodes.GetN(); ++j)
        {
          if (i == j) continue;
          // Send to node j unicast (simpler tracing than broadcast in a minimal template)
          auto ipv4i = nodes.Get(j)->GetObject<Ipv4>();
          Ipv4Address dst = ipv4i->GetAddress(1,0).GetLocal();

          UdpClientHelper client(dst, port);
          client.SetAttribute("Interval", TimeValue(Seconds(m_cfg.beaconIntervalSec)));
          client.SetAttribute("PacketSize", UintegerValue(m_cfg.beaconSizeBytes));
          client.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));

          ApplicationContainer cApps = client.Install(nodes.Get(i));
          cApps.Start(Seconds(0.1));
          cApps.Stop(Seconds(m_cfg.simEndSec + 1.0));
        }
    }

  m_wifiInstalled = true;

  // NOTE: because nodes can appear later (SUMO), for a production system:
  // - keep a "node pool" (max vehicles) pre-created, or
  // - re-run installations when new nodes appear (more complex).
}

void
SumoPerHelper::ConnectTraces()
{
  // Minimal template: Use FlowMonitor for Rx/Tx counts per flow, then map to (src,dst).
  // For true PER at MAC/PHY, extend to connect to WifiMac/Phy traces.
  // Here we keep hooks as TODOs and provide a stable dataset pipeline.

  // TODO: Connect to WifiMac Tx trace:
  // Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTx", MakeCallback(...));
  //
  // TODO: Connect to WifiPhy RxOk trace:
  // Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyRxEnd", MakeCallback(...));
}

void
SumoPerHelper::ComputeDistances(std::map<PerCollector::LinkKey, double>& distByLink) const
{
  // Compute distances for all known node pairs at flush time
  for (const auto& a : m_nodesById)
    {
      for (const auto& b : m_nodesById)
        {
          if (a.first == b.first) continue;
          Ptr<MobilityModel> ma = a.second->GetObject<MobilityModel>();
          Ptr<MobilityModel> mb = b.second->GetObject<MobilityModel>();
          if (!ma || !mb) continue;
          const Vector pa = ma->GetPosition();
          const Vector pb = mb->GetPosition();
          const double dx = pa.x - pb.x;
          const double dy = pa.y - pb.y;
          const double dist = std::sqrt(dx*dx + dy*dy);
          distByLink[{a.first, b.first}] = dist;
        }
    }
}

void
SumoPerHelper::Run(const std::string& sumocfg, bool gui)
{
  // Start SUMO (if available)
  m_sumo.Start(sumocfg, gui);

  // Configure ns-3 time resolution
  Time::SetResolution(Time::NS);

  double nextFlush = m_cfg.windowSec;

  for (double t = 0.0; t < m_cfg.simEndSec; t += m_cfg.stepSec)
    {
      // 1) step SUMO & update ns-3 mobility
      if (m_sumo.IsEnabled())
        {
          m_sumo.Step(m_cfg.stepSec);
          auto veh = m_sumo.GetVehicles();
          UpdateMobilityFromSumo(veh);
        }
      else
        {
          // no-SUMO mode: keep a tiny static set (for pipeline testing)
          if (m_nodesById.empty())
            {
              for (int i=0; i<5; ++i)
                {
                  std::string id = "n" + std::to_string(i);
                  Ptr<Node> n = GetOrCreateNode(id);
                  n->GetObject<MobilityModel>()->SetPosition(Vector(50.0*i, 0.0, 0.0));
                  m_speedById[id] = 0.0;
                }
            }
        }

      // 2) Ensure Wi-Fi stack and apps installed after first nodes exist
      if (!m_nodesById.empty())
        {
          EnsureWifiAndApps();
          // ConnectTraces(); // TODO: enable for MAC/PHY-level PER counting
        }

      // 3) Run ns-3 for this step
      Simulator::Stop(Seconds(m_cfg.stepSec));
      Simulator::Run();
      Simulator::Destroy();

      // 4) Flush dataset every window
      if (t + m_cfg.stepSec + 1e-9 >= nextFlush)
        {
          std::map<PerCollector::LinkKey, double> distByLink;
          ComputeDistances(distByLink);

          // NOTE: In this template, Tx/RxOk counters are not incremented yet because
          // we didn't connect trace callbacks. For a production paper-matching system,
          // attach MAC/PHY traces and call m_collector.CountTx / CountRxOk accordingly.
          m_collector.FlushToCsv(nextFlush, m_speedById, distByLink, m_cfg.outCsv);

          nextFlush += m_cfg.windowSec;
        }
    }

  m_sumo.Close();
}
