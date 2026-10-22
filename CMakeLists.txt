cmake_minimum_required(VERSION 3.20)
project(gizmos_recomp C)

# Build steps before this: see README (tools/build.py generates src/gen from
# your own original/ssg.exe). Nothing under src/gen is ever committed.

option(GG_HEADLESS "Build without SDL2 (no window, for automated bring-up)" OFF)
option(GG_FETCH_SDL "Download SDL2 if it is not installed" ON)
option(GG_SPCHECK "Lifted functions audit their stack balance" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/src/gen/recomp_all.h)
  message(FATAL_ERROR "src/gen is missing. Put your ssg.exe in original/ and run: python tools/build.py")
endif()

file(GLOB GEN_SOURCES ${CMAKE_SOURCE_DIR}/src/gen/*.c)
set(RUNTIME_SOURCES
  src/main.c
  src/runtime/cpu.c
  src/runtime/loader.c
  src/runtime/dos.c
  src/runtime/bios.c
  src/runtime/hw.c
  src/runtime/sound.c
  src/mods/mods.c
  src/mods/events.c
  third_party/emu8950/emu8950.c
  third_party/emu8950/emuadpcm.c)

add_executable(gizmos ${RUNTIME_SOURCES} ${GEN_SOURCES})
target_include_directories(gizmos PRIVATE src src/runtime include)
if(NOT WIN32)
  target_link_libraries(gizmos PRIVATE ${CMAKE_DL_LIBS})
endif()

# Example mod (mods/example): a shared library built against include/gg_mod.h only.
option(GG_BUILD_EXAMPLE_MOD "Build the example mod" ON)
if(GG_BUILD_EXAMPLE_MOD)
  add_library(gg_example_mod MODULE mods/example/example_mod.c)
  target_include_directories(gg_example_mod PRIVATE include)
  set_target_properties(gg_example_mod PROPERTIES PREFIX "" OUTPUT_NAME "example_mod"
                        C_VISIBILITY_PRESET hidden)
endif()
target_compile_definitions(gizmos PRIVATE RECOMP_IRQ)
if(GG_SPCHECK)
  target_compile_definitions(gizmos PRIVATE GG_SPCHECK)
endif()
option(GG_WATCH "Memory watchpoints (GG_WATCH=addr:len) for debugging; slower" OFF)
if(GG_WATCH)
  target_compile_definitions(gizmos PRIVATE RECOMP_MEM_HOOK)
  target_sources(gizmos PRIVATE src/runtime/watch.c)
  if(NOT MSVC)
    target_link_options(gizmos PRIVATE -rdynamic)
  endif()
endif()

if(MSVC)
  # Lifted code is huge and machine-written: silence the warnings it trips by design.
  target_compile_options(gizmos PRIVATE /W1 /bigobj /wd4244 /wd4146 /wd4018)
  target_compile_definitions(gizmos PRIVATE _CRT_SECURE_NO_WARNINGS)
  # Full optimisation of ~6 MB of machine-written C is slow and buys little.
  set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "/O1")
else()
  target_compile_options(gizmos PRIVATE -w)
  set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O1")
endif()

if(GG_HEADLESS)
  target_sources(gizmos PRIVATE src/host/host_null.c)
else()
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND AND GG_FETCH_SDL)
    include(FetchContent)
    FetchContent_Declare(SDL2
      URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    message(STATUS "Building SDL2 from source. On Linux, install your distro's SDL2 package "
                   "(e.g. libsdl2-dev) instead, or ALSA/PulseAudio headers, or there will be no audio.")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
  endif()
  target_sources(gizmos PRIVATE src/host/host_sdl.c)
  if(TARGET SDL2::SDL2-static)
    target_link_libraries(gizmos PRIVATE SDL2::SDL2-static)
  else()
    target_link_libraries(gizmos PRIVATE SDL2::SDL2)
  endif()
endif()

if(NOT WIN32)
  target_link_libraries(gizmos PRIVATE m)
endif()
