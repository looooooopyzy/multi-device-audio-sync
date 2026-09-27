#pragma once
#include <cstdint>
namespace audio {
int64_t ticks_to_ns(int64_t ticks, int64_t frequency);
int64_t monotonic_ns();
}
