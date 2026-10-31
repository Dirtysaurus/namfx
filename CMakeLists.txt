# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.16)
project(namfx CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Target: R36S (RK3326, Cortex-A35).
option(NAMFX_TARGET_A35 "Optimise for Cortex-A35" OFF)
option(NAMFX_WITH_ALSA "ALSA audio" ON)
option(NAMFX_WITH_SDL "Display via SDL2 (KMSDRM)" ON)
option(NAMFX_WITH_LV2 "LV2 plugin host (lilv)" ON)

if(NAMFX_TARGET_A35)
  add_compile_options(-mcpu=cortex-a35)
endif()
add_compile_options(-O3 -ffast-math -fno-finite-math-only)

set(NAM_DIR ${CMAKE_SOURCE_DIR}/third_party/NeuralAmpModelerCore)
if(NOT EXISTS ${NAM_DIR}/NAM/dsp.h)
  message(FATAL_ERROR "NeuralAmpModelerCore missing - run tools/fetch_deps.sh")
endif()

file(GLOB NAM_SRC ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/*/*.cpp)
# OBJECT library: NAM registers architectures via static initialisers,
# which the linker would drop from a static .a.
add_library(nam OBJECT ${NAM_SRC})
target_include_directories(nam SYSTEM PUBLIC ${NAM_DIR} ${NAM_DIR}/NAM ${NAM_DIR}/Dependencies/eigen ${NAM_DIR}/Dependencies/nlohmann)
target_compile_definitions(nam PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
target_compile_options(nam PRIVATE -w)

set(SRC
  src/main.cpp
  src/engine.cpp
  src/i18n.cpp
  src/dsp/fft.cpp
  src/dsp/convolver.cpp
  src/dsp/blocks.cpp
  src/dsp/catalog.cpp
  src/dsp/engines.cpp
  src/dsp/lv2_host.cpp
  src/dsp/nam_block.cpp
  src/dsp/graph.cpp
  src/dsp/tuner.cpp
  src/io/wav.cpp
  src/io/audio_alsa.cpp
  src/io/midi_in.cpp
  src/io/audio_devices.cpp
  src/net/http_server.cpp
  src/net/hotspot.cpp
  src/ui/canvas.cpp
  src/ui/qr/qrcodegen.cpp
  src/ui/screens.cpp
  src/ui/display.cpp
  src/ui/input_evdev.cpp
)
add_executable(namfx ${SRC})
target_include_directories(namfx PRIVATE src)
target_link_libraries(namfx PRIVATE nam pthread)

if(NAMFX_WITH_ALSA)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(ALSA alsa)
  endif()
  if(ALSA_FOUND)
    target_compile_definitions(namfx PRIVATE NAMFX_HAVE_ALSA)
    target_include_directories(namfx PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(namfx PRIVATE ${ALSA_LIBRARIES})
  else()
    message(WARNING "ALSA not found - offline/WAV mode only")
  endif()
endif()

if(NAMFX_WITH_SDL)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    target_compile_definitions(namfx PRIVATE NAMFX_HAVE_SDL)
    target_include_directories(namfx PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(namfx PRIVATE ${SDL2_LIBRARIES})
  else()
    message(WARNING "SDL2 not found - on-screen UI disabled (web UI and snapshots remain)")
  endif()
endif()

if(NAMFX_WITH_LV2)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(LILV lilv-0)
  endif()
  if(LILV_FOUND)
    target_compile_definitions(namfx PRIVATE NAMFX_HAVE_LILV)
    target_include_directories(namfx PRIVATE ${LILV_INCLUDE_DIRS})
    target_link_directories(namfx PRIVATE ${LILV_LIBRARY_DIRS})
    target_link_libraries(namfx PRIVATE ${LILV_LIBRARIES})
    message(STATUS "LV2 host enabled (lilv ${LILV_VERSION})")
  else()
    message(WARNING "lilv not found - LV2 plugins disabled (apt install liblilv-dev)")
  endif()
endif()
