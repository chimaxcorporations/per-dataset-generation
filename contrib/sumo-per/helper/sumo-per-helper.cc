#include "sumo-per-helper.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-module.h"

#include "../model/cam-broadcast-app.h"
#include "../model/cam-header.h"
#include "../model/cam-tag.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SumoPerHelper");

namespace
{

void
EnsureCsvHeader(const std::string& path)
{
  namespace fs = std::filesystem;
  const fs::path csvPath(path);

  if (csvPath.has_parent_path())
    {
      std::error_code ec;
      fs::create_directories(csvPath.parent_path(), ec);
      NS_ABORT_MSG_IF(ec,
                      "Cannot create CSV directory "
                          << csvPath.parent_path().string() << ": "
                          << ec.message());
    }

  if (fs::exists(csvPath))
    {
      NS_ABORT_MSG_IF(!fs::is_regular_file(csvPath),
                      "CSV path is not a regular file: "
                          << csvPath.string());
      return;
    }

  std::ofstream out(csvPath, std::ios::out);
  NS_ABORT_MSG_IF(!out.is_open(),
                  "Cannot create output CSV: " << csvPath.string());

  out << "run_id,scenario_id,ns3_seed,ns3_run,sumo_seed,propagation,"
         "window_start_s,window_end_s,src,dst,tx,rx_ok,per,dist_m,"
         "within_evaluation_radius,v_src_mps,v_dst_mps,rssi_dbm,snr_db,"
         "phy_n,cbr_tx,cbr_rx,cbr_global\n";
  NS_ABORT_MSG_IF(!out.good(),
                  "Failed to write CSV header: " << csvPath.string());
}

std::string
CsvField(const std::string& value)
{
  if (value.find_first_of(",\"\r\n") == std::string::npos)
    {
      return value;
    }

  std::string escaped{"\""};
  for (const char ch : value)
    {
      escaped += ch == '\"' ? "\"\"" : std::string(1, ch);
    }
  escaped += '\"';
  return escaped;
}

uint32_t
ExtractNodeIdFromContext(const std::string& context)
{
  const std::string key = "/NodeList/";
  const auto pos = context.find(key);
  if (pos == std::string::npos)
    {
      return std::numeric_limits<uint32_t>::max();
    }

  size_t i = pos + key.size();
  if (i >= context.size() ||
      !std::isdigit(static_cast<unsigned char>(context[i])))
    {
      return std::numeric_limits<uint32_t>::max();
    }

  uint32_t nodeId = 0;
  while (i < context.size() &&
         std::isdigit(static_cast<unsigned char>(context[i])))
    {
      nodeId = nodeId * 10u + static_cast<uint32_t>(context[i] - '0');
      ++i;
    }
  return nodeId;
}

} // namespace

SumoPerHelper::SumoPerHelper(const Config& cfg)
  : m_cfg(cfg)
{
}

Ptr<Node>
SumoPerHelper::AssignNodeToVehicle(const std::string& vehId)
{
  const auto existing = m_vehToIdx.find(vehId);
  if (existing != m_vehToIdx.end())
    {
      return m_pool.Get(existing->second);
    }

  if (m_freeIdx.empty())
    {
      NS_FATAL_ERROR("Node pool exhausted; increase maxVehicles. vehId="
                     << vehId);
    }

  const uint32_t idx = m_freeIdx.back();
  m_freeIdx.pop_back();
  m_vehToIdx.emplace(vehId, idx);
  m_idxToVeh.emplace(idx, vehId);

  NS_ABORT_MSG_IF(idx >= m_camApps.size(),
                  "CAM application missing for pool index " << idx);
  m_camApps[idx]->SetEnable(true);

  Ptr<MobilityModel> mobility = m_pool.Get(idx)->GetObject<MobilityModel>();
  if (mobility)
    {
      mobility->SetPosition(Vector(0.0, 0.0, 0.0));
    }
  return m_pool.Get(idx);
}

