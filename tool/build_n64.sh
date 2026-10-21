#!/usr/bin/env bash
# Builds RomRunner's own N64 core (cores/n64) and puts it where the app
# looks for it.
#
#   tool/build_n64.sh android   # -> app/android/app/src/main/jniLibs/<abi>/libromrunner_n64_libretro_android.so
#   tool/build_n64.sh ios       # -> app/ios/cores/romrunner_n64_libretro_ios.dylib
#   tool/build_n64.sh macos     # -> $DEST/romrunner_n64_libretro.dylib (universal)
#   tool/build_n64.sh linux     # -> $DEST/romrunner_n64_libretro.so
#   tool/build_n64.sh windows   # -> $DEST/romrunner_n64_libretro.dll (Git Bash, MSVC)
#
# DEST defaults to the app support cores folder, like tool/fetch_cores.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/cores/n64"
BUILD="${BUILD_DIR:-$ROOT/build-n64}"
APP="$ROOT/app"

cmake_build() { # <build dir> <cmake args...>
  local dir=$1; shift
  cmake -S "$SRC" -B "$dir" -DCMAKE_BUILD_TYPE=Release "$@"
  cmake --build "$dir" --config Release --parallel
}

target=${1:-}
case "$target" in
  android)
    ndk="${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:?set ANDROID_NDK_HOME}}}"
    for abi in arm64-v8a armeabi-v7a; do
      cmake_build "$BUILD/android-$abi" \
        -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-21 \
        -DRRN64_OUTPUT_NAME=libromrunner_n64_libretro_android
      out="$APP/android/app/src/main/jniLibs/$abi"
      mkdir -p "$out"
      cp "$BUILD/android-$abi/libromrunner_n64_libretro_android.so" "$out/"
      echo "  $out/libromrunner_n64_libretro_android.so"
    done
    ;;
  ios)
    cmake_build "$BUILD/ios" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DRRN64_OUTPUT_NAME=romrunner_n64_libretro_ios
    out="${DEST:-$APP/ios/cores}"
    mkdir -p "$out"
    cp "$BUILD/ios/romrunner_n64_libretro_ios.dylib" "$out/"
    install_name_tool -id "@rpath/romrunner_n64_libretro_ios.dylib" "$out/romrunner_n64_libretro_ios.dylib"
    echo "  $out/romrunner_n64_libretro_ios.dylib"
    ;;
  macos)
    cmake_build "$BUILD/macos" "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
    out="${DEST:-$HOME/Library/Application Support/com.mttech.romrunner/cores}"
    mkdir -p "$out"
    cp "$BUILD/macos/romrunner_n64_libretro.dylib" "$out/"
    codesign --force --sign - "$out/romrunner_n64_libretro.dylib"
    echo "  $out/romrunner_n64_libretro.dylib"
    ;;
  linux)
    cmake_build "$BUILD/linux"
    out="${DEST:-${XDG_DATA_HOME:-$HOME/.local/share}/com.mttech.romrunner/cores}"
    mkdir -p "$out"
    cp "$BUILD/linux/romrunner_n64_libretro.so" "$out/"
    echo "  $out/romrunner_n64_libretro.so"
    ;;
  windows)
    cmake_build "$BUILD/windows" -A x64
    out="${DEST:-${APPDATA:?run this from Git Bash on Windows}/com.mttech/RomRunner/cores}"
    mkdir -p "$out"
    cp "$BUILD/windows/Release/romrunner_n64_libretro.dll" "$out/"
    echo "  $out/romrunner_n64_libretro.dll"
    ;;
  *)
    echo "usage: $0 android|ios|macos|linux|windows" >&2
    exit 2
    ;;
esac
