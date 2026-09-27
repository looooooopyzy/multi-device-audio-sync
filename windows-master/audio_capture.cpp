#include "audio_capture.h"
#include "audio/monotonic_clock.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace audio {
namespace {
template<class T> struct ComHandle {
  T* value=nullptr;
  ~ComHandle(){ if(value) value->Release(); }
  T** put(){ return &value; }
  T* operator->() const { return value; }
};
void require(HRESULT result,const char* operation) {
  if(FAILED(result)) throw std::runtime_error(std::string(operation)+" failed HRESULT="+
                                           std::to_string(static_cast<unsigned long>(result)));
}
enum class SampleKind { Float32, Pcm16, Pcm24, Pcm32 };
const GUID kPcmSubtype={1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
const GUID kFloatSubtype={3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
SampleKind sample_kind(const WAVEFORMATEX& format) {
  WORD tag=format.wFormatTag;
  if(tag==WAVE_FORMAT_EXTENSIBLE && format.cbSize>=22) {
    const auto& ext=reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
    if(IsEqualGUID(ext.SubFormat,kFloatSubtype)) tag=WAVE_FORMAT_IEEE_FLOAT;
    else if(IsEqualGUID(ext.SubFormat,kPcmSubtype)) tag=WAVE_FORMAT_PCM;
  }
  if(tag==WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample==32) return SampleKind::Float32;
  if(tag==WAVE_FORMAT_PCM) {
    if(format.wBitsPerSample==16) return SampleKind::Pcm16;
    if(format.wBitsPerSample==24) return SampleKind::Pcm24;
    if(format.wBitsPerSample==32) return SampleKind::Pcm32;
  }
  throw std::runtime_error("unsupported WASAPI mix format");
}
float sample_at(const BYTE* data,size_t index,SampleKind kind) {
  if(kind==SampleKind::Float32) {
    float result; std::memcpy(&result,data+index*4,4);
    return std::isfinite(result)?std::clamp(result,-1.0f,1.0f):0.0f;
  }
  if(kind==SampleKind::Pcm16) {
    int16_t value; std::memcpy(&value,data+index*2,2); return value/32768.0f;
  }
  if(kind==SampleKind::Pcm24) {
    const BYTE* at=data+index*3;
    int32_t value=int32_t(at[0])|(int32_t(at[1])<<8)|(int32_t(at[2])<<16);
    if(value&0x800000) value|=~0xffffff;
    return value/8388608.0f;
  }
  int32_t value; std::memcpy(&value,data+index*4,4); return value/2147483648.0f;
}

class WorkerSource final:public IAudioCaptureSource {
public:
  explicit WorkerSource(bool tone):tone_(tone) {}
  ~WorkerSource() override { stop(); }
  void start() override {
    if(worker_.joinable()) return;
    stopping_=false;
    worker_=std::thread([this]{
      try { if(tone_) run_tone(); else run_system(); }
      catch(const std::exception& e) { set_status(std::string("ERROR: ")+e.what()); }
    });
  }
  void stop() override {
    stopping_=true;
    if(worker_.joinable()) worker_.join();
  }
  bool pop(CapturedBlock& block) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if(queue_.empty()) return false;
    block=std::move(queue_.front()); queue_.pop_front(); return true;
  }
  std::string status() const override {
    std::lock_guard<std::mutex> lock(mutex_); return status_;
  }
private:
  void set_status(std::string status) {
    std::lock_guard<std::mutex> lock(mutex_); status_=std::move(status);
  }
  void push(CapturedBlock block) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(queue_.size()>=50) { queue_.pop_front(); block.discontinuity=true; }
    queue_.push_back(std::move(block));
  }
  void run_tone() {
    set_status("RUNNING tone 48 kHz stereo");
    uint64_t frame=0;
    int64_t origin_ns=monotonic_ns();
    while(!stopping_) {
      int64_t target_ns=origin_ns+static_cast<int64_t>(frame*1000000000ULL/kAudioSampleRate);
      int64_t now_ns=monotonic_ns();
      if(now_ns>target_ns+50000000LL) {
        origin_ns=now_ns-static_cast<int64_t>(frame*1000000000ULL/kAudioSampleRate);
        target_ns=now_ns;
      }
      while(!stopping_ && (now_ns=monotonic_ns())<target_ns) {
        if(target_ns-now_ns>2000000LL) Sleep(1);
        else YieldProcessor();
      }
      CapturedBlock block;
      block.capture_master_ns=target_ns;
      for(size_t i=0;i<kAudioFramesPerPacket;++i) {
        float value=0.12f*std::sin(2*3.141592653589793*440*(frame+i)/48000.0);
        block.samples[2*i]=value; block.samples[2*i+1]=value;
      }
      frame+=kAudioFramesPerPacket; push(std::move(block));
    }
  }
  void run_system() {
    require(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"CoInitializeEx");
    struct CoScope { ~CoScope(){ CoUninitialize(); } } co_scope;
    ComHandle<IMMDeviceEnumerator> enumerator;
    require(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                             __uuidof(IMMDeviceEnumerator),
                             reinterpret_cast<void**>(enumerator.put())),"MMDeviceEnumerator");
    ComHandle<IMMDevice> device;
    require(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,device.put()),"default multimedia render endpoint");
    ComHandle<IAudioClient> client;
    require(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,
                             reinterpret_cast<void**>(client.put())),"IAudioClient activate");
    WAVEFORMATEX* raw_format=nullptr;
    require(client->GetMixFormat(&raw_format),"GetMixFormat");
    struct FormatScope { WAVEFORMATEX* ptr; ~FormatScope(){ CoTaskMemFree(ptr); } } format_scope{raw_format};
    const WAVEFORMATEX& format=*raw_format;
    if(!format.nSamplesPerSec || !format.nChannels || format.nChannels>8 ||
       format.nBlockAlign!=format.nChannels*(format.wBitsPerSample/8))
      throw std::runtime_error("invalid WASAPI mix format");
    SampleKind kind=sample_kind(format);
    require(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,
                               10000000,0,raw_format,nullptr),"loopback Initialize");
    ComHandle<IAudioCaptureClient> capture;
    require(client->GetService(__uuidof(IAudioCaptureClient),
                               reinterpret_cast<void**>(capture.put())),"IAudioCaptureClient");
    require(client->Start(),"loopback Start");
    struct StopScope { IAudioClient* client; ~StopScope(){ client->Stop(); } } stop_scope{client.value};
    set_status("RUNNING system loopback "+std::to_string(format.nSamplesPerSec)+" Hz / "+
               std::to_string(format.nChannels)+" channels");
    std::vector<float> pending,output;
    double position=0;
    int64_t pending_first_ns=0,output_first_ns=0;
    bool discontinuity=false;
    const double step=double(format.nSamplesPerSec)/kAudioSampleRate;
    while(!stopping_) {
      UINT32 packet_size=0;
      require(capture->GetNextPacketSize(&packet_size),"GetNextPacketSize");
      if(!packet_size) { Sleep(5); continue; }
      while(packet_size && !stopping_) {
        BYTE* data=nullptr; UINT32 frames=0; DWORD flags=0;
        UINT64 device_position=0,qpc_100ns=0;
        require(capture->GetBuffer(&data,&frames,&flags,&device_position,&qpc_100ns),"GetBuffer");
        int64_t first_ns=qpc_100ns?static_cast<int64_t>(qpc_100ns)*100:
          monotonic_ns()-static_cast<int64_t>(frames)*1000000000LL/format.nSamplesPerSec;
        if((flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ||
           (pending_first_ns && std::llabs(first_ns-(pending_first_ns+
              static_cast<int64_t>(pending.size()/2)*1000000000LL/format.nSamplesPerSec))>50000000LL)) {
          pending.clear(); output.clear(); position=0; pending_first_ns=0;
          discontinuity=true;
        }
        if(!pending_first_ns) pending_first_ns=first_ns;
        pending.reserve(pending.size()+frames*2);
        for(UINT32 i=0;i<frames;++i) {
          float left=0,right=0;
          if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)) {
            left=sample_at(data,size_t(i)*format.nChannels,kind);
            right=format.nChannels>1?sample_at(data,size_t(i)*format.nChannels+1,kind):left;
          }
          pending.push_back(left); pending.push_back(right);
        }
        require(capture->ReleaseBuffer(frames),"ReleaseBuffer");
        while(position+1<pending.size()/2) {
          size_t index=static_cast<size_t>(position);
          float fraction=static_cast<float>(position-index);
          if(output.empty()) output_first_ns=pending_first_ns+
            static_cast<int64_t>(position*1000000000.0/format.nSamplesPerSec);
          for(int channel=0;channel<2;++channel) {
            float a=pending[2*index+channel],b=pending[2*(index+1)+channel];
            output.push_back(a+(b-a)*fraction);
          }
          if(output.size()==kAudioFramesPerPacket*kAudioChannels) {
            CapturedBlock block; block.capture_master_ns=output_first_ns;
            std::copy(output.begin(),output.end(),block.samples.begin());
            block.discontinuity=discontinuity; discontinuity=false;
            push(std::move(block)); output.clear();
          }
          position+=step;
        }
        size_t consumed=std::min(static_cast<size_t>(position),pending.size()/2);
        if(consumed) {
          pending.erase(pending.begin(),pending.begin()+2*consumed);
          pending_first_ns+=static_cast<int64_t>(consumed*1000000000.0/format.nSamplesPerSec);
          position-=consumed;
        }
        require(capture->GetNextPacketSize(&packet_size),"GetNextPacketSize");
      }
    }
  }
  bool tone_;
  std::atomic<bool> stopping_{false};
  std::thread worker_;
  mutable std::mutex mutex_;
  std::deque<CapturedBlock> queue_;
  std::string status_="STARTING";
};
}
std::unique_ptr<IAudioCaptureSource> make_tone_source() {
  return std::make_unique<WorkerSource>(true);
}
std::unique_ptr<IAudioCaptureSource> make_system_loopback_source() {
  return std::make_unique<WorkerSource>(false);
}
}
