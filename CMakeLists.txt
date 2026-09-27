cmake_minimum_required(VERSION 3.20)
project(multi_device_audio LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_subdirectory(shared)
if(WIN32)
  add_subdirectory(windows-master)
  add_subdirectory(simulated-client)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME web_integration
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/web_integration.py"
              "$<TARGET_FILE:windows-master>")
    add_test(NAME audio_stream_integration
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/audio_stream_integration.py"
              "$<TARGET_FILE:windows-master>")
  endif()
endif()
