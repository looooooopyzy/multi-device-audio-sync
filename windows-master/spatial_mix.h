#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace audio {
struct SpeakerPosition {
  uint64_t id;
  int x;
};
struct SpeakerMix {
  bool spatial=false;
  float left=1.0f;
  float right=1.0f;
};

// The outermost active speakers carry the stereo endpoints. Speakers between
// them share both channels; each channel is power-normalized across devices.
inline std::map<uint64_t,SpeakerMix> spatial_mixes(
    const std::vector<SpeakerPosition>& speakers) {
  std::map<uint64_t,SpeakerMix> result;
  if(speakers.empty()) return result;
  auto bounds=std::minmax_element(speakers.begin(),speakers.end(),
    [](const SpeakerPosition& a,const SpeakerPosition& b){ return a.x<b.x; });
  const int width=bounds.second->x-bounds.first->x;
  if(speakers.size()<2 || width<10) {
    for(const auto& speaker:speakers) result[speaker.id]={};
    return result;
  }
  double left_power=0,right_power=0;
  for(const auto& speaker:speakers) {
    const double t=double(speaker.x-bounds.first->x)/width;
    left_power+=(1-t)*(1-t);
    right_power+=t*t;
  }
  for(const auto& speaker:speakers) {
    const double t=double(speaker.x-bounds.first->x)/width;
    result[speaker.id]={true,static_cast<float>((1-t)/std::sqrt(left_power)),
                        static_cast<float>(t/std::sqrt(right_power))};
  }
  return result;
}
}
