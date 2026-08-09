#include "ns3/command-line.h"
#include "ns3/core-module.h"

#include "../helper/sumo-per-helper.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace ns3;

int
main(int argc, char* argv[])
{
  std::string sumocfg;
  bool gui{false};
  SumoPerHelper::Config cfg;

  CommandLine cmd(__FILE__);
  cmd.AddValue("sumocfg", "Path to the required SUMO .sumocfg file", sumocfg);
  cmd.AddValue("gui", "Run SUMO with its GUI", gui);

  cmd.AddValue("runId", "Unique experiment-run identifier", cfg.runId);
  cmd.AddValue("scenarioId", "Scenario or map identifier", cfg.scenarioId);
  cmd.AddValue("outputDir", "Run-specific output directory", cfg.outputDir);
  cmd.AddValue("outCsv", "CSV filename inside outputDir", cfg.outCsv);
  cmd.AddValue("failIfOutputExists",
               "Abort rather than overwrite an existing CSV",
               cfg.failIfOutputExists);

  cmd.AddValue("ns3Seed", "ns-3 RNG seed", cfg.ns3Seed);
  cmd.AddValue("ns3Run", "ns-3 RNG run number", cfg.ns3Run);
  cmd.AddValue("sumoSeed", "SUMO RNG seed", cfg.sumoSeed);

  cmd.AddValue("warmup", "Warm-up duration in ns-3 seconds", cfg.warmupSec);
  cmd.AddValue("simEnd", "Simulation end in ns-3 seconds", cfg.simEndSec);
  cmd.AddValue("step", "SUMO/ns-3 coupling step in seconds", cfg.stepSec);
  cmd.AddValue("window", "Aggregation-window duration in seconds", cfg.windowSec);
  cmd.AddValue("sumoBegin", "Absolute SUMO begin time in seconds", cfg.sumoBeginSec);
  cmd.AddValue("sumoEnd", "Absolute SUMO end time in seconds", cfg.sumoEndSec);

  cmd.AddValue("trafficStart",
               "CAM application start in ns-3 seconds",
               cfg.trafficStartSec);
  cmd.AddValue("beaconInterval", "CAM interval in seconds", cfg.beaconIntervalSec);
  cmd.AddValue("beaconSize",
               "Total CAM application-packet size in bytes",
               cfg.beaconSizeBytes);
  cmd.AddValue("maxVehicles", "Maximum simultaneous SUMO vehicles", cfg.maxVehicles);

  cmd.AddValue("evalRadius",
               "Primary receiver-evaluation radius in metres",
               cfg.evaluationRadiusM);
  cmd.AddValue("maxRetainedRadius",
               "Maximum retained link radius in metres",
               cfg.maxRetainedRadiusM);

  cmd.AddValue("port", "UDP destination port", cfg.port);
cmd.AddValue("propagation","Propagation model: log-distance, "
    "log-distance-nakagami, or urban",cfg.propagation);
  cmd.AddValue("pathLossExponent", "Log-distance path-loss exponent", cfg.pathLossExponent);
  cmd.AddValue("referenceDistance", "Reference distance in metres", cfg.referenceDistanceM);
  cmd.AddValue("referenceLoss", "Reference loss in dB", cfg.referenceLossDb);
  cmd.AddValue("txPower", "Transmit power in dBm", cfg.txPowerDbm);
  cmd.AddValue("dataMode", "ns-3 Wi-Fi data/control mode", cfg.dataMode);
  cmd.AddValue("cbrSamplePeriod", "CBR sampling period in seconds", cfg.cbrSamplePeriodSec);

  cmd.Parse(argc, argv);

  if (sumocfg.empty())
    {
      std::cerr << "Error: --sumocfg is required.\n";
      return 2;
    }

  const std::filesystem::path sumocfgPath(sumocfg);
  if (!std::filesystem::is_regular_file(sumocfgPath))
    {
      std::cerr << "Error: SUMO configuration does not exist or is not a file: "
                << sumocfg << '\n';
      return 2;
    }

  SumoPerHelper helper(cfg);
  helper.Run(sumocfgPath.string(), gui);

  const std::filesystem::path datasetPath =
      std::filesystem::path(cfg.outputDir) / cfg.outCsv;
  std::cout << "[sumo-per] Dataset: " << datasetPath.string() << '\n';
  return 0;
}