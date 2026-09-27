#pragma once
#include <cstdint>
#include <vector>

namespace audio {
constexpr double kCalibrationProbeDurationMs=90.0;
constexpr double kCalibrationProbeGapMs=1000.0;
constexpr double kCalibrationMinConfidence=0.20;

enum class CalibrationProbeKind { Windows, Ipad };
// Different sweep directions identify which loudspeaker produced each probe.
std::vector<float> calibration_probe(uint32_t sample_rate,
  CalibrationProbeKind kind=CalibrationProbeKind::Windows);

struct ProbePairResult {
  bool valid=false;
  double first_ms=0;
  double second_ms=0;
  double delta_ms=0;
  double confidence=0;
  double windows_confidence=0;
  double ipad_confidence=0;
};

// Both probes must be found in one continuous microphone recording. Their
// difference cancels that microphone's fixed input latency and clock origin.
ProbePairResult detect_probe_pair(const std::vector<float>& recording,
                                  uint32_t sample_rate,
                                  double expected_gap_ms=kCalibrationProbeGapMs);
}
