#pragma once
#include "audio/acoustic_calibration.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace audio {
class MicrophoneCalibration {
public:
  ~MicrophoneCalibration();
  void start(uint32_t duration_ms=5500);
  void stop();
  bool ready() const { return state_==2; }
  bool finished() const { return state_>=3; }
  std::optional<ProbePairResult> result() const;
  std::string error() const;
private:
  void run(uint32_t duration_ms);
  std::atomic<int> state_{0}; // idle, starting, recording, finished, failed
  std::atomic<bool> stopping_{false};
  std::thread worker_;
  mutable std::mutex mutex_;
  std::optional<ProbePairResult> result_;
  std::string error_;
};
}
