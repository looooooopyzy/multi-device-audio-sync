#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdexcept>
#include <string>
#include "audio/protocol.h"
namespace audio {
struct UdpBindError : std::runtime_error {
  uint16_t port;
  int code;
  UdpBindError(uint16_t value,int error)
    :std::runtime_error("bind port "+std::to_string(value)+": "+std::to_string(error)),
     port(value),code(error) {}
};
inline void start_winsock() {
  WSADATA data{}; if(WSAStartup(MAKEWORD(2,2),&data)!=0) throw std::runtime_error("WSAStartup failed");
}
inline SOCKET udp_socket(uint16_t port,bool broadcast=false,bool reuse=false) {
  SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
  if(s==INVALID_SOCKET) throw std::runtime_error("socket failed: "+std::to_string(WSAGetLastError()));
  BOOL yes=TRUE;
  if(reuse && setsockopt(s,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&yes),sizeof(yes))!=0) {
    closesocket(s); throw std::runtime_error("SO_REUSEADDR failed");
  }
  if(broadcast && setsockopt(s,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&yes),sizeof(yes))!=0) {
    closesocket(s); throw std::runtime_error("SO_BROADCAST failed");
  }
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
  if(bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0) {
    int code=WSAGetLastError(); closesocket(s); throw UdpBindError(port,code);
  }
  u_long nonblocking=1;
  if(ioctlsocket(s,FIONBIO,&nonblocking)!=0) {
    closesocket(s); throw std::runtime_error("FIONBIO failed");
  }
  return s;
}
inline sockaddr_in endpoint(const char* ip,uint16_t port) {
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port);
  if(inet_pton(AF_INET,ip,&a.sin_addr)!=1) throw std::runtime_error("invalid IP");
  return a;
}
inline std::string ip_string(const sockaddr_in& a) {
  char b[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET,&a.sin_addr,b,sizeof(b)); return b;
}
inline bool send_packet(SOCKET s,const Packet& p,const sockaddr_in& to) {
  std::array<uint8_t,kPacketSize> b{};
  if(!encode(p,b)) return false;
  return sendto(s,reinterpret_cast<const char*>(b.data()),static_cast<int>(b.size()),0,
                reinterpret_cast<const sockaddr*>(&to),sizeof(to))==static_cast<int>(b.size());
}
}
#endif