void
SumoPerHelper::ReleaseVehicle(const std::string& vehId)
{
  const auto mapping = m_vehToIdx.find(vehId);
  if (mapping == m_vehToIdx.end())
    {
      return;
    }

  const uint32_t idx = mapping->second;
  const uint32_t nodeId = m_pool.Get(idx)->GetId();

  NS_ABORT_MSG_IF(idx >= m_camApps.size(),
                  "CAM application missing for pool index " << idx);
  m_camApps[idx]->SetEnable(false);

  // Node-level sampling state must not leak to the next vehicle assigned to
  // this pool slot. Packet opportunities retain their own vehicle identity.
  m_lastRssiDbmByNode.erase(nodeId);
  m_lastSnrDbByNode.erase(nodeId);
  m_vehToIdx.erase(mapping);
  m_idxToVeh.erase(idx);
  m_freeIdx.push_back(idx);
}

void
SumoPerHelper::UpdateMobilityFromSumo(
    const std::vector<SumoVehicleState>& vehicles)
{
  std::set<std::string> active;
  for (const auto& vehicle : vehicles)
    {
      active.insert(vehicle.id);
      Ptr<Node> node = AssignNodeToVehicle(vehicle.id);
      if (!node)
        {
          continue;
        }

      Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
      if (mobility)
        {
          mobility->SetPosition(Vector(vehicle.x, vehicle.y, 0.0));
        }
      m_speedById[vehicle.id] = vehicle.speed;
    }

  std::vector<std::string> departed;
  departed.reserve(m_vehToIdx.size());
  for (const auto& mapping : m_vehToIdx)
    {
      if (active.find(mapping.first) == active.end())
        {
          departed.push_back(mapping.first);
        }
    }

  for (const auto& vehId : departed)
    {
      ReleaseVehicle(vehId);
      m_speedById.erase(vehId);
    }
}

void
SumoPerHelper::EnsureWifiAndApps()
{
  if (m_wifiInstalled)
    {
      return;
    }

  m_pool.Create(m_cfg.maxVehicles);

  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(m_pool);

  WifiHelper wifi;
  wifi.SetStandard(WIFI_STANDARD_80211p);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode",
                               StringValue(m_cfg.dataMode),
                               "ControlMode",
                               StringValue(m_cfg.dataMode));

  YansWifiChannelHelper channel;
  channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                             "Exponent",
                             DoubleValue(m_cfg.pathLossExponent),
                             "ReferenceDistance",
                             DoubleValue(m_cfg.referenceDistanceM),
                             "ReferenceLoss",
                             DoubleValue(m_cfg.referenceLossDb));
  if (m_cfg.propagation == "log-distance-nakagami")
    {
      channel.AddPropagationLoss("ns3::NakagamiPropagationLossModel");
    }

  YansWifiPhyHelper phy;
  phy.SetChannel(channel.Create());
  phy.Set("ChannelSettings", StringValue("{172, 10, BAND_5GHZ, 0}"));
  phy.Set("TxPowerStart", DoubleValue(m_cfg.txPowerDbm));
  phy.Set("TxPowerEnd", DoubleValue(m_cfg.txPowerDbm));
  phy.Set("TxGain", DoubleValue(0.0));
  phy.Set("RxGain", DoubleValue(0.0));

  WifiMacHelper mac;
  mac.SetType("ns3::AdhocWifiMac");
  NetDeviceContainer devices = wifi.Install(phy, mac, m_pool);

  m_phyByNodeId.clear();
  m_phyByNodeId.reserve(devices.GetN());
  for (uint32_t i = 0; i < devices.GetN(); ++i)
    {
      Ptr<WifiNetDevice> wifiDevice =
          devices.Get(i)->GetObject<WifiNetDevice>();
      m_phyByNodeId[wifiDevice->GetNode()->GetId()] = wifiDevice->GetPhy();
    }

  InternetStackHelper internet;
  internet.Install(m_pool);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.0.0.0", "255.255.0.0");
  ipv4.Assign(devices);

  m_freeIdx.clear();
  m_freeIdx.reserve(m_pool.GetN());
  for (uint32_t i = 0; i < m_pool.GetN(); ++i)
    {
      m_freeIdx.push_back(i);
    }

  m_rxSockets.clear();
  m_rxSockets.reserve(m_pool.GetN());
  m_camApps.clear();
  m_camApps.reserve(m_pool.GetN());
  for (uint32_t i = 0; i < m_pool.GetN(); ++i)
    {
      Ptr<Node> node = m_pool.Get(i);
      Ptr<Socket> rx = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
      rx->SetAllowBroadcast(true);
      const int bindResult =
          rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_cfg.port));
      NS_ABORT_MSG_IF(bindResult != 0,
                      "Failed to bind RX socket on node " << node->GetId());
      rx->SetRecvCallback(MakeCallback(&SumoPerHelper::RxPacket, this));
      m_rxSockets.push_back(rx);

      Ptr<CamBroadcastApp> app = CreateObject<CamBroadcastApp>();
      app->Setup(m_cfg.port,
                 m_cfg.beaconSizeBytes,
                 m_cfg.beaconIntervalSec,
                 false);
      app->TraceConnectWithoutContext(
          "Tx", MakeCallback(&SumoPerHelper::TxTrace, this));
      node->AddApplication(app);
      app->SetStartTime(Seconds(m_cfg.trafficStartSec));
      app->SetStopTime(Seconds(m_cfg.simEndSec));
      m_camApps.push_back(app);
    }

  ns3::Config::ConnectFailSafe(
      "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/"
      "$ns3::WifiPhy/MonitorSnifferRx",
      MakeCallback(&SumoPerHelper::MonitorRxTrace, this));

  m_wifiInstalled = true;

  if (m_cfg.cbrSamplePeriodSec <= m_cfg.simEndSec)
    {
      Simulator::Schedule(Seconds(m_cfg.cbrSamplePeriodSec),
                          &SumoPerHelper::SampleCbr,
                          this);
    }
}

