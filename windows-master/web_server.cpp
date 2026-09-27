#include "web_server.h"
#include "audio/monotonic_clock.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace audio {
namespace {
constexpr size_t kMaxClients=32, kMaxInput=32768, kMaxOutput=262144;
std::string lower(std::string value) {
  std::transform(value.begin(),value.end(),value.begin(),
                 [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
  return value;
}
uint32_t rotate(uint32_t value,int amount) { return (value<<amount)|(value>>(32-amount)); }
std::array<uint8_t,20> sha1(const std::string& text) {
  std::vector<uint8_t> bytes(text.begin(),text.end());
  const uint64_t bit_count=static_cast<uint64_t>(bytes.size())*8;
  bytes.push_back(0x80);
  while(bytes.size()%64!=56) bytes.push_back(0);
  for(int i=7;i>=0;--i) bytes.push_back(static_cast<uint8_t>(bit_count>>(i*8)));
  uint32_t h[5]={0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
  for(size_t block=0;block<bytes.size();block+=64) {
    uint32_t w[80]{};
    for(int i=0;i<16;++i) {
      for(int j=0;j<4;++j) w[i]=(w[i]<<8)|bytes[block+i*4+j];
    }
    for(int i=16;i<80;++i) w[i]=rotate(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
    for(int i=0;i<80;++i) {
      uint32_t f=0,k=0;
      if(i<20) { f=(b&c)|(~b&d); k=0x5A827999; }
      else if(i<40) { f=b^c^d; k=0x6ED9EBA1; }
      else if(i<60) { f=(b&c)|(b&d)|(c&d); k=0x8F1BBCDC; }
      else { f=b^c^d; k=0xCA62C1D6; }
      uint32_t next=rotate(a,5)+f+e+k+w[i];
      e=d; d=c; c=rotate(b,30); b=a; a=next;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
  }
  std::array<uint8_t,20> result{};
  for(int i=0;i<5;++i) for(int j=0;j<4;++j)
    result[i*4+j]=static_cast<uint8_t>(h[i]>>(24-8*j));
  return result;
}
std::string base64(const std::array<uint8_t,20>& bytes) {
  constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for(size_t i=0;i<bytes.size();i+=3) {
    uint32_t value=uint32_t(bytes[i])<<16;
    if(i+1<bytes.size()) value|=uint32_t(bytes[i+1])<<8;
    if(i+2<bytes.size()) value|=bytes[i+2];
    out+=alphabet[(value>>18)&63]; out+=alphabet[(value>>12)&63];
    out+=i+1<bytes.size()?alphabet[(value>>6)&63]:'=';
    out+=i+2<bytes.size()?alphabet[value&63]:'=';
  }
  return out;
}
std::vector<std::string> fields(const std::string& input) {
  std::vector<std::string> result;
  size_t start=0;
  while(true) {
    size_t end=input.find('|',start);
    result.push_back(input.substr(start,end==std::string::npos?end:end-start));
    if(end==std::string::npos) break;
    start=end+1;
  }
  return result;
}
template<class T> bool parse_integer(const std::string& input,T& value) {
  if(input.empty()) return false;
  auto parsed=std::from_chars(input.data(),input.data()+input.size(),value);
  return parsed.ec==std::errc{} && parsed.ptr==input.data()+input.size();
}
std::string formatted(double number,int digits=6) {
  std::ostringstream out; out<<std::fixed<<std::setprecision(digits)<<number; return out.str();
}
std::string address_of(const sockaddr_in& peer) {
  char buffer[INET_ADDRSTRLEN]{};
  inet_ntop(AF_INET,&peer.sin_addr,buffer,sizeof(buffer));
  return buffer;
}
}

struct WebServer::Client {
  SOCKET socket=INVALID_SOCKET;
  std::string ip,input,output,name;
  bool websocket=false,close_after_write=false,dead=false,registered=false,audio_enabled=false;
  uint64_t id=0;
  double calibration_ms=0;
  int64_t last_seen_ns=0,last_sync_ns=0,last_model_ns=0;
  uint32_t next_sequence=0;
  double last_raw_offset_ns=0;
  uint64_t dropped_audio_packets=0;
  uint64_t reported_late=0;
  bool has_late_baseline=false;
  bool recent_late=false;
  std::map<uint32_t,int64_t> pending;
  ClockSyncEngine sync;
};

WebServer::WebServer(const std::filesystem::path& asset_root,uint16_t port)
    :asset_root_(asset_root),port_(port),
     room_path_(asset_root.parent_path()/".local-certs/spatial-layout.txt") {
  load_room();
  listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  if(listener_==INVALID_SOCKET) throw std::runtime_error("web socket creation failed");
  sockaddr_in address{}; address.sin_family=AF_INET;
  address.sin_addr.s_addr=INADDR_ANY; address.sin_port=htons(port);
  if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0 ||
     listen(listener_,SOMAXCONN)!=0) {
    int code=WSAGetLastError(); closesocket(listener_); listener_=INVALID_SOCKET;
    throw std::runtime_error("web port "+std::to_string(port)+" bind/listen failed: "+std::to_string(code));
  }
  u_long nonblocking=1;
  if(ioctlsocket(listener_,FIONBIO,&nonblocking)!=0) {
    closesocket(listener_); listener_=INVALID_SOCKET;
    throw std::runtime_error("web listener nonblocking failed");
  }
}
WebServer::~WebServer() {
  for(auto& item:clients_) closesocket(item.first);
  if(listener_!=INVALID_SOCKET) closesocket(listener_);
}
void WebServer::accept_clients() {
  while(true) {
    sockaddr_in peer{}; int length=sizeof(peer);
    SOCKET socket=accept(listener_,reinterpret_cast<sockaddr*>(&peer),&length);
    if(socket==INVALID_SOCKET) break;
    if(clients_.size()>=kMaxClients) { closesocket(socket); continue; }
    u_long nonblocking=1;
    if(ioctlsocket(socket,FIONBIO,&nonblocking)!=0) { closesocket(socket); continue; }
    auto client=std::make_unique<Client>();
    client->socket=socket; client->ip=address_of(peer);
    client->last_seen_ns=monotonic_ns();
    clients_.emplace(socket,std::move(client));
  }
}
void WebServer::poll(int64_t now_ns) {
  accept_clients();
  for(auto& item:clients_) {
    Client& client=*item.second;
    read_client(client);
    if(!client.dead && client.websocket) tick_client(client,now_ns);
    if(!client.dead) flush_client(client);
  }
  for(auto it=clients_.begin();it!=clients_.end();) {
    if(it->second->dead || (it->second->close_after_write && it->second->output.empty())) {
      if(it->second->registered) room_dirty_=true;
      closesocket(it->first); it=clients_.erase(it);
    } else ++it;
  }
  if(room_dirty_) broadcast_room();
}
void WebServer::load_room() {
  std::ifstream input(room_path_);
  std::string kind;
  while(input>>kind) {
    if(kind=="mode") {
      std::string value; input>>value; spatial_mode_=value=="spatial";
    } else if(kind=="windows") {
      int x,y; if(!(input>>x>>y)) break;
      if(x>=0 && x<=100 && y>=0 && y<=100) { windows_x_=x; windows_y_=y; }
    } else if(kind=="device") {
      uint64_t id; int x,y; if(!(input>>id>>x>>y)) break;
      if(id && x>=0 && x<=100 && y>=0 && y<=100 && room_positions_.size()<128)
        room_positions_[id]={x,y};
    } else { std::string rest; std::getline(input,rest); }
  }
}
void WebServer::save_room() const {
  std::filesystem::create_directories(room_path_.parent_path());
  std::ofstream output(room_path_,std::ios::trunc);
  if(!output) return;
  output<<"mode "<<(spatial_mode_?"spatial":"normal")<<'\n';
  output<<"windows "<<windows_x_<<' '<<windows_y_<<'\n';
  for(const auto& item:room_positions_)
    output<<"device "<<item.first<<' '<<item.second.x<<' '<<item.second.y<<'\n';
}
std::map<uint64_t,SpeakerMix> WebServer::active_mixes() const {
  std::vector<SpeakerPosition> speakers;
  if(local_output_) speakers.push_back({0,windows_x_});
  for(const auto& item:clients_) {
    const Client& client=*item.second;
    if(!client.registered || client.dead || !client.audio_enabled || client.sync.size()<8) continue;
    auto found=room_positions_.find(client.id);
    speakers.push_back({client.id,found==room_positions_.end()?75:found->second.x});
  }
  return spatial_mixes(speakers);
}
SpeakerMix WebServer::local_mix() const {
  if(!spatial_mode_) return {};
  const auto mixes=active_mixes();
  auto found=mixes.find(0);
  return found==mixes.end()?SpeakerMix{}:found->second;
}
void WebServer::set_local_output(bool enabled) {
  if(local_output_!=enabled) { local_output_=enabled; room_dirty_=true; }
}
void WebServer::broadcast_room() {
  room_dirty_=false;
  const auto mixes=spatial_mode_?active_mixes():std::map<uint64_t,SpeakerMix>{};
  std::string room="ROOM|"+std::string(spatial_mode_?"spatial":"normal")+"|"+
    std::to_string(windows_x_)+"|"+std::to_string(windows_y_)+"|"+
    (local_output_?"1":"0");
  for(const auto& item:clients_) {
    const Client& client=*item.second;
    if(!client.registered || client.dead) continue;
    auto found=room_positions_.find(client.id);
    const RoomPosition position=found==room_positions_.end()?RoomPosition{}:found->second;
    const bool active=client.audio_enabled && client.sync.size()>=8;
    room+="|"+std::to_string(client.id)+"|"+client.name+"|"+
      std::to_string(position.x)+"|"+std::to_string(position.y)+"|"+
      (active?"1":"0");
  }
  for(auto& item:clients_) {
    Client& client=*item.second;
    if(!client.registered || client.dead) continue;
    send_text(client,room);
    auto found=mixes.find(client.id);
    const SpeakerMix mix=found==mixes.end()?SpeakerMix{}:found->second;
    send_text(client,"MIX|"+std::string(mix.spatial?"spatial":"normal")+"|"+
      formatted(mix.left,6)+"|"+formatted(mix.right,6));
  }
}
void WebServer::read_client(Client& client) {
  char bytes[8192];
  while(!client.dead) {
    int count=recv(client.socket,bytes,sizeof(bytes),0);
    if(count==0) { client.dead=true; return; }
    if(count<0) {
      if(WSAGetLastError()==WSAEWOULDBLOCK) return;
      client.dead=true; return;
    }
    client.input.append(bytes,count);
    if(client.input.size()>kMaxInput) { client.dead=true; return; }
    if(client.websocket) handle_frames(client); else handle_http(client);
  }
}
void WebServer::flush_client(Client& client) {
  while(!client.output.empty()) {
    int count=send(client.socket,client.output.data(),
                   static_cast<int>(std::min<size_t>(client.output.size(),16384)),0);
    if(count<0) {
      if(WSAGetLastError()==WSAEWOULDBLOCK) return;
      client.dead=true; return;
    }
    if(count==0) return;
    client.output.erase(0,count);
  }
}
void WebServer::handle_http(Client& client) {
  size_t end=client.input.find("\r\n\r\n");
  if(end==std::string::npos) return;
  std::string request=client.input.substr(0,end+4);
  client.input.erase(0,end+4);
  size_t first=request.find("\r\n");
  std::string line=request.substr(0,first);
  if(line.rfind("GET ",0)!=0) { client.dead=true; return; }
  size_t path_end=line.find(' ',4);
  if(path_end==std::string::npos) { client.dead=true; return; }
  std::string path=line.substr(4,path_end-4);
  const size_t query=path.find('?');
  if(query!=std::string::npos) path.resize(query);
  std::string key,upgrade;
  std::istringstream stream(request.substr(first+2));
  std::string header;
  while(std::getline(stream,header)) {
    if(!header.empty() && header.back()=='\r') header.pop_back();
    size_t colon=header.find(':');
    if(colon==std::string::npos) continue;
    std::string name=lower(header.substr(0,colon));
    std::string value=header.substr(colon+1);
    value.erase(0,value.find_first_not_of(" \t"));
    if(name=="sec-websocket-key") key=value;
    if(name=="upgrade") upgrade=lower(value);
  }
  if(path=="/ws" && upgrade=="websocket" && key.size()==24) {
    std::string accept_key=base64(sha1(key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
    client.output+="HTTP/1.1 101 Switching Protocols\r\n"
                   "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Accept: "+accept_key+"\r\n\r\n";
    client.websocket=true;
    if(!client.input.empty()) handle_frames(client);
    return;
  }
  static const std::map<std::string,std::pair<std::string,std::string>> assets={
    {"/",{"index.html","text/html; charset=utf-8"}},
    {"/index.html",{"index.html","text/html; charset=utf-8"}},
    {"/setup.html",{"setup.html","text/html; charset=utf-8"}},
    {"/styles/main.css",{"styles/main.css","text/css; charset=utf-8"}},
    {"/src/app.js",{"src/app.js","text/javascript; charset=utf-8"}},
    {"/src/audio-engine.js",{"src/audio-engine.js","text/javascript; charset=utf-8"}},
    {"/src/audio-packet.js",{"src/audio-packet.js","text/javascript; charset=utf-8"}},
    {"/src/audio-session.js",{"src/audio-session.js","text/javascript; charset=utf-8"}},
    {"/src/clock-sync.js",{"src/clock-sync.js","text/javascript; charset=utf-8"}},
    {"/src/websocket-client.js",{"src/websocket-client.js","text/javascript; charset=utf-8"}},
    {"/src/jitter-buffer.js",{"src/jitter-buffer.js","text/javascript; charset=utf-8"}},
    {"/src/device-info.js",{"src/device-info.js","text/javascript; charset=utf-8"}},
    {"/src/ui.js",{"src/ui.js","text/javascript; charset=utf-8"}},
    {"/src/calibration-probe.js",{"src/calibration-probe.js","text/javascript; charset=utf-8"}},
    {"/src/calibration-worker.js",{"src/calibration-worker.js","text/javascript; charset=utf-8"}},
    {"/src/calibration-recorder-worklet.js",{"src/calibration-recorder-worklet.js","text/javascript; charset=utf-8"}},
    {"/src/microphone-capture.js",{"src/microphone-capture.js","text/javascript; charset=utf-8"}}
  };
  auto asset=assets.find(path);
  std::string body,status="200 OK",mime="text/plain; charset=utf-8";
  if(path=="/lan-root-ca.crt") {
    std::ifstream file(asset_root_.parent_path()/".local-certs/lan-root-ca.crt",std::ios::binary);
    if(!file) { status="404 Not Found"; body="Certificate not ready"; }
    else { body.assign(std::istreambuf_iterator<char>(file),{});
      mime="application/x-x509-ca-cert"; }
  }
  else if(asset==assets.end()) { status="404 Not Found"; body="Not found"; }
  else {
    std::ifstream file(asset_root_/asset->second.first,std::ios::binary);
    if(!file) { status="404 Not Found"; body="Not found"; }
    else { body.assign(std::istreambuf_iterator<char>(file),{}); mime=asset->second.second; }
  }
  client.output+="HTTP/1.1 "+status+"\r\nContent-Type: "+mime+
                 "\r\nContent-Length: "+std::to_string(body.size())+
                 "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"+body;
  client.close_after_write=true;
}
void WebServer::handle_frames(Client& client) {
  while(client.input.size()>=2) {
    const auto* bytes=reinterpret_cast<const uint8_t*>(client.input.data());
    uint8_t opcode=bytes[0]&0x0f;
    bool final=(bytes[0]&0x80)!=0,masked=(bytes[1]&0x80)!=0;
    uint64_t length=bytes[1]&0x7f; size_t header=2;
    if(length==126) {
      if(client.input.size()<4) return;
      length=(uint64_t(bytes[2])<<8)|bytes[3]; header=4;
    } else if(length==127) {
      if(client.input.size()<10) return;
      length=0;
      for(int i=2;i<10;++i) length=(length<<8)|bytes[i];
      header=10;
    }
    if(!final || !masked || length>16384 || (opcode!=1 && opcode!=8 && opcode!=9 && opcode!=10)) {
      client.dead=true; return;
    }
    if(client.input.size()<header+4+length) return;
    std::string payload; payload.resize(static_cast<size_t>(length));
    for(size_t i=0;i<length;++i) payload[i]=
      static_cast<char>(bytes[header+4+i]^bytes[header+i%4]);
    client.input.erase(0,header+4+static_cast<size_t>(length));
    if(opcode==8) { client.dead=true; return; }
    if(opcode==9) send_frame(client,10,payload);
    if(opcode==1) handle_message(client,payload,monotonic_ns());
  }
}
void WebServer::send_frame(Client& client,uint8_t opcode,const std::string& payload) {
  if(payload.size()>65535 || client.output.size()+payload.size()+4>kMaxOutput) {
    client.dead=true; return;
  }
  client.output.push_back(static_cast<char>(0x80|opcode));
  if(payload.size()<126) client.output.push_back(static_cast<char>(payload.size()));
  else {
    client.output.push_back(126);
    client.output.push_back(static_cast<char>(payload.size()>>8));
    client.output.push_back(static_cast<char>(payload.size()));
  }
  client.output+=payload;
}
void WebServer::send_text(Client& client,const std::string& text) { send_frame(client,1,text); }
void WebServer::handle_message(Client& client,const std::string& message,int64_t received_ns) {
  auto f=fields(message);
  if(f.empty()) return;
  if(f[0]=="HELLO" && f.size()==3) {
    uint64_t id=0;
    if(!parse_integer(f[1],id) || !id || f[2].empty() || f[2].size()>48 ||
       !std::all_of(f[2].begin(),f[2].end(),[](unsigned char c){return c>=32 && c<=126;})) return;
    for(auto& item:clients_) if(item.second.get()!=&client &&
        item.second->registered && item.second->id==id) item.second->dead=true;
    client.id=id; client.name=f[2]; client.registered=true;
    client.last_seen_ns=received_ns;
    room_dirty_=true;
    send_text(client,"WELCOME|Windows-Master|"+std::to_string(id));
    send_stream_info(client);
  } else if(!client.registered) return;
  else if(f[0]=="SYNC_RESP" && f.size()==5) {
    uint32_t sequence=0; int64_t t1=0,t2=0,t3=0;
    if(!parse_integer(f[1],sequence) || !parse_integer(f[2],t1) ||
       !parse_integer(f[3],t2) || !parse_integer(f[4],t3)) return;
    auto pending=client.pending.find(sequence);
    if(pending==client.pending.end() || pending->second!=t1) return;
    client.pending.erase(pending);
    ClockSample sample;
    if(ClockSample::calculate(t1,t2,t3,received_ns,sample) && client.sync.add(sample)) {
      if(client.sync.size()==8) room_dirty_=true;
      client.last_raw_offset_ns=sample.offset_ns;
      client.last_seen_ns=received_ns;
    }
  } else if(f[0]=="PING" && f.size()==1) {
    client.last_seen_ns=received_ns; send_text(client,"PONG");
  } else if(f[0]=="CLICK" && f.size()==1) {
    click_requested_=true; client.last_seen_ns=received_ns;
  } else if(f[0]=="AUDIO" && f.size()==2 && (f[1]=="0" || f[1]=="1")) {
    const bool enabled=f[1]=="1";
    if(client.audio_enabled!=enabled) room_dirty_=true;
    client.audio_enabled=enabled; client.last_seen_ns=received_ns;
  } else if(f[0]=="MODE" && f.size()==2 &&
            (f[1]=="normal" || f[1]=="spatial")) {
    const bool spatial=f[1]=="spatial";
    if(spatial_mode_!=spatial) {
      spatial_mode_=spatial; room_dirty_=true; save_room();
    }
    client.last_seen_ns=received_ns;
  } else if(f[0]=="POSITION" && f.size()==4) {
    uint64_t id=0; int x=-1,y=-1;
    if(!parse_integer(f[1],id) || !parse_integer(f[2],x) ||
       !parse_integer(f[3],y) || x<0 || x>100 || y<0 || y>100) return;
    if(id) {
      const bool connected=std::any_of(clients_.begin(),clients_.end(),[&](const auto& item){
        return item.second->registered && !item.second->dead && item.second->id==id;
      });
      if(!connected || (room_positions_.size()>=128 && !room_positions_.count(id))) return;
      room_positions_[id]={x,y};
    } else { windows_x_=x; windows_y_=y; }
    room_dirty_=true; save_room(); client.last_seen_ns=received_ns;
  } else if(f[0]=="CAL" && f.size()==2) {
    try {
      size_t consumed=0; double value=std::stod(f[1],&consumed);
      if(consumed==f[1].size() && std::isfinite(value) && value>=-500 && value<=500) {
        client.calibration_ms=value; client.last_seen_ns=received_ns;
      }
    } catch(...) {}
  } else if(f[0]=="CAL_REQUEST" && f.size()==1) {
    if(client.audio_enabled && client.sync.size()>=8 && !calibration_request_)
      calibration_request_=client.id;
  } else if(f[0]=="CAL_RESULT" && f.size()==6) {
    uint32_t session=0;
    if(!parse_integer(f[1],session) || !session) return;
    try {
      size_t d=0,c=0,w=0,i=0;
      double delta=std::stod(f[2],&d),confidence=std::stod(f[3],&c);
      double windows_confidence=std::stod(f[4],&w);
      double ipad_confidence=std::stod(f[5],&i);
      if(d==f[2].size() && c==f[3].size() && std::isfinite(delta) &&
         std::isfinite(confidence) && std::abs(delta)<=500 &&
         confidence>=0 && confidence<=1 && w==f[4].size() && i==f[5].size() &&
         std::isfinite(windows_confidence) && std::isfinite(ipad_confidence) &&
         windows_confidence>=0 && windows_confidence<=1 &&
         ipad_confidence>=0 && ipad_confidence<=1)
        calibration_reports_.push_back({client.id,session,delta,confidence,
                                        windows_confidence,ipad_confidence});
    } catch(...) {}
  } else if(f[0]=="NET" && f.size()==2) {
    uint64_t late=0;
    if(parse_integer(f[1],late) && late<1000000000ULL) {
      if(client.has_late_baseline && client.audio_enabled && late>client.reported_late)
        client.recent_late=true;
      client.reported_late=late;
      client.has_late_baseline=true;
    }
  }
}
void WebServer::tick_client(Client& client,int64_t now_ns) {
  if(!client.registered) return;
  if(now_ns-client.last_seen_ns>5000000000LL) { client.dead=true; return; }
  for(auto it=client.pending.begin();it!=client.pending.end();) {
    if(now_ns-it->second>2000000000LL) it=client.pending.erase(it); else ++it;
  }
  if(now_ns-client.last_sync_ns>=250000000LL) {
    uint32_t sequence=++client.next_sequence;
    if(!sequence) sequence=++client.next_sequence;
    int64_t t1=monotonic_ns();
    client.pending[sequence]=t1;
    send_text(client,"SYNC_REQ|"+std::to_string(sequence)+"|"+std::to_string(t1));
    client.last_sync_ns=now_ns;
  }
  if(now_ns-client.last_model_ns>=1000000000LL) {
    ClockModel model=client.sync.model();
    if(model.valid) {
      double master_ms=double(now_ns)/1e6;
      double client_ms=(double(now_ns)+model.offset_at(now_ns))/1e6;
      send_text(client,"SYNC_MODEL|"+formatted(master_ms,6)+"|"+formatted(client_ms,6)+
        "|"+formatted(model.rate,12)+"|"+formatted(model.rtt_ns/1e6,3)+
        "|"+formatted(model.filtered_rtt_ns/1e6,3)+"|"+formatted(model.jitter_ns/1e6,3)+
        "|"+quality_name(model.quality)+"|"+std::to_string(model.sample_count)+
        "|"+formatted(client.last_raw_offset_ns/1e6,3));
    }
    client.last_model_ns=now_ns;
  }
}
bool WebServer::take_click_request() {
  bool requested=click_requested_; click_requested_=false; return requested;
}
void WebServer::broadcast_click(int64_t master_time_ns) {
  std::string message="CLICK_AT|"+std::to_string(master_time_ns)+"|10|1000";
  for(auto& item:clients_) if(item.second->websocket && item.second->registered)
    send_text(*item.second,message);
}
void WebServer::send_stream_info(Client& client) {
  if(stream_id_ && client.registered)
    send_text(client,"STREAM_INFO|"+std::to_string(stream_id_)+"|"+stream_source_+
              "|48000|2|"+std::to_string(stream_delay_ms_));
}
void WebServer::set_stream_info(uint32_t stream_id,const std::string& source,uint32_t delay_ms) {
  stream_id_=stream_id; stream_source_=source; stream_delay_ms_=delay_ms;
  for(auto& item:clients_) send_stream_info(*item.second);
}
void WebServer::broadcast_audio(const AudioPacket& packet) {
  std::vector<uint8_t> bytes;
  if(!encode_audio_packet(packet,bytes)) return;
  std::string payload(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  for(auto& item:clients_) {
    Client& client=*item.second;
    if(!client.websocket || !client.registered || !client.audio_enabled ||
       client.sync.size()<8 || client.dead) continue;
    if(client.output.size()+payload.size()+4>65536) {
      ++client.dropped_audio_packets;
      continue;
    }
    send_frame(client,2,payload);
  }
}
std::vector<WebNodeView> WebServer::nodes() const {
  std::vector<WebNodeView> result;
  for(const auto& item:clients_) {
    const Client& c=*item.second;
    if(c.registered && !c.dead)
      result.push_back({c.id,c.name,c.ip,c.sync.model(),c.calibration_ms,
                        c.audio_enabled,c.last_seen_ns,c.dropped_audio_packets});
  }
  return result;
}
bool WebServer::take_network_late() {
  bool late=false;
  for(auto& item:clients_) {
    late=late || item.second->recent_late;
    item.second->recent_late=false;
  }
  return late;
}
uint64_t WebServer::take_calibration_request() {
  uint64_t id=calibration_request_; calibration_request_=0; return id;
}
std::vector<CalibrationReport> WebServer::take_calibration_reports() {
  std::vector<CalibrationReport> reports;
  reports.swap(calibration_reports_);
  return reports;
}
bool WebServer::send_calibration_probes(uint64_t node_id,uint32_t session,
                                        int64_t windows_ns,int64_t ipad_ns) {
  for(auto& item:clients_) {
    Client& client=*item.second;
    if(client.id!=node_id || !client.registered || !client.audio_enabled || client.dead) continue;
    send_text(client,"CAL_PROBES|"+std::to_string(session)+"|"+
      std::to_string(windows_ns)+"|"+std::to_string(ipad_ns));
    return true;
  }
  return false;
}
void WebServer::send_calibration_applied(uint64_t node_id,uint32_t session,
                                         double ipad_delay_ms,double windows_delay_ms,
                                         double residual_ms,double windows_delta_ms,
                                         double ipad_delta_ms) {
  for(auto& item:clients_) if(item.second->id==node_id && item.second->registered) {
    item.second->calibration_ms=ipad_delay_ms;
    send_text(*item.second,"CAL_APPLY|"+std::to_string(session)+"|"+
      formatted(ipad_delay_ms,2)+"|"+formatted(windows_delay_ms,2)+"|"+
      formatted(residual_ms,2)+"|"+formatted(windows_delta_ms,2)+"|"+
      formatted(ipad_delta_ms,2));
  }
}
void WebServer::send_calibration_error(uint64_t node_id,uint32_t session,
                                       const std::string& reason) {
  for(auto& item:clients_) if(item.second->id==node_id && item.second->registered)
    send_text(*item.second,"CAL_REJECT|"+std::to_string(session)+"|"+reason);
}
}
