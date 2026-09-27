#include "audio/acoustic_calibration.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace audio {
namespace {
constexpr double kPi=3.14159265358979323846;
constexpr uint32_t kAnalysisRate=12000;
constexpr double kStartHz=900.0,kEndHz=3900.0;
void high_pass(std::vector<float>& samples) {
  const double rc=1.0/(2*kPi*700.0);
  const double alpha=rc/(rc+1.0/kAnalysisRate);
  for(int stage=0;stage<2;++stage) {
    double previous_input=0,previous_output=0;
    for(float& sample:samples) {
      const double input=sample;
      const double output=alpha*(previous_output+input-previous_input);
      sample=static_cast<float>(output);
      previous_input=input;
      previous_output=output;
    }
  }
}

float probe_at(double time_seconds,CalibrationProbeKind kind) {
  const double duration=kCalibrationProbeDurationMs/1000.0;
  if(time_seconds<0 || time_seconds>=duration) return 0;
  const double rise=std::sin(kPi*time_seconds/duration);
  const double start=kind==CalibrationProbeKind::Windows?kStartHz:kEndHz;
  const double end=kind==CalibrationProbeKind::Windows?kEndHz:kStartHz;
  const double phase=2*kPi*(start*time_seconds+
                           (end-start)*time_seconds*time_seconds/(2*duration));
  return static_cast<float>(0.45*rise*rise*std::sin(phase));
}

std::vector<float> at_analysis_rate(const std::vector<float>& input,uint32_t rate) {
  const size_t size=static_cast<size_t>(double(input.size())*kAnalysisRate/rate);
  std::vector<float> output(size);
  for(size_t i=0;i<size;++i) {
    const double position=double(i)*rate/kAnalysisRate;
    const size_t left=static_cast<size_t>(position);
    const size_t right=std::min(left+1,input.size()-1);
    const double fraction=position-left;
    output[i]=static_cast<float>(input[left]+(input[right]-input[left])*fraction);
  }
  return output;
}

struct Peak { size_t frame; double score; };
std::vector<Peak> select_peaks(std::vector<Peak>& candidates) {
  std::sort(candidates.begin(),candidates.end(),[](const Peak& a,const Peak& b){
    return a.score>b.score;
  });
  std::vector<Peak> peaks;
  for(const Peak& candidate:candidates) {
    const bool nearby=std::any_of(peaks.begin(),peaks.end(),[&](const Peak& selected){
      return selected.frame>candidate.frame?
        selected.frame-candidate.frame<kAnalysisRate/20:
        candidate.frame-selected.frame<kAnalysisRate/20;
    });
    if(!nearby) peaks.push_back(candidate);
    if(peaks.size()>=24) break;
  }
  return peaks;
}
}

std::vector<float> calibration_probe(uint32_t sample_rate,CalibrationProbeKind kind) {
  if(sample_rate<8000 || sample_rate>192000) return {};
  const size_t count=static_cast<size_t>(std::round(sample_rate*kCalibrationProbeDurationMs/1000.0));
  std::vector<float> result(count);
  for(size_t i=0;i<count;++i) result[i]=probe_at(double(i)/sample_rate,kind);
  return result;
}

ProbePairResult detect_probe_pair(const std::vector<float>& recording,uint32_t sample_rate,
                                  double expected_gap_ms) {
  ProbePairResult result;
  if(sample_rate<8000 || sample_rate>192000 || recording.empty() ||
     recording.size()>size_t(sample_rate)*12 ||
     !std::isfinite(expected_gap_ms) || expected_gap_ms<300 || expected_gap_ms>3000) return result;
  auto samples=at_analysis_rate(recording,sample_rate);
  auto windows_reference=calibration_probe(kAnalysisRate,CalibrationProbeKind::Windows);
  auto ipad_reference=calibration_probe(kAnalysisRate,CalibrationProbeKind::Ipad);
  if(samples.size()<windows_reference.size()+
     size_t(std::max(0.0,expected_gap_ms-500)*kAnalysisRate/1000)) return result;
  high_pass(samples);
  high_pass(windows_reference);
  high_pass(ipad_reference);
  double windows_energy=0,ipad_energy=0;
  for(float value:windows_reference) windows_energy+=double(value)*value;
  for(float value:ipad_reference) ipad_energy+=double(value)*value;
  std::vector<double> energy(samples.size()+1,0);
  for(size_t i=0;i<samples.size();++i)
    energy[i+1]=energy[i]+double(samples[i])*samples[i];
  std::vector<Peak> windows_candidates,ipad_candidates;
  for(size_t frame=0;frame+windows_reference.size()<=samples.size();frame+=2) {
    const double window_energy=energy[frame+windows_reference.size()]-energy[frame];
    if(window_energy<1e-8) continue;
    double windows_dot=0,ipad_dot=0;
    for(size_t j=0;j<windows_reference.size();++j) {
      windows_dot+=double(samples[frame+j])*windows_reference[j];
      ipad_dot+=double(samples[frame+j])*ipad_reference[j];
    }
    const double windows_score=std::min(1.0,std::abs(windows_dot)/
      std::sqrt(windows_energy*window_energy));
    const double ipad_score=std::min(1.0,std::abs(ipad_dot)/
      std::sqrt(ipad_energy*window_energy));
    result.windows_confidence=std::max(result.windows_confidence,windows_score);
    result.ipad_confidence=std::max(result.ipad_confidence,ipad_score);
    if(windows_score>=0.16) windows_candidates.push_back({frame,windows_score});
    if(ipad_score>=0.16) ipad_candidates.push_back({frame,ipad_score});
  }
  auto windows_peaks=select_peaks(windows_candidates);
  auto ipad_peaks=select_peaks(ipad_candidates);
  double best=-std::numeric_limits<double>::infinity();
  for(const Peak& first:windows_peaks) for(const Peak& second:ipad_peaks) {
    if(second.frame<=first.frame) continue;
    const double gap_ms=double(second.frame-first.frame)*1000/kAnalysisRate;
    const double delta_ms=gap_ms-expected_gap_ms;
    if(std::abs(delta_ms)>500) continue;
    const double quality=std::min(first.score,second.score)-
                         0.02*std::abs(delta_ms)/500;
    if(quality>best) {
      best=quality;
      result.first_ms=double(first.frame)*1000/kAnalysisRate;
      result.second_ms=double(second.frame)*1000/kAnalysisRate;
      result.delta_ms=delta_ms;
      result.confidence=std::min(first.score,second.score);
      result.valid=result.confidence>=kCalibrationMinConfidence;
    }
  }
  return result;
}
}