void
SumoPerHelper::SampleCbr()
{
  // Only assigned nodes contribute samples. This avoids state inheritance by
  // nodes that were idle while outside the SUMO vehicle set.
  for (const auto& mapping : m_vehToIdx)
    {
      const uint32_t nodeId = m_pool.Get(mapping.second)->GetId();
      const auto phyIt = m_phyByNodeId.find(nodeId);
      if (phyIt == m_phyByNodeId.end())
        {
          continue;
        }

      ++m_cbrTotalSamples[mapping.first];
      if (!phyIt->second->IsStateIdle())
        {
          ++m_cbrBusySamples[mapping.first];
        }
    }

  const double next =
      Simulator::Now().GetSeconds() + m_cfg.cbrSamplePeriodSec;
  if (next <= m_cfg.simEndSec)
    {
      Simulator::Schedule(Seconds(m_cfg.cbrSamplePeriodSec),
                          &SumoPerHelper::SampleCbr,
                          this);
    }
}

void
SumoPerHelper::TxTrace(uint32_t nodeId, uint32_t seq)
{
  const auto srcPool = std::find_if(
      m_idxToVeh.begin(),
      m_idxToVeh.end(),
      [this, nodeId](const auto& entry) {
        return m_pool.Get(entry.first)->GetId() == nodeId;
      });
  if (srcPool == m_idxToVeh.end())
    {
      NS_LOG_WARN("Ignoring TX from an unassigned pool node " << nodeId);
      return;
    }

  const std::string& srcVehicle = srcPool->second;
  const uint32_t srcIdx = srcPool->first;
  const double srcSpeed = m_speedById.count(srcVehicle)
                              ? m_speedById.at(srcVehicle)
                              : std::numeric_limits<double>::quiet_NaN();

  for (const auto& dst : m_idxToVeh)
    {
      if (dst.first == srcIdx)
        {
          continue;
        }

      const double distance = DistanceMeters(srcIdx, dst.first);
      if (!std::isfinite(distance) ||
          distance > m_cfg.maxRetainedRadiusM)
        {
          continue;
        }

      const uint32_t dstNodeId = m_pool.Get(dst.first)->GetId();
      Opportunity opportunity;
      opportunity.txTimeSec = Simulator::Now().GetSeconds();
      opportunity.srcVehicle = srcVehicle;
      opportunity.dstVehicle = dst.second;
      opportunity.srcNodeId = nodeId;
      opportunity.dstNodeId = dstNodeId;
      opportunity.sequence = seq;
      opportunity.distanceM = distance;
      opportunity.srcSpeedMps = srcSpeed;
      opportunity.dstSpeedMps = m_speedById.count(dst.second)
                                    ? m_speedById.at(dst.second)
                                    : std::numeric_limits<double>::quiet_NaN();

      const OpportunityKey key{nodeId, seq, dstNodeId};
      const auto inserted = m_opportunities.emplace(key, std::move(opportunity));
      NS_ABORT_MSG_IF(!inserted.second,
                      "Duplicate CAM opportunity for source=" << nodeId
                      << ", sequence=" << seq
                      << ", destination=" << dstNodeId);
    }
}

