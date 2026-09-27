#pragma once
#include "audio/audio_packet.h"
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace audio {
// Phase A click renderer. Every burst is queued against the master QPC timeline.
// Device output latency remains an acoustic-calibration input.
class LocalPlaybackNode {
public:
  explicit LocalPlaybackNode(uint32_t output_device=0xffffffffu);
  void schedule_click(int64_t presentation_master_ns);
  void schedule_probe(int64_t presentation_master_ns);
  ~LocalPlaybackNode();
private:
  uint32_t output_device_;
  std::vector<std::thread> workers_;
};
class LocalStreamPlayback {
public:
  explicit LocalStreamPlayback(uint32_t output_device=0xffffffffu);
  ~LocalStreamPlayback();
  LocalStreamPlayback(const LocalStreamPlayback&)=delete;
  LocalStreamPlayback& operator=(const LocalStreamPlayback&)=delete;
  void submit(const AudioPacket& packet);
  void set_delay_ms(double delay_ms);
  uint64_t dropped() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
