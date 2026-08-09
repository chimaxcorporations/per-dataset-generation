#include "cam-header.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("CamHeader");
NS_OBJECT_ENSURE_REGISTERED(CamHeader);

CamHeader::CamHeader() {}

TypeId
CamHeader::GetTypeId()
{
  static TypeId tid = TypeId("ns3::CamHeader")
    .SetParent<Header>()
    .SetGroupName("SumoPer")
    .AddConstructor<CamHeader>();
  return tid;
}

TypeId
CamHeader::GetInstanceTypeId() const
{
  return GetTypeId();
}

void
CamHeader::SetSrcId(uint32_t id)
{
  m_srcId = id;
}

uint32_t
CamHeader::GetSrcId() const
{
  return m_srcId;
}

void
CamHeader::SetSeq(uint32_t seq)
{
  m_seq = seq;
}

uint32_t
CamHeader::GetSeq() const
{
  return m_seq;
}

uint32_t
CamHeader::GetSerializedSize() const
{
  // two uint32_t fields
  return 8;
}

void
CamHeader::Serialize(Buffer::Iterator start) const
{
  start.WriteHtonU32(m_srcId);
  start.WriteHtonU32(m_seq);
}

uint32_t
CamHeader::Deserialize(Buffer::Iterator start)
{
  m_srcId = start.ReadNtohU32();
  m_seq   = start.ReadNtohU32();
  return GetSerializedSize();
}

void
CamHeader::Print(std::ostream& os) const
{
  os << "CAM(src=" << m_srcId
     << ", seq=" << m_seq << ")";
}

} // namespace ns3

