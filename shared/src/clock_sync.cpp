#include "audio/clock_sync.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace audio {
bool ClockSample::calculate(int64_t a,int64_t b,int64_t c,int64_t d,ClockSample& out) {
  if (a<0 || b<0 || c<b || d<a) return false;
  const double elapsed=static_cast<double>(d)-a, processing=static_cast<double>(c)-b;
  const double rtt=elapsed-processing;
  if (rtt<0 || rtt>1000000000.0 || processing>1000000000.0) return false;
  out={a,b,c,d,rtt,((static_cast<double>(b)-a)+(static_cast<double>(c)-d))/2,
       (static_cast<double>(a)+d)/2};
  return true;
}
const char* quality_name(SyncQuality q) {
  switch(q) { case SyncQuality::Excellent:return "EXCELLENT"; case SyncQuality::Good:return "GOOD";
    case SyncQuality::Fair:return "FAIR"; case SyncQuality::Poor:return "POOR";
    default:return "UNSTABLE"; }
}
bool ClockSyncEngine::add(const ClockSample& s) {
  if (!std::isfinite(s.rtt_ns) || !std::isfinite(s.offset_ns) || s.rtt_ns<0 || s.rtt_ns>1e9) return false;
  samples_.push_back(s);
  if(samples_.size()>256) samples_.pop_front();
  return true;
}
ClockModel ClockSyncEngine::model() const {
  ClockModel m; m.sample_count=samples_.size();
  if(samples_.empty()) return m;
  m.rtt_ns=samples_.back().rtt_ns;
  double min_rtt=std::numeric_limits<double>::infinity();
  for(const auto& s:samples_) min_rtt=std::min(min_rtt,s.rtt_ns);
  std::vector<const ClockSample*> selected;
  for(const auto& s:samples_) if(s.rtt_ns<=min_rtt+2000000.0) selected.push_back(&s);
  if(selected.empty()) return m;
  m.filtered_count=selected.size();
  double sw=0,sx=0,sy=0;
  for(const auto* s:selected) {
    double w=1.0/(1.0+s->rtt_ns/1000000.0);
    sw+=w; sx+=w*s->master_mid_ns; sy+=w*s->offset_ns;
    m.filtered_rtt_ns+=w*s->rtt_ns;
  }
  m.filtered_rtt_ns/=sw;
  const double x0=sx/sw, y0=sy/sw;
  double numerator=0,denominator=0;
  for(const auto* s:selected) {
    double w=1.0/(1.0+s->rtt_ns/1000000.0), dx=s->master_mid_ns-x0;
    numerator+=w*dx*(s->offset_ns-y0); denominator+=w*dx*dx;
  }
  double slope=denominator>0?numerator/denominator:0;
  // A real oscillator cannot plausibly move by more than 1000 ppm here.
  if(std::abs(slope)>0.001) slope=0;
  m.valid=true; m.rate=1+slope; m.drift_ppm=slope*1000000.0;
  m.reference_master_ns=x0; m.offset_at_reference_ns=y0;
  double variance=0;
  for(const auto* s:selected) {
    double residual=s->offset_ns-(y0+slope*(s->master_mid_ns-x0));
    variance+=residual*residual;
  }
  m.jitter_ns=std::sqrt(variance/selected.size());
  double span=(selected.back()->master_mid_ns-selected.front()->master_mid_ns)/1e9;
  if(selected.size()<8 || span<5) m.quality=SyncQuality::Unstable;
  else if(m.filtered_rtt_ns>20e6 || m.jitter_ns>5e6) m.quality=SyncQuality::Poor;
  else if(m.filtered_rtt_ns>10e6 || m.jitter_ns>2e6) m.quality=SyncQuality::Fair;
  else if(m.filtered_rtt_ns>4e6 || m.jitter_ns>0.7e6) m.quality=SyncQuality::Good;
  else m.quality=SyncQuality::Excellent;
  return m;
}
}
