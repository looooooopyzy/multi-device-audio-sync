#include "audio/monotonic_clock.h"
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
namespace audio {
int64_t ticks_to_ns(int64_t ticks, int64_t frequency) {
  if (frequency<=0 || ticks<0) throw std::invalid_argument("invalid monotonic counter");
  return (ticks/frequency)*1000000000LL + ((ticks%frequency)*1000000000LL)/frequency;
}
int64_t monotonic_ns() {
#ifdef _WIN32
  LARGE_INTEGER counter, frequency;
  if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency))
    throw std::runtime_error("QPC failed");
  return ticks_to_ns(counter.QuadPart,frequency.QuadPart);
#else
  timespec t{};
  if (clock_gettime(CLOCK_MONOTONIC,&t)!=0) throw std::runtime_error("clock_gettime failed");
  return static_cast<int64_t>(t.tv_sec)*1000000000LL+t.tv_nsec;
#endif
}
}
