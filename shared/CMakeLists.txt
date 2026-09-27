add_library(audio_shared STATIC
  src/protocol.cpp src/clock_sync.cpp src/node_registry.cpp src/monotonic_clock.cpp
  src/audio_packet.cpp src/acoustic_calibration.cpp)
target_include_directories(audio_shared PUBLIC include)
if(WIN32)
  target_link_libraries(audio_shared PUBLIC ws2_32)
endif()
add_executable(shared_tests tests/test_main.cpp)
target_link_libraries(shared_tests PRIVATE audio_shared)
if(MINGW)
  target_link_options(shared_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME shared_tests COMMAND shared_tests)
