#include "audio/audio_packet.h"
#include <cmath>
#include <cstring>

namespace audio {
namespace {
void put(std::vector<uint8_t>& out,size_t& at,uint64_t value,size_t count) {
  for(size_t i=0;i<count;++i) out[at++]=static_cast<uint8_t>(value>>(8*(count-1-i)));
}
uint64_t get(const uint8_t* bytes,size_t& at,size_t count) {
  uint64_t value=0;
  for(size_t i=0;i<count;++i) value=(value<<8)|bytes[at++];
  return value;
}
}
bool encode_audio_packet(const AudioPacket& packet,std::vector<uint8_t>& bytes) {
  if(!packet.stream_id || packet.presentation_master_ns<=0 ||
     packet.samples.size()!=kAudioFramesPerPacket*kAudioChannels) return false;
  bytes.assign(kAudioHeaderSize+packet.samples.size()*sizeof(float),0);
  size_t at=0;
  put(bytes,at,kAudioMagic,4); put(bytes,at,kAudioVersion,2);
  put(bytes,at,kAudioHeaderSize,2); put(bytes,at,packet.stream_id,4);
  put(bytes,at,packet.sequence,4); put(bytes,at,packet.sample_frame,8);
  put(bytes,at,static_cast<uint64_t>(packet.presentation_master_ns),8);
  put(bytes,at,kAudioSampleRate,4); put(bytes,at,kAudioChannels,2);
  put(bytes,at,kAudioFloat32,2); put(bytes,at,kAudioFramesPerPacket,4);
  put(bytes,at,packet.flags,4);
  for(float sample:packet.samples) {
    if(!std::isfinite(sample)) return false;
    uint32_t bits; std::memcpy(&bits,&sample,4);
    for(int i=0;i<4;++i) bytes[at++]=static_cast<uint8_t>(bits>>(8*i));
  }
  return true;
}
bool decode_audio_packet(const uint8_t* bytes,size_t length,AudioPacket& packet) {
  if(!bytes || length!=kAudioHeaderSize+kAudioFramesPerPacket*kAudioChannels*4) return false;
  size_t at=0;
  if(get(bytes,at,4)!=kAudioMagic || get(bytes,at,2)!=kAudioVersion ||
     get(bytes,at,2)!=kAudioHeaderSize) return false;
  AudioPacket next;
  next.stream_id=static_cast<uint32_t>(get(bytes,at,4));
  next.sequence=static_cast<uint32_t>(get(bytes,at,4));
  next.sample_frame=get(bytes,at,8);
  next.presentation_master_ns=static_cast<int64_t>(get(bytes,at,8));
  if(get(bytes,at,4)!=kAudioSampleRate || get(bytes,at,2)!=kAudioChannels ||
     get(bytes,at,2)!=kAudioFloat32 || get(bytes,at,4)!=kAudioFramesPerPacket ||
     !next.stream_id || next.presentation_master_ns<=0) return false;
  next.flags=static_cast<uint32_t>(get(bytes,at,4));
  next.samples.resize(kAudioFramesPerPacket*kAudioChannels);
  for(float& sample:next.samples) {
    uint32_t bits=0;
    for(int i=0;i<4;++i) bits|=uint32_t(bytes[at++])<<(8*i);
    std::memcpy(&sample,&bits,4);
    if(!std::isfinite(sample)) return false;
  }
  packet=std::move(next);
  return true;
}
}
