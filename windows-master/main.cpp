#include "audio/clock_sync.h"
#include "audio/monotonic_clock.h"
#include "audio/node_registry.h"
#include "audio/sequence.h"
#include "audio/windows_udp.h"
#include "web_server.h"
#include "local_playback.h"
#include "audio_capture.h"
#include "network_addresses.h"
#include "microphone_calibration.h"
#include <conio.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
using namespace audio;
int main(int argc,char** argv) {
  try {
    bool web_only=false,local_stream_output=false;
    uint32_t render_device=0xffffffffu;
    bool render_device_set=false;
    uint16_t http_port=17890;
    std::string source_name;
    for(int i=1;i<argc;++i) {
      std::string arg=argv[i];
      if(arg=="--web-only") web_only=true;
      else if(arg=="--source" && i+1<argc) {
        source_name=argv[++i];
        if(source_name!="tone" && source_name!="system")
          throw std::runtime_error("--source must be tone or system");
      }
      else if(arg=="--local-stream-output") local_stream_output=true;
      else if(arg=="--render-device" && i+1<argc) {
        unsigned long value=std::stoul(argv[++i]);
        if(value>0xfffffffful) throw std::runtime_error("invalid render device");
        render_device=static_cast<uint32_t>(value); render_device_set=true;
      }
      else if(arg=="--http-port" && i+1<argc) {
        int value=std::stoi(argv[++i]);
        if(value<1 || value>65535) throw std::runtime_error("invalid HTTP port");
        http_port=static_cast<uint16_t>(value);
      } else throw std::runtime_error("usage: windows-master [--web-only] [--http-port 1..65535] [--source tone|system] [--local-stream-output] [--render-device N]");
    }
    if(local_stream_output && source_name.empty())
      throw std::runtime_error("--local-stream-output requires --source tone or system");
    if(local_stream_output && source_name=="system" && !render_device_set)
      throw std::runtime_error("system loopback output requires an explicit --render-device");
    if(local_stream_output && source_name=="system") {
      WAVEFORMATEX format{};
      format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=2;
      format.nSamplesPerSec=48000; format.wBitsPerSample=16;
      format.nBlockAlign=4; format.nAvgBytesPerSec=192000;
      HWAVEOUT mapper=nullptr; UINT default_device=WAVE_MAPPER;
      if(waveOutOpen(&mapper,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR) {
        waveOutGetID(mapper,&default_device);
        waveOutClose(mapper);
      }
      if(default_device==render_device)
        throw std::runtime_error("render device is the loopback input; choose a different physical output device");
    }
    start_winsock();
    SOCKET discovery=INVALID_SOCKET, sync=INVALID_SOCKET;
    if(!web_only) {
      try { discovery=udp_socket(0,true); sync=udp_socket(kSyncPort); }
      catch(const UdpBindError& error) {
        if(discovery!=INVALID_SOCKET) closesocket(discovery);
        discovery=INVALID_SOCKET;
        if(error.port!=kSyncPort || error.code!=WSAEADDRINUSE) throw;
        web_only=true;
      }
    }
    std::filesystem::path assets="web-client";
    if(!std::filesystem::exists(assets/"index.html"))
      assets=std::filesystem::path(argv[0]).parent_path()/"../../web-client";
    if(!std::filesystem::exists(assets/"index.html"))
      throw std::runtime_error("web-client/index.html not found; run from repository root");
    WebServer web(assets,http_port);
    const auto status_path=assets.parent_path()/".local-certs/calibration-status.txt";
    std::filesystem::create_directories(status_path.parent_path());
    auto calibration_status=[&](const std::string& value) {
      std::ofstream status(status_path,std::ios::binary|std::ios::trunc);
      if(status) status<<value;
    };
    calibration_status("等待 iPad 发起双向声音自动校准");
    LocalPlaybackNode local(render_device);
    std::unique_ptr<IAudioCaptureSource> capture;
    if(source_name=="tone") capture=make_tone_source();
    if(source_name=="system") capture=make_system_loopback_source();
    std::unique_ptr<LocalStreamPlayback> stream_output;
    if(local_stream_output && capture) {
      try { stream_output=std::make_unique<LocalStreamPlayback>(render_device); }
      catch(const std::exception& error) {
        calibration_status("Windows 输出设备启动失败："+std::string(error.what()));
        throw;
      }
    }
    const std::string stream_label=source_name=="system" && stream_output?
      "system-routed":source_name;
    uint32_t stream_id=capture?static_cast<uint32_t>(monotonic_ns()|1):0;
    uint32_t stream_delay_ms=200;
    uint32_t network_delay_ms=200;
    if(capture) { web.set_stream_info(stream_id,stream_label,stream_delay_ms); capture->start(); }
    uint32_t audio_sequence=0;
    uint64_t sample_frame=0,late_capture_blocks=0;
    bool next_discontinuity=false;
    NodeRegistry nodes; SequenceTracker sequences;
    uint32_t discovery_seq=0;
    int64_t last_discovery=0,last_sync=0,last_telemetry=0;
    int64_t last_source_check=0;
    int64_t last_network_adjust=0;
    unsigned stable_network_periods=0;
    MicrophoneCalibration microphone;
    struct CalibrationRun {
      uint64_t node_id;
      uint32_t session;
      int64_t started_ns;
      bool probes_sent=false;
      bool windows_only=false;
      std::optional<CalibrationReport> ipad_report;
    };
    std::optional<CalibrationRun> calibration;
    uint32_t next_calibration_session=0;
    double windows_output_delay_ms=0;
    auto addresses=lan_addresses();
    if(addresses.empty()) {
      std::cout<<"http://127.0.0.1:"<<http_port<<"/\n";
    } else {
      const bool has_preferred=std::any_of(addresses.begin(),addresses.end(),
        [](const LanAddress& address){ return address.preferred; });
      for(const auto& address:addresses) {
        if(has_preferred && !address.preferred) continue;
        std::cout<<"http://"<<address.ip<<':'<<http_port<<"/\n";
      }
    }
    std::cout.flush();
    for(;;) {
      int64_t now=monotonic_ns();
      web.poll(now);
      if(source_name=="system" && capture && now-last_source_check>=1000000000LL) {
        const std::string source_status=capture->status();
        if(source_status.rfind("ERROR:",0)==0)
          calibration_status("电脑音乐捕获失败："+source_status);
        last_source_check=now;
      }
      const uint64_t requested_node=web.take_calibration_request();
      if(requested_node) {
        if(calibration) web.send_calibration_error(requested_node,0,"主控正在进行另一项校准");
        else {
          calibration=CalibrationRun{requested_node,++next_calibration_session,now};
          calibration->windows_only=source_name=="system" && bool(stream_output);
          microphone.start();
          calibration_status("正在启动 Windows 麦克风");
        }
      }
      for(const auto& report:web.take_calibration_reports()) {
        if(calibration && report.node_id==calibration->node_id &&
           report.session==calibration->session) calibration->ipad_report=report;
      }
      if(calibration) {
        auto& run=*calibration;
        if(!run.probes_sent && microphone.ready()) {
          const int64_t windows_probe=now+2500000000LL;
          const int64_t ipad_probe=windows_probe+1000000000LL;
          if(web.send_calibration_probes(run.node_id,run.session,windows_probe,ipad_probe)) {
            local.schedule_probe(windows_probe+
              static_cast<int64_t>(windows_output_delay_ms*1000000));
            run.probes_sent=true;
            calibration_status("两端校准声已安排，正在录音");
          } else {
            microphone.stop(); calibration.reset();
            calibration_status("iPad 已断开，校准取消");
          }
        } else if(!run.probes_sent && microphone.finished()) {
          web.send_calibration_error(run.node_id,0,"Windows 麦克风无法启动");
          microphone.stop(); calibration.reset();
          calibration_status("Windows 麦克风无法启动，请检查录音设备");
        } else if(run.probes_sent && microphone.finished() &&
                  (run.windows_only || run.ipad_report)) {
          const auto windows= microphone.result();
          const auto ipad=run.ipad_report.value_or(CalibrationReport{});
          auto score_text=[](double windows_score,double ipad_score) {
            return "Windows声 "+std::to_string(int(std::round(windows_score*100)))+
              "%，iPad声 "+std::to_string(int(std::round(ipad_score*100)))+"%";
          };
          std::string reject_reason;
          if(!windows) reject_reason="Windows 麦克风录音失败："+microphone.error();
          else if(!windows->valid)
            reject_reason="Windows 麦克风未录清两声（"+
              score_text(windows->windows_confidence,windows->ipad_confidence)+"）";
          else if(!run.windows_only && ipad.confidence<kCalibrationMinConfidence)
            reject_reason="iPad 麦克风未录清两声（"+
              score_text(ipad.windows_confidence,ipad.ipad_confidence)+"）";
          else if(!run.windows_only && std::abs(windows->delta_ms-ipad.delta_ms)>45)
            reject_reason="两台麦克风时间差不一致：Windows "+
              std::to_string(int(std::round(windows->delta_ms)))+" ms，iPad "+
              std::to_string(int(std::round(ipad.delta_ms)))+" ms";
          if(!reject_reason.empty()) {
            web.send_calibration_error(run.node_id,run.session,reject_reason);
            calibration_status("校准未应用："+reject_reason);
          } else {
            const double residual_ms=run.windows_only?windows->delta_ms:
              (windows->delta_ms+ipad.delta_ms)*0.5;
            // Each mic recorded both probes. The average cancels first-order
            // propagation asymmetry between the two nearby devices.
            const auto views=web.nodes();
            auto view=std::find_if(views.begin(),views.end(),[&](const WebNodeView& n){
              return n.id==run.node_id;
            });
            if(view!=views.end()) {
              const bool other_speaker_active=std::any_of(views.begin(),views.end(),
                [&](const WebNodeView& node){
                  return node.id!=run.node_id && node.audio_enabled;
                });
              double client_delay=0,windows_delay=windows_output_delay_ms;
              if(other_speaker_active) {
                // Keep the shared Windows output fixed so an already aligned
                // browser speaker remains aligned while a new one is measured.
                client_delay=view->calibration_ms-residual_ms;
              } else {
                const double difference=view->calibration_ms-
                  windows_output_delay_ms-residual_ms;
                client_delay=std::clamp(difference,0.0,500.0);
                windows_delay=std::clamp(-difference,0.0,500.0);
              }
              if(client_delay<-500.0 || client_delay>500.0) {
                web.send_calibration_error(run.node_id,run.session,
                  "所需设备延迟超出 ±500 ms，未更改已同步设备");
                calibration_status("校准未应用：所需设备延迟超出 ±500 ms");
              } else {
                windows_output_delay_ms=windows_delay;
                if(stream_output) stream_output->set_delay_ms(windows_delay);
                web.send_calibration_applied(run.node_id,run.session,
                                             client_delay,windows_delay,residual_ms,
                                             windows->delta_ms,
                                             run.windows_only?0.0:ipad.delta_ms);
                calibration_status("校准完成（Windows 麦克风）：当前设备延迟 "+
                  std::to_string(client_delay)+" ms，Windows 延迟 "+
                  std::to_string(windows_delay)+" ms");
              }
            }
          }
          microphone.stop(); calibration.reset();
        } else if(now-run.started_ns>12000000000LL) {
          web.send_calibration_error(run.node_id,run.session,"校准超时");
          microphone.stop(); calibration.reset();
          calibration_status("校准超时，请保持 iPad 页面在前台后重试");
        }
      }
      if(capture && now-last_network_adjust>=5000000000LL) {
        uint32_t desired=200;
        if(web.take_network_late()) desired=300;
        for(const auto& node:web.nodes()) {
          if(!node.audio_enabled || !node.model.valid) continue;
          const double rtt_ms=node.model.filtered_rtt_ns/1e6;
          const double jitter_ms=node.model.jitter_ns/1e6;
          if(rtt_ms>220 || jitter_ms>45) desired=500;
          else if(rtt_ms>100 || jitter_ms>18) desired=std::max(desired,300u);
        }
        if(desired>network_delay_ms) {
          network_delay_ms=desired; stable_network_periods=0;
        } else if(desired<network_delay_ms && ++stable_network_periods>=6) {
          network_delay_ms=desired; stable_network_periods=0;
        }
        last_network_adjust=now;
      }
      if(capture) {
        // A negative client calibration consumes the future presentation lead.
        // Keep at least 100 ms after capture (more when the network is unstable)
        // so Safari can schedule samples that have actually arrived.
        uint32_t required_delay=network_delay_ms;
        for(const auto& node:web.nodes()) {
          if(!node.audio_enabled) continue;
          const uint32_t early_ms=static_cast<uint32_t>(
            std::ceil(std::max(0.0,-node.calibration_ms)));
          required_delay=std::max(required_delay,
            early_ms+network_delay_ms-100);
        }
        if(required_delay!=stream_delay_ms) {
          stream_delay_ms=required_delay;
          ++stream_id;
          if(!stream_id) ++stream_id;
          sample_frame=0;
          next_discontinuity=true;
          web.set_stream_info(stream_id,stream_label,stream_delay_ms);
        }
      }
      if(capture) {
        CapturedBlock block;
        for(int i=0;i<16 && capture->pop(block);++i) {
          AudioPacket packet;
          packet.stream_id=stream_id;
          packet.sequence=++audio_sequence;
          packet.sample_frame=sample_frame;
          sample_frame+=kAudioFramesPerPacket;
          packet.presentation_master_ns=block.capture_master_ns+
            int64_t(stream_delay_ms)*1000000LL;
          packet.flags=(block.discontinuity || next_discontinuity)?1:0;
          if(packet.presentation_master_ns-monotonic_ns()<50000000LL) {
            ++late_capture_blocks; next_discontinuity=true; continue;
          }
          next_discontinuity=false;
          packet.samples.assign(block.samples.begin(),block.samples.end());
          web.broadcast_audio(packet);
          if(stream_output) stream_output->submit(packet);
        }
      }
      bool cli_click=false;
      while(_kbhit()) { int key=_getch(); if(key=='c' || key=='C') cli_click=true; }
      if(cli_click || web.take_click_request()) {
        int64_t presentation=monotonic_ns()+1000000000LL;
        web.broadcast_click(presentation);
        local.schedule_click(presentation+
          static_cast<int64_t>(windows_output_delay_ms*1000000));
      }
      if(!web_only && now-last_discovery>=1000000000LL) {
        Packet request; request.type=MessageType::DiscoveryRequest; request.sequence=++discovery_seq;
        request.platform=Platform::Windows; request.deviceName="Windows-Master";
        auto broadcast=endpoint("255.255.255.255",kDiscoveryPort);
        send_packet(discovery,request,broadcast);
        // Loopback broadcast reaches multiple local simulator instances.
        auto loopback=endpoint("127.255.255.255",kDiscoveryPort);
        send_packet(discovery,request,loopback);
        last_discovery=now;
      }
      if(!web_only && now-last_sync>=250000000LL) {
        for(const auto& kv:nodes.all()) {
          const Node& n=kv.second; if(n.status!=NodeStatus::Online) continue;
          Packet request; request.type=MessageType::SyncRequest;
          request.sequence=sequences.next(); request.deviceId=0;
          request.t1=monotonic_ns(); request.platform=Platform::Windows;
          sockaddr_in to=endpoint(n.ip.c_str(),n.port);
          if(send_packet(sync,request,to))
            sequences.add(request.sequence,{n.id,request.t1,to.sin_addr.s_addr,n.port});
        }
        last_sync=now;
      }
      fd_set reads; FD_ZERO(&reads);
      if(!web_only) { FD_SET(discovery,&reads); FD_SET(sync,&reads); }
      timeval timeout{0,2000};
      if(!web_only && select(0,&reads,nullptr,nullptr,&timeout)==SOCKET_ERROR)
        throw std::runtime_error("select failed");
      if(web_only) Sleep(2);
      for(SOCKET s:{discovery,sync}) {
        if(web_only) break;
        if(!FD_ISSET(s,&reads)) continue;
        for(;;) {
          uint8_t bytes[256]; sockaddr_in from{}; int len=sizeof(from);
          int count=recvfrom(s,reinterpret_cast<char*>(bytes),sizeof(bytes),0,
                             reinterpret_cast<sockaddr*>(&from),&len);
          int64_t t4=monotonic_ns();
          if(count==SOCKET_ERROR) { if(WSAGetLastError()==WSAEWOULDBLOCK) break;
            throw std::runtime_error("recvfrom failed"); }
          Packet p; if(!decode(bytes,count,p) || !p.deviceId) continue;
          std::string ip=ip_string(from); uint16_t port=ntohs(from.sin_port);
          if(p.type==MessageType::DiscoveryResponse) {
            nodes.seen(p.deviceId,p.deviceName,ip,port,p.platform,t4);
          } else if(s==sync && p.type==MessageType::SyncResponse) {
            Node* n=nodes.find(p.deviceId);
            if(!n || n->ip!=ip || n->port!=port ||
               !sequences.consume(p.sequence,p.deviceId,p.t1,from.sin_addr.s_addr,port)) continue;
            ClockSample sample;
            if(ClockSample::calculate(p.t1,p.t2,p.t3,t4,sample) && n->sync.add(sample)) {
              nodes.seen(n->id,n->name,n->ip,n->port,n->platform,t4);
            }
          } else if(s==sync && p.type==MessageType::Heartbeat) {
            Node* n=nodes.find(p.deviceId);
            if(n && n->ip==ip && n->port==port)
              nodes.seen(n->id,n->name,n->ip,n->port,n->platform,t4);
          }
        }
      }
      now=monotonic_ns(); nodes.expire(now); sequences.expire(now);
      if(now-last_telemetry>=1000000000LL) {
        for(const auto& kv:nodes.all()) {
          const Node& n=kv.second; ClockModel m=n.sync.model();
          if(n.status==NodeStatus::Online && m.valid) {
            Packet telemetry; telemetry.type=MessageType::Heartbeat;
            telemetry.sequence=static_cast<uint32_t>(m.sample_count);
            telemetry.t1=static_cast<int64_t>(m.offset_at(now));
            telemetry.t2=static_cast<int64_t>(m.filtered_rtt_ns);
            telemetry.t3=static_cast<int64_t>(m.drift_ppm*1000);
            telemetry.platform=Platform::Windows;
            send_packet(sync,telemetry,endpoint(n.ip.c_str(),n.port));
          }
        }
        last_telemetry=now;
      }
    }
  } catch(const std::exception& e) { std::cerr<<"windows-master: "<<e.what()<<'\n'; return 1; }
}
