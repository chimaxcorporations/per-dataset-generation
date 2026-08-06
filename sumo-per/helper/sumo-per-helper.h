#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ns3/node-container.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/wifi-phy.h"

#include "../model/sumo-client.h"
#include "../model/cam-broadcast-app.h"

namespace ns3
{
// class CamBroadcastApp;
class MobilityModel;
class Node;
} // namespace ns3

class SumoPerHelper
{
public:
  struct Config
  {
    // Experiment identity
    std::string runId{"julich_d10_r2_log_s01"};
    std::string scenarioId{"julich"};
    std::string outputDir{"results/julich_d10_r2_log_s01"};

    // Reproducibility
    uint32_t ns3Seed{12345};
    uint32_t ns3Run{1};
    int32_t sumoSeed{1};

    // ns-3 timing
    double warmupSec{60.0};
    double simEndSec{660.0};
    double stepSec{0.1};
    double windowSec{1.0};

    // SUMO timing
    double sumoBeginSec{28800.0}; // 08:00
    double sumoEndSec{32400.0};   // 09:00

    // Traffic/application
    double trafficStartSec{0.0};
    double beaconIntervalSec{0.5}; // 2 Hz
    uint32_t beaconSizeBytes{300};
    uint32_t maxVehicles{100};

    // Receiver eligibility and retained observations
    double evaluationRadiusM{500.0};
    double maxRetainedRadiusM{1000.0};

    // Network
    uint16_t port{5000};

    // PHY/MAC
    std::string propagation{"log-distance"};
    double pathLossExponent{2.0};
    double referenceDistanceM{1.0};
    double referenceLossDb{46.6777};
    double txPowerDbm{20.0};
    std::string dataMode{"OfdmRate6MbpsBW10MHz"};

    // CBR measurement
    double cbrSamplePeriodSec{0.001};

    // Output
    std::string outCsv{"window_1s.csv"};
    bool failIfOutputExists{true};
  };

  explicit SumoPerHelper(const Config& cfg);

  void Run(const std::string& sumocfg, bool gui);

private:
  using OpportunityKey = std::tuple<uint32_t, uint32_t, uint32_t>;

  struct Opportunity
  {
    double txTimeSec{0.0};
    std::string srcVehicle;
    std::string dstVehicle;
    uint32_t srcNodeId{0};
    uint32_t dstNodeId{0};
    uint32_t sequence{0};
    double distanceM{0.0};
    double srcSpeedMps{0.0};
    double dstSpeedMps{0.0};
    bool received{false};
    double rssiSumDbm{0.0};
    double snrSumDb{0.0};
    uint32_t phySamples{0};
  };

  // Configuration
  void ValidateConfig() const;

  // Vehicle-to-node pool mapping
  ns3::Ptr<ns3::Node> AssignNodeToVehicle(const std::string& vehId);
  void ReleaseVehicle(const std::string& vehId);

  // SUMO-to-ns-3 mobility synchronization
  void UpdateMobilityFromSumo(
      const std::vector<SumoVehicleState>& vehicles);

  // One-time ns-3 installation
  void EnsureWifiAndApps();

  // Packet and PHY trace callbacks
  void RxPacket(ns3::Ptr<ns3::Socket> socket);
  void TxTrace(uint32_t nodeId, uint32_t seq);
  void MonitorRxTrace(std::string context,
                      ns3::Ptr<const ns3::Packet> packet,
                      uint16_t channelFreqMhz,
                      ns3::WifiTxVector txVector,
                      ns3::MpduInfo mpdu,
                      ns3::SignalNoiseDbm signalNoise,
                      uint16_t staId);

  // Periodic simulation callbacks
  void StepOnce();
  void SampleCbr();
  void FlushWindow();

  double DistanceMeters(uint32_t idxA, uint32_t idxB) const;

  // Configuration and output state
  Config m_cfg;
  std::string m_windowCsvPath;

  // SUMO interface
  SumoClient m_sumo;

  // ns-3 node pool and active vehicle assignments
  ns3::NodeContainer m_pool;
  bool m_wifiInstalled{false};
  std::vector<uint32_t> m_freeIdx;
  std::unordered_map<std::string, uint32_t> m_vehToIdx;
  std::unordered_map<uint32_t, std::string> m_idxToVeh;
  std::unordered_map<std::string, double> m_speedById;

  // One receive socket and one PHY reference per pool node
  std::vector<ns3::Ptr<ns3::Socket>> m_rxSockets;
  std::vector<ns3::Ptr<ns3::CamBroadcastApp>> m_camApps;
  std::unordered_map<uint32_t, ns3::Ptr<ns3::WifiPhy>> m_phyByNodeId;

  // Packet-time receiver opportunities.  A key is (source node, CAM
  // sequence, destination node). Vehicle IDs are stored in the value so a
  // pool node can be safely reused after a vehicle departs.
  std::map<OpportunityKey, Opportunity> m_opportunities;

  // Last observed receiver-level PHY values, retained for diagnostics
  std::unordered_map<uint32_t, double> m_lastRssiDbmByNode;
  std::unordered_map<uint32_t, double> m_lastSnrDbByNode;

  // Per-window CBR sampling and completed-window snapshot
  std::unordered_map<std::string, uint32_t> m_cbrBusySamples;
  std::unordered_map<std::string, uint32_t> m_cbrTotalSamples;
  std::unordered_map<std::string, double> m_cbrLastWindow;

  // PHY trace diagnostics
  uint64_t m_monRxCalls{0};
  uint64_t m_monRxTagged{0};
};