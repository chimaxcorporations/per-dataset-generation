#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SumoVehicleState
{
  std::string id;
  double x{0.0};
  double y{0.0};
  double speed{0.0}; // m/s
};

class SumoClient
{
public:
  /**
   * Start SUMO via libsumo with deterministic timing and randomization.
   */
  void Start(const std::string& sumocfg, bool gui, int port = 0);
  void Start_tapas(const std::string& sumocfg,
                   bool gui,
                   double stepSec,
                   int32_t sumoSeed,
                   double beginSec,
                   double endSec);

  /** Advance SUMO time by stepSec seconds. */
  void Step(double stepSec);

  /** Get current list of vehicle states. */
  std::vector<SumoVehicleState> GetVehicles() const;

  /** Close SUMO. */
  void Close();

  /** True if compiled with libsumo support. */
  bool IsEnabled() const;

private:
  std::string m_sumocfg;
  bool m_gui{false};
  double m_timeSec{0.0};
};
