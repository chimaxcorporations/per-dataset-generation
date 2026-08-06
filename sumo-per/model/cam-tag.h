#pragma once

#include <cstdint>
#include <iosfwd>

#include "ns3/tag.h"
#include "ns3/tag-buffer.h"
#include "ns3/type-id.h"

namespace ns3 {

class CamTag : public Tag
{
public:
  CamTag() : m_src(0), m_seq(0) {}
  CamTag(uint32_t src, uint32_t seq) : m_src(src), m_seq(seq) {}

  static TypeId GetTypeId()
  {
    static TypeId tid = TypeId("ns3::CamTag")
      .SetParent<Tag>()
      .SetGroupName("SumoPer")
      .AddConstructor<CamTag>();
    return tid;
  }

  TypeId GetInstanceTypeId() const override { return GetTypeId(); }

  uint32_t GetSerializedSize() const override { return 8; }
  void Serialize(TagBuffer i) const override { i.WriteU32(m_src); i.WriteU32(m_seq); }
  void Deserialize(TagBuffer i) override { m_src = i.ReadU32(); m_seq = i.ReadU32(); }

  void Print(std::ostream& os) const override { os << "src=" << m_src << " seq=" << m_seq; }

  void Set(uint32_t src, uint32_t seq) { m_src = src; m_seq = seq; }
  uint32_t GetSrc() const { return m_src; }
  uint32_t GetSeq() const { return m_seq; }

private:
  uint32_t m_src;
  uint32_t m_seq;
};

} // namespace ns3
