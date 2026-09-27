#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
namespace audio {
struct ClockSample {
  int64_t t1=0,t2=0,t3=0,t4=0;
  double rtt_ns=0, offset_ns=0, master_mid_ns=0;
  static bool calculate(int64_t t1,int64_t t2,int64_t t3,int64_t t4,ClockSample& out);
};
enum class SyncQuality { Unstable, Poor, Fair, Good, Excellent };
const char* quality_name(SyncQuality q);
struct ClockModel {
  bool valid=false;
  double rate=1, offset_at_reference_ns=0, reference_master_ns=0;
  double rtt_ns=0, filtered_rtt_ns=0, jitter_ns=0, drift_ppm=0;
  size_t sample_count=0, filtered_count=0;
  SyncQuality quality=SyncQuality::Unstable;
  double offset_at(double master_ns) const {
    return offset_at_reference_ns+(rate-1)*(master_ns-reference_master_ns);
  }
};
class ClockSyncEngine {
public:
  bool add(const ClockSample& sample);
  ClockModel model() const;
  size_t size() const { return samples_.size(); }
private:
  std::deque<ClockSample> samples_;
};
}
