#include "microphone_calibration.h"
#include "audio/monotonic_clock.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace audio {
namespace {
template<class T> struct ComHandle {
  T* value=nullptr;
  ~ComHandle(){ if(value) value->Release(); }
  T** put(){ return &value; }
  T* operator->() const { return value; }
};
void require(HRESULT code,const char* operation) {
  if(FAILED(code)) throw std::runtime_error(std::string(operation)+" failed HRESULT="+
    std::to_string(static_cast<unsigned long>(code)));
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
  throw std::runtime_error("unsupported microphone format");
}
float sample_at(const BYTE* data,size_t index,SampleKind kind) {
  if(kind==SampleKind::Float32) {
    float value; std::memcpy(&value,data+index*4,4);
    return std::isfinite(value)?std::clamp(value,-1.0f,1.0f):0;
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
bool physical_microphone(IMMDevice* device) {
  ComHandle<IPropertyStore> properties;
  if(FAILED(device->OpenPropertyStore(STGM_READ,properties.put()))) return false;
  PROPVARIANT value{};
  const HRESULT status=properties->GetValue(PKEY_Device_FriendlyName,&value);
  std::wstring name=SUCCEEDED(status) && value.vt==VT_LPWSTR && value.pwszVal?
                    value.pwszVal:L"";
  PropVariantClear(&value);
  for(auto& ch:name) if(ch>=L'A' && ch<=L'Z') ch+=L'a'-L'A';
  return !name.empty() && name.find(L"virtual")==std::wstring::npos &&
         name.find(L"cable")==std::wstring::npos &&
         name.find(L"transcreen")==std::wstring::npos &&
         name.find(L"虚拟")==std::wstring::npos;
}
}

MicrophoneCalibration::~MicrophoneCalibration(){ stop(); }
void MicrophoneCalibration::start(uint32_t duration_ms) {
  stop();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result_.reset(); error_.clear();
  }
  stopping_=false; state_=1;
  worker_=std::thread([this,duration_ms]{ run(duration_ms); });
}
void MicrophoneCalibration::stop() {
  stopping_=true;
  if(worker_.joinable()) worker_.join();
  state_=0;
}
std::optional<ProbePairResult> MicrophoneCalibration::result() const {
  std::lock_guard<std::mutex> lock(mutex_); return result_;
}
std::string MicrophoneCalibration::error() const {
  std::lock_guard<std::mutex> lock(mutex_); return error_;
}
void MicrophoneCalibration::run(uint32_t duration_ms) {
  try {
    require(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"microphone CoInitializeEx");
    struct CoScope { ~CoScope(){ CoUninitialize(); } } co_scope;
    ComHandle<IMMDeviceEnumerator> enumerator;
    require(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
      __uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(enumerator.put())),"MMDeviceEnumerator");
    ComHandle<IMMDevice> device;
    require(enumerator->GetDefaultAudioEndpoint(eCapture,eConsole,device.put()),"default microphone");
    if(!physical_microphone(device.value)) {
      device.value->Release(); device.value=nullptr;
      ComHandle<IMMDeviceCollection> collection;
      require(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,collection.put()),
              "enumerate microphones");
      UINT count=0;
      require(collection->GetCount(&count),"count microphones");
      for(UINT index=0;index<count;++index) {
        IMMDevice* candidate=nullptr;
        if(SUCCEEDED(collection->Item(index,&candidate))) {
          if(physical_microphone(candidate)) { device.value=candidate; break; }
          candidate->Release();
        }
      }
      if(!device.value) throw std::runtime_error("找不到实体 Windows 麦克风");
    }
    ComHandle<IAudioClient> client;
    require(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,
      reinterpret_cast<void**>(client.put())),"microphone IAudioClient");
    WAVEFORMATEX* raw_format=nullptr;
    require(client->GetMixFormat(&raw_format),"microphone GetMixFormat");
    struct FormatScope { WAVEFORMATEX* ptr; ~FormatScope(){ CoTaskMemFree(ptr); } } format_scope{raw_format};
    const WAVEFORMATEX& format=*raw_format;
    if(format.nSamplesPerSec<8000 || format.nSamplesPerSec>192000 || !format.nChannels ||
       format.nChannels>8 || format.nBlockAlign!=format.nChannels*(format.wBitsPerSample/8))
      throw std::runtime_error("invalid microphone format");
    const SampleKind kind=sample_kind(format);
    require(client->Initialize(AUDCLNT_SHAREMODE_SHARED,0,10000000,0,raw_format,nullptr),
            "microphone Initialize");
    ComHandle<IAudioCaptureClient> capture;
    require(client->GetService(__uuidof(IAudioCaptureClient),
      reinterpret_cast<void**>(capture.put())),"microphone IAudioCaptureClient");
    require(client->Start(),"microphone Start");
    struct StopScope { IAudioClient* client; ~StopScope(){ client->Stop(); } } stop_scope{client.value};
    std::vector<float> recording;
    recording.reserve(size_t(format.nSamplesPerSec)*duration_ms/1000+format.nSamplesPerSec);
    const int64_t deadline=monotonic_ns()+int64_t(duration_ms)*1000000;
    state_=2;
    while(!stopping_ && monotonic_ns()<deadline) {
      UINT32 available=0;
      require(capture->GetNextPacketSize(&available),"microphone GetNextPacketSize");
      if(!available) { Sleep(3); continue; }
      while(available && !stopping_) {
        BYTE* data=nullptr; UINT32 frames=0; DWORD flags=0;
        UINT64 device_position=0,qpc_position=0;
        require(capture->GetBuffer(&data,&frames,&flags,&device_position,&qpc_position),
                "microphone GetBuffer");
        for(UINT32 frame=0;frame<frames;++frame) {
          float value=0;
          if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)) {
            value=sample_at(data,size_t(frame)*format.nChannels,kind);
            if(format.nChannels>1)
              value=(value+sample_at(data,size_t(frame)*format.nChannels+1,kind))*0.5f;
          }
          recording.push_back(value);
        }
        require(capture->ReleaseBuffer(frames),"microphone ReleaseBuffer");
        if(recording.size()>size_t(format.nSamplesPerSec)*12)
          throw std::runtime_error("microphone recording too long");
        require(capture->GetNextPacketSize(&available),"microphone GetNextPacketSize");
      }
    }
    if(stopping_) return;
    auto detected=detect_probe_pair(recording,format.nSamplesPerSec);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      result_=detected;
      if(!detected.valid) error_="Windows 麦克风未清楚录到两次校准声";
    }
    state_=3;
  } catch(const std::exception& error) {
    {
      std::lock_guard<std::mutex> lock(mutex_); error_=error.what();
    }
    state_=4;
  }
}
}