void
SumoPerHelper::RxPacket(Ptr<Socket> socket)
{
  Address from;
  Ptr<Packet> packet;
  while ((packet = socket->RecvFrom(from)))
    {
      CamHeader header;
      if (packet->PeekHeader(header) == 0)
        {
          NS_LOG_WARN("Packet without CamHeader on node "
                      << socket->GetNode()->GetId());
          continue;
        }

      packet->RemoveHeader(header);
      const uint32_t srcNodeId = header.GetSrcId();
      const uint32_t dstNodeId = socket->GetNode()->GetId();
      if (srcNodeId != dstNodeId)
        {
          const OpportunityKey key{srcNodeId, header.GetSeq(), dstNodeId};
          const auto opportunity = m_opportunities.find(key);
          if (opportunity != m_opportunities.end())
            {
              opportunity->second.received = true;
            }
        }
    }
}

void
SumoPerHelper::MonitorRxTrace(std::string context,
                              Ptr<const Packet> packet,
                              uint16_t /*channelFreqMhz*/,
                              WifiTxVector /*txVector*/,
                              MpduInfo /*mpdu*/,
                              SignalNoiseDbm signalNoise,
                              uint16_t /*staId*/)
{
  ++m_monRxCalls;
  const uint32_t rxNodeId = ExtractNodeIdFromContext(context);
  if (rxNodeId == std::numeric_limits<uint32_t>::max())
    {
      return;
    }

  CamTag tag;
  bool tagged = packet->FindFirstMatchingByteTag(tag);
  if (!tagged)
    {
      tagged = packet->PeekPacketTag(tag);
    }

  const double rssiDbm = signalNoise.signal;
  const double snrDb = signalNoise.signal - signalNoise.noise;
  m_lastRssiDbmByNode[rxNodeId] = rssiDbm;
  m_lastSnrDbByNode[rxNodeId] = snrDb;

  if (!tagged)
    {
      return;
    }

  ++m_monRxTagged;
  const uint32_t srcNodeId = tag.GetSrc();
  if (srcNodeId == rxNodeId)
    {
      return;
    }

  const OpportunityKey key{srcNodeId, tag.GetSeq(), rxNodeId};
  const auto opportunity = m_opportunities.find(key);
  if (opportunity != m_opportunities.end())
    {
      opportunity->second.rssiSumDbm += rssiDbm;
      opportunity->second.snrSumDb += snrDb;
      ++opportunity->second.phySamples;
    }
}

double
SumoPerHelper::DistanceMeters(uint32_t idxA, uint32_t idxB) const
{
  Ptr<MobilityModel> a = m_pool.Get(idxA)->GetObject<MobilityModel>();
  Ptr<MobilityModel> b = m_pool.Get(idxB)->GetObject<MobilityModel>();
  if (!a || !b)
    {
      return std::numeric_limits<double>::quiet_NaN();
    }

  const Vector pa = a->GetPosition();
  const Vector pb = b->GetPosition();
  return std::hypot(pa.x - pb.x, pa.y - pb.y);
}

