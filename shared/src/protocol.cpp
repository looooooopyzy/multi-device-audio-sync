#include "audio/protocol.h"
#include <algorithm>

namespace audio {
namespace {
void put(uint8_t* b, uint64_t value, size_t size) {
  for (size_t i=0;i<size;++i) b[i]=static_cast<uint8_t>(value >> (8*(size-1-i)));
}
uint64_t get(const uint8_t* b, size_t size) {
  uint64_t value=0;
  for (size_t i=0;i<size;++i) value=(value<<8)|b[i];
  return value;
}
bool valid_type(uint16_t t) { return t>=1 && t<=5; }
bool valid_platform(uint8_t p) { return p>=1 && p<=5; }
bool valid_utf8(const std::string& s) {
  // Reject embedded NUL and malformed UTF-8; all payloads remain printable names.
  for (size_t i=0;i<s.size();) {
    unsigned char c=static_cast<unsigned char>(s[i]);
    if (c==0) return false;
    size_t n=c<0x80?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
    if (!n || i+n>s.size()) return false;
    for(size_t j=1;j<n;++j) if((static_cast<unsigned char>(s[i+j])&0xc0)!=0x80) return false;
    i+=n;
  }
  return true;
}
}
bool encode(const Packet& p, std::array<uint8_t,kPacketSize>& b) {
  if (!valid_type(static_cast<uint16_t>(p.type)) || !valid_platform(static_cast<uint8_t>(p.platform)) ||
      p.deviceName.size()>kNameCapacity || !valid_utf8(p.deviceName)) return false;
  b.fill(0);
  put(b.data(),kMagic,4); put(b.data()+4,kVersion,2);
  put(b.data()+6,static_cast<uint16_t>(p.type),2); put(b.data()+8,p.sequence,4);
  put(b.data()+12,p.deviceId,8); put(b.data()+20,static_cast<uint64_t>(p.t1),8);
  put(b.data()+28,static_cast<uint64_t>(p.t2),8); put(b.data()+36,static_cast<uint64_t>(p.t3),8);
  b[44]=static_cast<uint8_t>(p.platform); b[45]=static_cast<uint8_t>(p.deviceName.size());
  std::copy(p.deviceName.begin(),p.deviceName.end(),b.begin()+48);
  return true;
}
bool decode(const uint8_t* b, size_t n, Packet& p) {
  if (!b || n!=kPacketSize || get(b,4)!=kMagic || get(b+4,2)!=kVersion ||
      !valid_type(static_cast<uint16_t>(get(b+6,2))) || !valid_platform(b[44]) ||
      b[45]>kNameCapacity || b[46]!=0 || b[47]!=0) return false;
  for(size_t i=48+b[45];i<kPacketSize;++i) if(b[i]!=0) return false;
  Packet result;
  result.type=static_cast<MessageType>(get(b+6,2)); result.sequence=static_cast<uint32_t>(get(b+8,4));
  result.deviceId=get(b+12,8); result.t1=static_cast<int64_t>(get(b+20,8));
  result.t2=static_cast<int64_t>(get(b+28,8)); result.t3=static_cast<int64_t>(get(b+36,8));
  result.platform=static_cast<Platform>(b[44]); result.deviceName.assign(reinterpret_cast<const char*>(b+48),b[45]);
  if (!valid_utf8(result.deviceName)) return false;
  p=std::move(result); return true;
}
const char* platform_name(Platform p) {
  switch(p) { case Platform::Windows:return "WINDOWS"; case Platform::Android:return "ANDROID";
    case Platform::IOS:return "IOS"; case Platform::IPadOS:return "IPADOS";
    case Platform::Simulated:return "SIMULATED"; }
  return "UNKNOWN";
}
}
