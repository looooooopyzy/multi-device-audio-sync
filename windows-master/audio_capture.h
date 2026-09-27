#pragma once
#include "audio/audio_packet.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace audio {
struct CapturedBlock {
  int64_t capture_master_ns=0;
  std::array<float,kAudioFramesPerPacket*kAudioChannels> samples{};
  bool discontinuity=false;
};
class IAudioCaptureSource {
public:
  virtual ~IAudioCaptureSource()=default;
  virtual void start()=0;
  virtual void stop()=0;
  virtual bool pop(CapturedBlock& block)=0;
  virtual std::string status() const=0;
};
std::unique_ptr<IAudioCaptureSource> make_tone_source();
std::unique_ptr<IAudioCaptureSource> make_system_loopback_source();
}