void
SumoPerHelper::FlushWindow()
{
  const double windowEnd = Simulator::Now().GetSeconds();
  const double windowStart = windowEnd - m_cfg.windowSec;
  const bool retainWindow = windowStart >= m_cfg.warmupSec &&
                            windowEnd <= m_cfg.simEndSec;

  std::ofstream out;
  if (retainWindow)
    {
      EnsureCsvHeader(m_windowCsvPath);
      out.open(m_windowCsvPath, std::ios::app);
      NS_ABORT_MSG_IF(!out.is_open(),
                      "Cannot open output CSV: " << m_windowCsvPath);
    }

  m_cbrLastWindow.clear();
  for (const auto& mapping : m_cbrTotalSamples)
    {
      const std::string& vehicleId = mapping.first;
      const uint32_t total = mapping.second;
      const uint32_t busy = m_cbrBusySamples.count(vehicleId)
                                ? m_cbrBusySamples.at(vehicleId)
                                : 0u;
      m_cbrLastWindow[vehicleId] =
          total > 0 ? static_cast<double>(busy) / total : 0.0;
    }

  double cbrGlobal = 0.0;
  for (const auto& value : m_cbrLastWindow)
    {
      cbrGlobal += value.second;
    }
  if (!m_cbrLastWindow.empty())
    {
      cbrGlobal /= static_cast<double>(m_cbrLastWindow.size());
    }

  struct Aggregate
  {
    uint32_t opportunities{0};
    uint32_t received{0};
    double distanceSum{0.0};
    double srcSpeedSum{0.0};
    double dstSpeedSum{0.0};
    double rssiSum{0.0};
    double snrSum{0.0};
    uint32_t phySamples{0};
  };

  using VehicleLink = std::pair<std::string, std::string>;
  std::map<VehicleLink, Aggregate> aggregates;

  for (const auto& item : m_opportunities)
    {
      const Opportunity& opportunity = item.second;
      if (opportunity.txTimeSec < windowStart ||
          opportunity.txTimeSec >= windowEnd)
        {
          continue;
        }

      Aggregate& aggregate =
          aggregates[{opportunity.srcVehicle, opportunity.dstVehicle}];
      ++aggregate.opportunities;
      aggregate.received += opportunity.received ? 1u : 0u;
      aggregate.distanceSum += opportunity.distanceM;
      aggregate.srcSpeedSum += opportunity.srcSpeedMps;
      aggregate.dstSpeedSum += opportunity.dstSpeedMps;
      aggregate.rssiSum += opportunity.rssiSumDbm;
      aggregate.snrSum += opportunity.snrSumDb;
      aggregate.phySamples += opportunity.phySamples;
    }

  if (retainWindow)
    {
      for (const auto& item : aggregates)
        {
          const Aggregate& aggregate = item.second;
          const double count = static_cast<double>(aggregate.opportunities);
          const double per = 1.0 -
              static_cast<double>(aggregate.received) / count;
          const double rssi = aggregate.phySamples > 0
                                  ? aggregate.rssiSum / aggregate.phySamples
                                  : std::numeric_limits<double>::quiet_NaN();
          const double snr = aggregate.phySamples > 0
                                 ? aggregate.snrSum / aggregate.phySamples
                                 : std::numeric_limits<double>::quiet_NaN();
          const double cbrTx = m_cbrLastWindow.count(item.first.first)
                                   ? m_cbrLastWindow.at(item.first.first)
                                   : std::numeric_limits<double>::quiet_NaN();
          const double cbrRx = m_cbrLastWindow.count(item.first.second)
                                   ? m_cbrLastWindow.at(item.first.second)
                                   : std::numeric_limits<double>::quiet_NaN();
          const double meanDistance = aggregate.distanceSum / count;

          out << CsvField(m_cfg.runId) << ','
              << CsvField(m_cfg.scenarioId) << ',' << m_cfg.ns3Seed << ','
              << m_cfg.ns3Run << ',' << m_cfg.sumoSeed << ','
              << CsvField(m_cfg.propagation) << ',' << windowStart << ','
              << windowEnd << ',' << CsvField(item.first.first) << ','
              << CsvField(item.first.second) << ',' << aggregate.opportunities
              << ',' << aggregate.received << ',' << per << ','
              << meanDistance << ','
              << (meanDistance <= m_cfg.evaluationRadiusM ? 1 : 0) << ','
              << aggregate.srcSpeedSum / count << ','
              << aggregate.dstSpeedSum / count << ',' << rssi << ',' << snr
              << ',' << aggregate.phySamples << ',' << cbrTx << ',' << cbrRx
              << ',' << cbrGlobal << '\n';
        }
      NS_ABORT_MSG_IF(!out.good(),
                      "Failed while writing output CSV: "
                          << m_windowCsvPath);
    }

  // Remove only opportunities belonging to the completed window. Keeping
  // newer entries makes ordering at an exact window boundary deterministic.
  for (auto it = m_opportunities.begin(); it != m_opportunities.end();)
    {
      if (it->second.txTimeSec < windowEnd)
        {
          it = m_opportunities.erase(it);
        }
      else
        {
          ++it;
        }
    }

  m_lastRssiDbmByNode.clear();
  m_lastSnrDbByNode.clear();
  m_cbrBusySamples.clear();
  m_cbrTotalSamples.clear();

  const double next = Simulator::Now().GetSeconds() + m_cfg.windowSec;
  if (next <= m_cfg.simEndSec)
    {
      Simulator::Schedule(Seconds(m_cfg.windowSec),
                          &SumoPerHelper::FlushWindow,
                          this);
    }
}

