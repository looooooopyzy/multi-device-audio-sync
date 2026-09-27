#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {
constexpr uint32_t kAudioMagic=0x4d444150; // MDAP
constexpr uint16_t kAudioVersion=1;
constexpr size_t kAudioHeaderSize=48;
constexpr uint32_t kAudioSampleRate=48000;
constexpr uint16_t kAudioChannels=2;
constexpr uint16_t kAudioFloat32=1;
constexpr uint32_t kAudioFramesPerPacket=480;

struct AudioPacket {
  uint32_t stream_id=0, sequence=0;
  uint64_t sample_frame=0;
  int64_t presentation_master_ns=0;
  uint32_t flags=0;
  std::vector<float> samples; // 480 stereo interleaved frames
};

bool encode_audio_packet(const AudioPacket& packet,std::vector<uint8_t>& bytes);
bool decode_audio_packet(const uint8_t* bytes,size_t length,AudioPacket& packet);
}
