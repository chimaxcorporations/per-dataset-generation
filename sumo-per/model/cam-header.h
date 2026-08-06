#pragma once

#include <cstdint>
#include <iosfwd>

#include "ns3/header.h"
#include "ns3/type-id.h"

namespace ns3 {

/**
 * Minimal CAM header used for broadcast PRR accounting.
 * Carries:
 *   - srcNodeId : ns-3 node ID of transmitter
 *   - seq       : sequence number
 */
class CamHeader : public Header
{
public:
  CamHeader();
  ~CamHeader() override = default;

  static TypeId GetTypeId();
  TypeId GetInstanceTypeId() const override;

  void SetSrcId(uint32_t id);
  uint32_t GetSrcId() const;

  void SetSeq(uint32_t seq);
  uint32_t GetSeq() const;

  // ns-3 Header API
  void Serialize(Buffer::Iterator start) const override;
  uint32_t Deserialize(Buffer::Iterator start) override;
  uint32_t GetSerializedSize() const override;
  void Print(std::ostream& os) const override;

private:
  uint32_t m_srcId{0};
  uint32_t m_seq{0};
};

} // namespace ns3
