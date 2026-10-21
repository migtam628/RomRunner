# RomRunner N64 core: CMake build for platforms where the Makefile doesn't
# fit (Windows/MSVC, Android NDK, iOS, universal macOS).
#
#   cmake -S cores/n64 -B build-n64 -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-n64 --config Release
#
# Output: romrunner_n64_libretro.{dll,dylib,so}; set RRN64_OUTPUT_NAME to
# rename (Android wants libromrunner_n64_libretro_android.so, iOS
# romrunner_n64_libretro_ios.dylib).
cmake_minimum_required(VERSION 3.16)
project(romrunner_n64 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(romrunner_n64 SHARED
  src/cpu.c src/fpu.c src/rsp.c src/rdp.c src/bus.c src/pif.c src/vi.c src/n64.c src/libretro.c)
target_include_directories(romrunner_n64 PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../../packages/romrunner_core/src/third_party)

set(RRN64_OUTPUT_NAME "romrunner_n64_libretro" CACHE STRING "Library file name without prefix/suffix")
set_target_properties(romrunner_n64 PROPERTIES PREFIX "" OUTPUT_NAME "${RRN64_OUTPUT_NAME}")

if(MSVC)
  target_compile_options(romrunner_n64 PRIVATE /O2 /W3 /wd4244 /wd4267 /wd4146)
  target_compile_definitions(romrunner_n64 PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(romrunner_n64 PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
  if(NOT APPLE)
    target_link_libraries(romrunner_n64 PRIVATE m)
  endif()
endif()
