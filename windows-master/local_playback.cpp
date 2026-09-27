#include "local_playback.h"
#include "audio/acoustic_calibration.h"
#include "audio/monotonic_clock.h"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace audio {
namespace {
void play_burst(int64_t presentation_master_ns,const std::vector<float>& burst,
                UINT output_device) {
  WAVEFORMATEX format{};
  format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=2;
  format.nSamplesPerSec=48000; format.wBitsPerSample=16;
  format.nBlockAlign=4; format.nAvgBytesPerSec=192000;
  HWAVEOUT device=nullptr;
  MMRESULT result=waveOutOpen(&device,output_device,&format,0,0,CALLBACK_NULL);
  if(result!=MMSYSERR_NOERROR) {
    return;
  }
  // Queue silence followed by the burst. The click position is calculated from
  // the same QPC timestamp sent to browsers; actual DAC latency needs calibration.
  int64_t now=monotonic_ns();
  int64_t lead_ns=std::max<int64_t>(0,presentation_master_ns-now);
  lead_ns=std::min<int64_t>(lead_ns,3000000000LL);
  size_t lead_frames=static_cast<size_t>(lead_ns*48/1000000);
  constexpr size_t tail_frames=2400;
  std::vector<int16_t> samples((lead_frames+burst.size()+tail_frames)*2,0);
  for(size_t i=0;i<burst.size();++i) {
    auto value=static_cast<int16_t>(std::clamp(burst[i],-1.0f,1.0f)*32767.0f);
    samples[(lead_frames+i)*2]=value;
    samples[(lead_frames+i)*2+1]=value;
  }
  WAVEHDR header{};
  header.lpData=reinterpret_cast<LPSTR>(samples.data());
  header.dwBufferLength=static_cast<DWORD>(samples.size()*sizeof(int16_t));
  if(waveOutPrepareHeader(device,&header,sizeof(header))==MMSYSERR_NOERROR) {
    if(waveOutWrite(device,&header,sizeof(header))==MMSYSERR_NOERROR) {
      int64_t deadline=presentation_master_ns+3000000000LL;
      while(!(header.dwFlags&WHDR_DONE) && monotonic_ns()<deadline) Sleep(2);
      if(!(header.dwFlags&WHDR_DONE)) waveOutReset(device);
    }
    waveOutUnprepareHeader(device,&header,sizeof(header));
  }
  waveOutClose(device);
}
void play_click(int64_t presentation_master_ns,UINT output_device) {
  constexpr double pi=3.14159265358979323846;
  std::vector<float> burst(480);
  for(size_t i=0;i<burst.size();++i) {
    double envelope=std::sin(pi*double(i)/double(burst.size()));
    burst[i]=static_cast<float>(0.42*std::sin(2*pi*1000*double(i)/48000)*envelope);
  }
  play_burst(presentation_master_ns,burst,output_device);
}
}
LocalPlaybackNode::LocalPlaybackNode(uint32_t output_device)
    :output_device_(output_device) {}
void LocalPlaybackNode::schedule_click(int64_t presentation_master_ns) {
  const UINT device=static_cast<UINT>(output_device_);
  workers_.emplace_back([presentation_master_ns,device]{ play_click(presentation_master_ns,device); });
}
void LocalPlaybackNode::schedule_probe(int64_t presentation_master_ns) {
  const UINT device=static_cast<UINT>(output_device_);
  workers_.emplace_back([presentation_master_ns,device]{
    play_burst(presentation_master_ns,calibration_probe(48000),device);
  });
}
LocalPlaybackNode::~LocalPlaybackNode() {
  for(auto& worker:workers_) if(worker.joinable()) worker.join();
}
struct LocalStreamPlayback::Impl {
  struct Buffer { std::vector<int16_t> pcm; WAVEHDR header{}; };
  HWAVEOUT device=nullptr;
  std::deque<std::unique_ptr<Buffer>> queued;
  uint64_t expected_frame=0,dropped=0;
  bool started=false;
  double delay_ms=0;
  explicit Impl(UINT output_device) {
    WAVEFORMATEX format{};
    format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=2;
    format.nSamplesPerSec=48000; format.wBitsPerSample=16;
    format.nBlockAlign=4; format.nAvgBytesPerSec=192000;
    MMRESULT result=waveOutOpen(&device,output_device,&format,0,0,CALLBACK_NULL);
    if(result!=MMSYSERR_NOERROR)
      throw std::runtime_error("waveOutOpen selected output failed: "+std::to_string(result));
  }
  void reap() {
    while(!queued.empty() && (queued.front()->header.dwFlags&WHDR_DONE)) {
      waveOutUnprepareHeader(device,&queued.front()->header,sizeof(WAVEHDR));
      queued.pop_front();
    }
  }
  void reset() {
    if(device) waveOutReset(device);
    while(!queued.empty()) {
      waveOutUnprepareHeader(device,&queued.front()->header,sizeof(WAVEHDR));
      queued.pop_front();
    }
    started=false;
  }
  ~Impl() {
    if(device) { reset(); waveOutClose(device); }
  }
};
LocalStreamPlayback::LocalStreamPlayback(uint32_t output_device)
    :impl_(std::make_unique<Impl>(static_cast<UINT>(output_device))) {}
LocalStreamPlayback::~LocalStreamPlayback()=default;
uint64_t LocalStreamPlayback::dropped() const { return impl_->dropped; }
void LocalStreamPlayback::set_delay_ms(double delay_ms) {
  if(!std::isfinite(delay_ms) || delay_ms<0 || delay_ms>500) return;
  if(std::abs(impl_->delay_ms-delay_ms)>0.01) {
    impl_->delay_ms=delay_ms;
    impl_->reset();
  }
}
void LocalStreamPlayback::submit(const AudioPacket& packet) {
  Impl& state=*impl_;
  if(!state.device || packet.samples.size()!=kAudioFramesPerPacket*kAudioChannels) return;
  state.reap();
  if(state.started && (packet.sample_frame!=state.expected_frame || (packet.flags&1)))
    state.reset();
  if(state.queued.size()>60) { state.reset(); ++state.dropped; }
  int64_t lead_ns=packet.presentation_master_ns+
    static_cast<int64_t>(std::llround(state.delay_ms*1000000))-monotonic_ns();
  if(!state.started && lead_ns<20000000) { ++state.dropped; return; }
  size_t lead_frames=state.started?0:static_cast<size_t>(
    std::min<int64_t>(lead_ns,500000000LL)*48/1000000);
  auto buffer=std::make_unique<Impl::Buffer>();
  buffer->pcm.assign(lead_frames*2+packet.samples.size(),0);
  for(size_t i=0;i<packet.samples.size();++i)
    buffer->pcm[lead_frames*2+i]=static_cast<int16_t>(
      std::clamp(packet.samples[i],-1.0f,1.0f)*32767.0f);
  buffer->header.lpData=reinterpret_cast<LPSTR>(buffer->pcm.data());
  buffer->header.dwBufferLength=static_cast<DWORD>(buffer->pcm.size()*sizeof(int16_t));
  if(waveOutPrepareHeader(state.device,&buffer->header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR) {
    ++state.dropped; return;
  }
  if(waveOutWrite(state.device,&buffer->header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR) {
    waveOutUnprepareHeader(state.device,&buffer->header,sizeof(WAVEHDR));
    ++state.dropped; return;
  }
  state.queued.push_back(std::move(buffer));
  state.started=true;
  state.expected_frame=packet.sample_frame+kAudioFramesPerPacket;
}
}
