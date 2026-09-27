#pragma once
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
namespace audio {
enum class LogLevel { Debug=0, Info=1, Warn=2, Error=3 };
inline LogLevel configured_log_level() {
  const char* value=std::getenv("MDA_LOG_LEVEL");
  if(value && std::string(value)=="DEBUG") return LogLevel::Debug;
  if(value && std::string(value)=="WARN") return LogLevel::Warn;
  if(value && std::string(value)=="ERROR") return LogLevel::Error;
  return LogLevel::Info;
}
inline void log(LogLevel level,const char* category,const std::string& message) {
  if(static_cast<int>(level)<static_cast<int>(configured_log_level())) return;
  auto now=std::chrono::system_clock::now(); // diagnostics only, never used by clock sync
  auto time=std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local,&time);
#else
  localtime_r(&time,&local);
#endif
  int ms=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000);
  const char* label=level==LogLevel::Debug?"DEBUG":level==LogLevel::Info?"INFO":level==LogLevel::Warn?"WARN":"ERROR";
  std::cerr<<std::put_time(&local,"%Y-%m-%d %H:%M:%S")<<'.'<<std::setw(3)<<std::setfill('0')<<ms
           <<" ["<<label<<"] ["<<category<<"] "<<message<<'\n';
}
}
