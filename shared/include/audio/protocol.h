#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace audio {
constexpr uint32_t kMagic = 0x4d444131;
constexpr uint16_t kVersion = 1;
constexpr uint16_t kDiscoveryPort = 45670;
constexpr uint16_t kSyncPort = 45671;
constexpr size_t kPacketSize = 96;
constexpr size_t kNameCapacity = 48;

enum class MessageType : uint16_t {
  DiscoveryRequest=1, DiscoveryResponse=2, SyncRequest=3,
  SyncResponse=4, Heartbeat=5,
  PrepareTrack=16, TrackReady=17, PlayAt=18, PauseAt=19,
  SeekAt=20, PlaybackPosition=21, ChannelAssignment=22
};
enum class Platform : uint8_t { Windows=1, Android=2, IOS=3, IPadOS=4, Simulated=5 };

struct Packet {
  MessageType type = MessageType::DiscoveryRequest;
  uint32_t sequence = 0;
  uint64_t deviceId = 0;
  int64_t t1 = 0, t2 = 0, t3 = 0;
  Platform platform = Platform::Windows;
  std::string deviceName;
};

bool encode(const Packet& packet, std::array<uint8_t,kPacketSize>& bytes);
bool decode(const uint8_t* bytes, size_t length, Packet& packet);
const char* platform_name(Platform platform);
}
