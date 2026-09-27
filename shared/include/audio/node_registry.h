#pragma once
#include "audio/clock_sync.h"
#include "audio/protocol.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace audio {
enum class NodeStatus { Online, Timeout, Disconnected };
const char* status_name(NodeStatus s);
struct Node {
  uint64_t id=0;
  std::string name, ip;
  uint16_t port=0;
  Platform platform=Platform::Simulated;
  NodeStatus status=NodeStatus::Disconnected;
  int64_t last_seen_ns=0;
  ClockSyncEngine sync;
};
class NodeRegistry {
public:
  Node& seen(uint64_t id,const std::string& name,const std::string& ip,uint16_t port,
             Platform platform,int64_t now_ns);
  Node* find(uint64_t id);
  void expire(int64_t now_ns);
  const std::map<uint64_t,Node>& all() const { return nodes_; }
private:
  std::map<uint64_t,Node> nodes_;
};
}