void
SumoPerHelper::Run(const std::string& sumocfg, bool gui)
{
  ValidateConfig();
  Time::SetResolution(Time::NS);
  RngSeedManager::SetSeed(m_cfg.ns3Seed);
  RngSeedManager::SetRun(m_cfg.ns3Run);

  namespace fs = std::filesystem;
  const fs::path outputDir(m_cfg.outputDir);
  const fs::path outputFile = outputDir / m_cfg.outCsv;
  std::error_code error;
  fs::create_directories(outputDir, error);
  NS_ABORT_MSG_IF(error,
                  "Cannot create output directory " << outputDir.string()
                  << ": " << error.message());
  NS_ABORT_MSG_IF(m_cfg.failIfOutputExists && fs::exists(outputFile),
                  "Output file already exists: " << outputFile.string());
  NS_ABORT_MSG_IF(fs::exists(outputFile) && !fs::is_regular_file(outputFile),
                  "Output path is not a regular file: "
                      << outputFile.string());
  m_windowCsvPath = outputFile.string();

  m_sumo.Start_tapas(sumocfg,
                     gui,
                     m_cfg.stepSec,
                     m_cfg.sumoSeed,
                     m_cfg.sumoBeginSec,
                     m_cfg.sumoEndSec);
  NS_ABORT_MSG_IF(!m_sumo.IsEnabled(),
                  "This experiment requires a build with libsumo support");

  EnsureWifiAndApps();
  Simulator::ScheduleNow(&SumoPerHelper::StepOnce, this);
  if (m_cfg.windowSec <= m_cfg.simEndSec)
    {
      Simulator::Schedule(Seconds(m_cfg.windowSec),
                          &SumoPerHelper::FlushWindow,
                          this);
    }
  Simulator::Stop(Seconds(m_cfg.simEndSec) + NanoSeconds(1));
  Simulator::Run();
  Simulator::Destroy();
  m_sumo.Close();
}

void
SumoPerHelper::StepOnce()
{
  if (m_sumo.IsEnabled())
    {
      m_sumo.Step(m_cfg.stepSec);
      UpdateMobilityFromSumo(m_sumo.GetVehicles());
    }

  const double next = Simulator::Now().GetSeconds() + m_cfg.stepSec;
  if (next <= m_cfg.simEndSec)
    {
      Simulator::Schedule(Seconds(m_cfg.stepSec),
                          &SumoPerHelper::StepOnce,
                          this);
    }
}

