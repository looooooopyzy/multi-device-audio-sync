#include "network_addresses.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

namespace audio {
std::vector<LanAddress> lan_addresses() {
  ULONG size=15000;
  std::vector<unsigned char> storage(size);
  ULONG result=ERROR_BUFFER_OVERFLOW;
  constexpr ULONG flags=GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|
                        GAA_FLAG_SKIP_DNS_SERVER|GAA_FLAG_INCLUDE_GATEWAYS;
  for(int attempt=0;attempt<3 && result==ERROR_BUFFER_OVERFLOW;++attempt) {
    result=GetAdaptersAddresses(AF_INET,flags,nullptr,
      reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data()),&size);
    if(result==ERROR_BUFFER_OVERFLOW) storage.resize(size);
  }
  if(result!=NO_ERROR) return {};

  struct Ranked { LanAddress address; int rank; };
  std::vector<Ranked> found;
  std::set<std::string> seen;
  for(auto* adapter=reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
      adapter;adapter=adapter->Next) {
    if(adapter->OperStatus!=IfOperStatusUp || adapter->IfType==IF_TYPE_SOFTWARE_LOOPBACK ||
       adapter->IfType==IF_TYPE_TUNNEL) continue;
    const bool wifi=adapter->IfType==IF_TYPE_IEEE80211;
    const bool ethernet=adapter->IfType==IF_TYPE_ETHERNET_CSMACD;
    const bool gateway=adapter->FirstGatewayAddress!=nullptr;
    const int rank=wifi&&gateway?0:ethernet&&gateway?1:wifi?2:ethernet?3:4;
    const std::string kind=wifi?"Wi-Fi":ethernet?"Ethernet":"Other adapter";
    for(auto* address=adapter->FirstUnicastAddress;address;address=address->Next) {
      if(!address->Address.lpSockaddr || address->Address.lpSockaddr->sa_family!=AF_INET)
        continue;
      const auto* ipv4=reinterpret_cast<const sockaddr_in*>(address->Address.lpSockaddr);
      uint32_t host=ntohl(ipv4->sin_addr.s_addr);
      uint8_t first=static_cast<uint8_t>(host>>24),second=static_cast<uint8_t>(host>>16);
      if(!first || first==127 || first>=224 || (first==169 && second==254)) continue;
      char text[INET_ADDRSTRLEN]{};
      if(!inet_ntop(AF_INET,&ipv4->sin_addr,text,sizeof(text)) || !seen.insert(text).second)
        continue;
      found.push_back({{text,kind,rank<=1},rank});
    }
  }
  std::stable_sort(found.begin(),found.end(),[](const Ranked& a,const Ranked& b){
    if(a.rank!=b.rank) return a.rank<b.rank;
    return a.address.ip<b.address.ip;
  });
  std::vector<LanAddress> addresses;
  for(auto& entry:found) addresses.push_back(std::move(entry.address));
  return addresses;
}
}
