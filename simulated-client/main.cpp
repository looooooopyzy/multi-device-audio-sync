#include "audio/monotonic_clock.h"
#include "audio/windows_udp.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>
using namespace audio;
struct Options {
  std::string name="Simulated-Speaker";
  double offset_ms=0, drift_ppm=0, jitter_ms=0, loss=0;
};
Options parse(int argc,char** argv) {
  Options o;
  for(int i=1;i<argc;++i) {
    std::string key=argv[i];
    if(i+1>=argc) throw std::runtime_error("missing value for "+key);
    std::string value=argv[++i];
    if(key=="--name") o.name=value;
    else if(key=="--clock-offset-ms") o.offset_ms=std::stod(value);
    else if(key=="--clock-drift-ppm") o.drift_ppm=std::stod(value);
    else if(key=="--network-jitter-ms") o.jitter_ms=std::stod(value);
    else if(key=="--packet-loss") o.loss=std::stod(value);
    else throw std::runtime_error("unknown option: "+key);
  }
  if(o.name.empty() || o.name.size()>kNameCapacity || o.jitter_ms<0 || o.jitter_ms>1000 ||
     o.loss<0 || o.loss>1 || std::abs(o.drift_ppm)>1000) throw std::runtime_error("invalid option value");
  return o;
}
int main(int argc,char** argv) {
  try {
    auto options=parse(argc,argv); start_winsock();
    SOCKET discovery=udp_socket(kDiscoveryPort,false,true);
    SOCKET clock=udp_socket(0);
    std::random_device rd; std::mt19937_64 rng(rd());
    uint64_t id=rng(); if(!id) id=1;
    std::uniform_real_distribution<double> unit(0,1), jitter(0,options.jitter_ms);
    auto network_delay=[&]() {
      if(options.jitter_ms<=0) return;
      int64_t delay_ns=static_cast<int64_t>(jitter(rng)*1e6);
      int64_t deadline=monotonic_ns()+delay_ns;
      if(delay_ns>5000000) std::this_thread::sleep_for(
        std::chrono::nanoseconds(delay_ns-5000000));
      // Windows' coarse Sleep quantum can exceed the requested 0-2 ms jitter.
      while(monotonic_ns()<deadline) YieldProcessor();
    };
    int64_t origin=monotonic_ns();
    auto client_now=[&]() -> int64_t {
      int64_t now=monotonic_ns();
      return now+static_cast<int64_t>(options.offset_ms*1e6+
                                     (now-origin)*options.drift_ppm/1e6);
    };
    sockaddr_in master{}; bool have_master=false; int64_t last_heartbeat=0;
    std::cout<<"Simulated client "<<options.name<<" id="<<id<<" offset="<<options.offset_ms
             <<"ms drift="<<options.drift_ppm<<"ppm\n";
    for(;;) {
      fd_set reads; FD_ZERO(&reads); FD_SET(discovery,&reads); FD_SET(clock,&reads);
      timeval timeout{0,100000};
      int ready=select(0,&reads,nullptr,nullptr,&timeout);
      if(ready==SOCKET_ERROR) throw std::runtime_error("select failed");
      for(SOCKET s:{discovery,clock}) {
        if(!FD_ISSET(s,&reads)) continue;
        for(;;) {
          uint8_t bytes[256]; sockaddr_in from{}; int len=sizeof(from);
          int count=recvfrom(s,reinterpret_cast<char*>(bytes),sizeof(bytes),0,
                             reinterpret_cast<sockaddr*>(&from),&len);
          if(count==SOCKET_ERROR) { if(WSAGetLastError()==WSAEWOULDBLOCK) break;
            throw std::runtime_error("recvfrom failed"); }
          Packet p; if(!decode(bytes,count,p)) continue;
          if(p.type==MessageType::DiscoveryRequest && s==discovery) {
            master=from; master.sin_port=htons(kSyncPort); have_master=true;
            Packet response; response.type=MessageType::DiscoveryResponse;
            response.sequence=p.sequence; response.deviceId=id;
            response.platform=Platform::Simulated; response.deviceName=options.name;
            if(unit(rng)>=options.loss) send_packet(clock,response,from);
          } else if(p.type==MessageType::SyncRequest && s==clock && have_master &&
                    from.sin_addr.s_addr==master.sin_addr.s_addr && ntohs(from.sin_port)==kSyncPort) {
            if(unit(rng)<options.loss) continue;
            // Emulate inbound path delay before the virtual receive timestamp.
            network_delay();
            int64_t t2=client_now();
            Packet response; response.type=MessageType::SyncResponse;
            response.sequence=p.sequence; response.deviceId=id; response.t1=p.t1;
            response.t2=t2; response.t3=client_now();
            response.platform=Platform::Simulated; response.deviceName=options.name;
            // Emulate outbound path delay after the virtual send timestamp.
            network_delay();
            send_packet(clock,response,from);
          }
        }
      }
      int64_t now=monotonic_ns();
      if(have_master && now-last_heartbeat>=1000000000LL) {
        Packet heartbeat; heartbeat.type=MessageType::Heartbeat; heartbeat.deviceId=id;
        heartbeat.platform=Platform::Simulated; heartbeat.deviceName=options.name;
        send_packet(clock,heartbeat,master); last_heartbeat=now;
      }
    }
  } catch(const std::exception& e) { std::cerr<<"simulated-client: "<<e.what()<<'\n'; return 1; }
}
