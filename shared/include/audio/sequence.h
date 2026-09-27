#pragma once
#include <cstdint>
#include <map>
namespace audio {
struct PendingSync { uint64_t device_id; int64_t t1; uint32_t ipv4; uint16_t port; };
class SequenceTracker {
public:
  uint32_t next() { ++next_; if(!next_) ++next_; return next_; }
  void add(uint32_t seq,PendingSync pending) { pending_[seq]=pending; }
  bool consume(uint32_t seq,uint64_t id,int64_t t1,uint32_t ip,uint16_t port) {
    auto it=pending_.find(seq);
    if(it==pending_.end() || it->second.device_id!=id || it->second.t1!=t1 ||
       it->second.ipv4!=ip || it->second.port!=port) return false;
    pending_.erase(it); return true;
  }
  void expire(int64_t now_ns) {
    for(auto it=pending_.begin();it!=pending_.end();) {
      if(now_ns-it->second.t1>2000000000LL) it=pending_.erase(it); else ++it;
    }
  }
private:
  uint32_t next_=0;
  std::map<uint32_t,PendingSync> pending_;
};
}