void
SumoPerHelper::ValidateConfig() const
{
  NS_ABORT_MSG_IF(m_cfg.runId.empty(), "runId must not be empty");
  NS_ABORT_MSG_IF(m_cfg.scenarioId.empty(), "scenarioId must not be empty");
  NS_ABORT_MSG_IF(m_cfg.outputDir.empty(), "outputDir must not be empty");
  NS_ABORT_MSG_IF(m_cfg.outCsv.empty(), "outCsv must not be empty");
  const std::filesystem::path outCsvPath(m_cfg.outCsv);
  NS_ABORT_MSG_IF(outCsvPath.is_absolute() || outCsvPath.has_parent_path(),
                  "outCsv must be a filename only; put directories in "
                  "outputDir: " << m_cfg.outCsv);
  NS_ABORT_MSG_IF(m_cfg.ns3Seed == 0, "ns3Seed must be greater than zero");
  NS_ABORT_MSG_IF(m_cfg.ns3Run == 0, "ns3Run must be greater than zero");
  NS_ABORT_MSG_IF(m_cfg.warmupSec < 0.0, "warmupSec must not be negative");
  NS_ABORT_MSG_IF(m_cfg.stepSec <= 0.0, "stepSec must be positive");
  NS_ABORT_MSG_IF(m_cfg.windowSec <= 0.0, "windowSec must be positive");
  NS_ABORT_MSG_IF(m_cfg.simEndSec <= m_cfg.warmupSec,
                  "simEndSec must exceed warmupSec");
  NS_ABORT_MSG_IF(m_cfg.trafficStartSec < 0.0 ||
                      m_cfg.trafficStartSec >= m_cfg.simEndSec,
                  "trafficStartSec must be in [0, simEndSec)");
  NS_ABORT_MSG_IF(m_cfg.beaconIntervalSec <= 0.0,
                  "beaconIntervalSec must be positive");
  NS_ABORT_MSG_IF(m_cfg.beaconSizeBytes == 0,
                  "beaconSizeBytes must be greater than zero");
  NS_ABORT_MSG_IF(m_cfg.beaconSizeBytes < 8,
                  "beaconSizeBytes must include the 8-byte CamHeader");
  NS_ABORT_MSG_IF(m_cfg.maxVehicles == 0,
                  "maxVehicles must be greater than zero");
  NS_ABORT_MSG_IF(m_cfg.evaluationRadiusM <= 0.0,
                  "evaluationRadiusM must be positive");
  NS_ABORT_MSG_IF(m_cfg.maxRetainedRadiusM <= 0.0,
                  "maxRetainedRadiusM must be positive");
  NS_ABORT_MSG_IF(m_cfg.maxRetainedRadiusM < m_cfg.evaluationRadiusM,
                  "maxRetainedRadiusM must be at least evaluationRadiusM");
  NS_ABORT_MSG_IF(m_cfg.cbrSamplePeriodSec <= 0.0,
                  "cbrSamplePeriodSec must be positive");
  NS_ABORT_MSG_IF(m_cfg.sumoEndSec <= m_cfg.sumoBeginSec,
                  "sumoEndSec must exceed sumoBeginSec");
  NS_ABORT_MSG_IF(m_cfg.txPowerDbm < 0.0,
                  "txPowerDbm must not be negative");
  NS_ABORT_MSG_IF(m_cfg.pathLossExponent <= 0.0,
                  "pathLossExponent must be positive");
  NS_ABORT_MSG_IF(m_cfg.referenceDistanceM <= 0.0,
                  "referenceDistanceM must be positive");
  NS_ABORT_MSG_IF(m_cfg.port == 0, "port must be non-zero");
  NS_ABORT_MSG_IF(m_cfg.dataMode.empty(), "dataMode must not be empty");
  NS_ABORT_MSG_IF(!std::isfinite(m_cfg.referenceLossDb),
                  "referenceLossDb must be finite");
  NS_ABORT_MSG_IF(!std::isfinite(m_cfg.txPowerDbm),
                  "txPowerDbm must be finite");
  NS_ABORT_MSG_IF(m_cfg.propagation != "log-distance" &&
                      m_cfg.propagation != "log-distance-nakagami",
                  "Unsupported propagation configuration: "
                      << m_cfg.propagation);
}