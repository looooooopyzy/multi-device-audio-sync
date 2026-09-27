#include "audio/node_registry.h"
namespace audio {
const char* status_name(NodeStatus s) {
  switch(s) { case NodeStatus::Online:return "ONLINE"; case NodeStatus::Timeout:return "TIMEOUT";
    default:return "DISCONNECTED"; }
}
Node& NodeRegistry::seen(uint64_t id,const std::string& name,const std::string& ip,uint16_t port,
                          Platform platform,int64_t now_ns) {
  auto& n=nodes_[id]; n.id=id; n.name=name; n.ip=ip; n.port=port; n.platform=platform;
  n.status=NodeStatus::Online; n.last_seen_ns=now_ns; return n;
}
Node* NodeRegistry::find(uint64_t id) {
  auto it=nodes_.find(id); return it==nodes_.end()?nullptr:&it->second;
}
void NodeRegistry::expire(int64_t now_ns) {
  for(auto& kv:nodes_) if(now_ns-kv.second.last_seen_ns>5000000000LL)
    kv.second.status=NodeStatus::Timeout;
}
}
