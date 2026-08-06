#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include "ns3/ptr.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/vector.h"

#include "sumo-client.h"
#include "per-collector.h"

namespace ns3 {
class MobilityModel;
}

class SumoPerHelper
{
public:
  struct Config
  {
    double stepSec{0.1};
    double windowSec{1.0};
    double simEndSec{60.0};
    std::string outCsv{"per_dataset.csv"};

    // Wi-Fi beacon traffic
    double beaconIntervalSec{0.1};
    uint32_t beaconSizeBytes{300};
  };

  explicit SumoPerHelper(const Config& cfg);

  void Run(const std::string& sumocfg, bool gui);

private:
  ns3::Ptr<ns3::Node> GetOrCreateNode(const std::string& vehId);

  void UpdateMobilityFromSumo(const std::vector<SumoVehicleState>& vehicles);
  void EnsureWifiAndApps();

  void ConnectTraces();
  void ScheduleBeacon(ns3::Ptr<ns3::Node> src, ns3::Ptr<ns3::Node> dst);

  // feature computation
  void ComputeDistances(std::map<PerCollector::LinkKey, double>& distByLink) const;

private:
  Config m_cfg;

  SumoClient m_sumo;
  PerCollector m_collector;

  // vehicleId -> ns-3 node
  std::unordered_map<std::string, ns3::Ptr<ns3::Node>> m_nodesById;

  // speeds updated from SUMO
  std::map<std::string, double> m_speedById;

  bool m_wifiInstalled{false};
  // ----PRR window state ----
  std::unordered_map<uint32_t, uint32_t> m_txThisWindow;
  std::map<std::pair<uint32_t,uint32_t>, uint32_t> m_rxThisWindow;
  std::vector<ns3::Ptr<ns3::Socket>> m_rxSockets;
  
  void RxPacket(ns3::Ptr<ns3::Socket> sock);
  void FlushWindow();

};
