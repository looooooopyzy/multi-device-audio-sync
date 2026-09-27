#pragma once
#include "audio/clock_sync.h"
#include "audio/audio_packet.h"
#include <winsock2.h>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace audio {
struct WebNodeView {
  uint64_t id=0;
  std::string name, ip;
  ClockModel model;
  double calibration_ms=0;
  bool audio_enabled=false;
  int64_t last_seen_ns=0;
  uint64_t dropped_audio_packets=0;
};
struct CalibrationReport {
  uint64_t node_id=0;
  uint32_t session=0;
  double delta_ms=0;
  double confidence=0;
  double windows_confidence=0;
  double ipad_confidence=0;
};

class WebServer {
public:
  WebServer(const std::filesystem::path& asset_root, uint16_t port=17890);
  ~WebServer();
  WebServer(const WebServer&)=delete;
  WebServer& operator=(const WebServer&)=delete;
  void poll(int64_t now_ns);
  bool take_click_request();
  void broadcast_click(int64_t master_time_ns);
  void set_stream_info(uint32_t stream_id,const std::string& source,uint32_t delay_ms);
  void broadcast_audio(const AudioPacket& packet);
  std::vector<WebNodeView> nodes() const;
  bool take_network_late();
  uint64_t take_calibration_request();
  std::vector<CalibrationReport> take_calibration_reports();
  bool send_calibration_probes(uint64_t node_id,uint32_t session,int64_t windows_ns,int64_t ipad_ns);
  void send_calibration_applied(uint64_t node_id,uint32_t session,double ipad_delay_ms,double windows_delay_ms,
                                double residual_ms,double windows_delta_ms,double ipad_delta_ms);
  void send_calibration_error(uint64_t node_id,uint32_t session,const std::string& reason);
  uint16_t port() const { return port_; }
private:
  struct Client;
  SOCKET listener_=INVALID_SOCKET;
  std::filesystem::path asset_root_;
  uint16_t port_;
  std::map<SOCKET,std::unique_ptr<Client>> clients_;
  bool click_requested_=false;
  uint64_t calibration_request_=0;
  std::vector<CalibrationReport> calibration_reports_;
  uint32_t stream_id_=0,stream_delay_ms_=0;
  std::string stream_source_;
  void accept_clients();
  void read_client(Client& client);
  void flush_client(Client& client);
  void handle_http(Client& client);
  void handle_frames(Client& client);
  void handle_message(Client& client,const std::string& message,int64_t received_ns);
  void send_text(Client& client,const std::string& text);
  void send_frame(Client& client,uint8_t opcode,const std::string& payload);
  void tick_client(Client& client,int64_t now_ns);
  void send_stream_info(Client& client);
};
}
