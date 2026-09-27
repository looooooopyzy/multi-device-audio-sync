add_executable(windows-master main.cpp web_server.cpp local_playback.cpp audio_capture.cpp
  microphone_calibration.cpp network_addresses.cpp)
target_link_libraries(windows-master PRIVATE audio_shared winmm ole32 uuid iphlpapi)
if(MINGW)
  target_link_options(windows-master PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_executable(windows-calibrator WIN32 launcher.cpp network_addresses.cpp)
target_link_libraries(windows-calibrator PRIVATE audio_shared iphlpapi user32 gdi32 shell32 winmm ole32 uuid)
if(MINGW)
  target_link_options(windows-calibrator PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
