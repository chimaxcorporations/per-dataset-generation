#pragma once
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ns3 {
class Node;
template <typename T> class Ptr;
}

struct PerRecord
{
  double t{0.0};
  std::string src;
  std::string dst;
  uint32_t tx{0};
  uint32_t rxOk{0};
  double per{0.0};

  double dist_m{0.0};
  double v_src{0.0};
  double v_dst{0.0};

  double rssi_dbm{0.0}; // optional (extend hooks)
  double snr_db{0.0};   // optional (extend hooks)
  double cbr{0.0};      // optional (extend hooks)
};

class PerCollector
{
public:
  using LinkKey = std::pair<std::string, std::string>;

  void CountTx(const std::string& src, const std::string& dst);
  void CountRxOk(const std::string& src, const std::string& dst);

  /**
   * Flush window counters to CSV.
   * Provide dist/speed maps computed by the helper at the end of the window.
   */
  void FlushToCsv(double tNow,
                  const std::map<std::string, double>& speedById,
                  const std::map<LinkKey, double>& distByLink,
                  const std::string& outCsv);

  void Reset();

private:
  std::map<LinkKey, uint32_t> m_tx;
  std::map<LinkKey, uint32_t> m_rxOk;
};
