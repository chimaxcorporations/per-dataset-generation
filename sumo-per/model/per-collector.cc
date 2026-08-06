#include "per-collector.h"
#include <iostream>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace
{
std::string
CsvEscape(const std::string& value)
{
  if (value.find_first_of(",\"\n\r") == std::string::npos)
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
}

void
PerCollector::CountTx(const std::string& src, const std::string& dst)
{
  m_tx[{src, dst}]++;
}

void
PerCollector::CountRxOk(const std::string& src, const std::string& dst)
{
  m_rxOk[{src, dst}]++;
}

void
PerCollector::FlushToCsv(double tNow,
                         const std::map<std::string, double>& speedById,
                         const std::map<LinkKey, double>& distByLink,
                         const std::string& outCsv)
{
  std::ofstream f;
  const bool needsHeader = !std::filesystem::exists(outCsv) ||
                           std::filesystem::file_size(outCsv) == 0;
  f.open(outCsv, std::ios::out | std::ios::app);
  if (!f.is_open())
    {
      std::cerr << "[sumo-per] ERROR: cannot open output file: " << outCsv << "\n";
      return;
    }

  if (needsHeader)
    {
      f << "t,src,dst,tx,rxOk,per,dist_m,v_src,v_dst,rssi_dbm,snr_db,cbr\n";
    }

  // iterate all observed links (union of tx and rxOk maps)
  std::map<LinkKey, std::pair<uint32_t,uint32_t>> unionMap;
  for (const auto& kv : m_tx) unionMap[kv.first].first = kv.second;
  for (const auto& kv : m_rxOk) unionMap[kv.first].second = kv.second;

  for (const auto& kv : unionMap)
    {
      const auto& link = kv.first;
      const uint32_t tx = kv.second.first;
      const uint32_t rx = kv.second.second;
      if (rx > tx)
        {
          throw std::runtime_error("PER accounting error: rx exceeds tx");
        }
      const double per = tx == 0
                             ? std::numeric_limits<double>::quiet_NaN()
                             : 1.0 - (static_cast<double>(rx) / tx);

      double dist = 0.0;
      auto itD = distByLink.find(link);
      if (itD != distByLink.end()) dist = itD->second;

      double vsrc = 0.0, vdst = 0.0;
      auto itVs = speedById.find(link.first);
      if (itVs != speedById.end()) vsrc = itVs->second;
      auto itVd = speedById.find(link.second);
      if (itVd != speedById.end()) vdst = itVd->second;

      // These measurements are unavailable through this legacy collector.
      const double rssi = std::numeric_limits<double>::quiet_NaN();
      const double snr = std::numeric_limits<double>::quiet_NaN();
      const double cbr = std::numeric_limits<double>::quiet_NaN();

      f << tNow << ","
        << CsvEscape(link.first) << ","
        << CsvEscape(link.second) << ","
        << tx << ","
        << rx << ","
        << per << ","
        << dist << ","
        << vsrc << ","
        << vdst << ","
        << rssi << ","
        << snr << ","
        << cbr << "\n";
    }
  f.close();

  Reset();
}

void
PerCollector::Reset()
{
  m_tx.clear();
  m_rxOk.clear();
}
