#pragma once
#include <string>
#include <vector>

namespace audio {
struct LanAddress {
  std::string ip;
  std::string adapter_kind;
  bool preferred=false;
};

// Active IPv4 addresses, with Wi-Fi/Ethernet routes first. No network request.
std::vector<LanAddress> lan_addresses();
}
