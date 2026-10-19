# Builds the RomRunner libretro bridge as a shared library.
# Used by the Android (NDK), Linux and Windows plugin builds.
# iOS and macOS compile the same source through CocoaPods (see *.podspec).
cmake_minimum_required(VERSION 3.10)

project(romrunner_core_library VERSION 0.1.0 LANGUAGES C)

add_library(romrunner_core SHARED "romrunner_core.c")

set_target_properties(romrunner_core PROPERTIES
  PUBLIC_HEADER romrunner_core.h
  OUTPUT_NAME "romrunner_core"
  C_VISIBILITY_PRESET hidden
)

target_compile_definitions(romrunner_core PUBLIC DART_SHARED_LIB)

if (ANDROID)
  # 16 KB page alignment required on newer Android devices.
  target_link_options(romrunner_core PRIVATE "-Wl,-z,max-page-size=16384")
  target_link_libraries(romrunner_core PRIVATE log dl m)
elseif (WIN32)
  target_link_libraries(romrunner_core PRIVATE winmm)
elseif (UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(romrunner_core PRIVATE Threads::Threads dl m)
endif()

if (MSVC)
  target_compile_definitions(romrunner_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
