#include "sumo-client.h"
#include <iostream>

#ifdef HAVE_LIBSUMO
  #include <libsumo/libsumo.h>
#endif

void
SumoClient::Start(const std::string& sumocfg, bool gui, int /*port*/)
{
  m_sumocfg = sumocfg;
  m_gui = gui;
  m_timeSec = 0.0;

#ifdef HAVE_LIBSUMO
  // libsumo uses the SUMO binaries internally; we pass SUMO-style arguments.
  // NOTE: adjust paths/args as needed (e.g., --step-length, --seed).
  std::vector<std::string> args;
  args.push_back(gui ? "sumo-gui" : "sumo");
  args.push_back("-c");
  args.push_back(sumocfg);
  // args.push_back("--step-length");
  // args.push_back("0.1");

    // less console noise
  args.push_back("--no-step-log");
  args.push_back("true");
  args.push_back("--duration-log.disable");
  args.push_back("true");

  libsumo::Simulation::start(args);
#else
  std::cerr << "[sumo-per] WARNING: Built without libsumo. Running in no-SUMO mode.\n";
  (void)sumocfg; (void)gui;
#endif
}

void
SumoClient::Start_tapas(const std::string& sumocfg,
                        bool gui,
                        double stepSec,
                        int32_t sumoSeed,
                        double beginSec,
                        double endSec)
{
  m_sumocfg = sumocfg;
  m_gui = gui;
  m_timeSec = beginSec;

#ifdef HAVE_LIBSUMO
  // libsumo uses the SUMO binaries internally; we pass SUMO-style arguments.
  // NOTE: adjust paths/args as needed (e.g., --step-length, --seed).
  std::vector<std::string> args;
  args.push_back(gui ? "sumo-gui" : "sumo");
  args.push_back("-c");
  args.push_back(sumocfg);

  // Randomizatin control
  args.push_back("--seed");
  args.push_back(std::to_string(sumoSeed));
  //  step length from cfg
  args.push_back("--step-length");
  args.push_back(std::to_string(stepSec));
  args.push_back("--begin");
  args.push_back(std::to_string(beginSec));
  args.push_back("--end");
  args.push_back(std::to_string(endSec));

  // recommended: less console noise
  args.push_back("--no-step-log");
  args.push_back("true");
  args.push_back("--duration-log.disable");
  args.push_back("true");

  libsumo::Simulation::start(args);
#else
  std::cerr << "[sumo-per] WARNING: Built without libsumo. Running in no-SUMO mode.\n";
  (void)sumocfg;
  (void)gui;
  (void)stepSec;
  (void)sumoSeed;
  (void)beginSec;
  (void)endSec;
#endif
}
void
SumoClient::Step(double stepSec)
{
  m_timeSec += stepSec;
#ifdef HAVE_LIBSUMO
  // SUMO advances to the given simulation time (in seconds)
  libsumo::Simulation::step(m_timeSec);
#else
  (void)stepSec;
#endif
}

std::vector<SumoVehicleState>
SumoClient::GetVehicles() const
{
  std::vector<SumoVehicleState> out;
#ifdef HAVE_LIBSUMO
  const auto ids = libsumo::Vehicle::getIDList();
  out.reserve(ids.size());
  for (const auto& id : ids)
    {
      SumoVehicleState s;
      s.id = id;
      const auto pos = libsumo::Vehicle::getPosition(id); // returns TraCIPosition {x,y,z}
      s.x = pos.x;
      s.y = pos.y;
      s.speed = libsumo::Vehicle::getSpeed(id);
      out.push_back(s);
    }
#endif
  return out;
}

void
SumoClient::Close()
{
#ifdef HAVE_LIBSUMO
  libsumo::Simulation::close();
#endif
}

bool
SumoClient::IsEnabled() const
{
#ifdef HAVE_LIBSUMO
  return true;
#else
  return false;
#endif
}
