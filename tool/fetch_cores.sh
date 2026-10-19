#!/usr/bin/env bash
# Downloads prebuilt libretro cores from the libretro buildbot and puts them
# where each MigPlay build expects them.
#
#   tool/fetch_cores.sh android   # -> app/android/app/src/main/jniLibs/<abi>/ (phones + Fire TV)
#   tool/fetch_cores.sh ios       # -> app/ios/cores/ (embedded by an Xcode build phase)
#   tool/fetch_cores.sh macos     # -> ~/Library/Application Support/com.mttech.migplay/cores
#   tool/fetch_cores.sh linux     # -> ~/.local/share/com.mttech.migplay/cores
#   tool/fetch_cores.sh windows   # -> %APPDATA%/com.mttech/migplay/cores (run from Git Bash)
#
# Desktop builds also download a missing core automatically on first launch
# of a game; the desktop targets here are for offline machines.
# Override the core list with: CORES="fceumm snes9x" tool/fetch_cores.sh android
# Override the destination (desktop and iOS targets) with: DEST=some/dir
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/app"
BB="https://buildbot.libretro.com/nightly"
CORES="${CORES:-fceumm gambatte mgba snes9x genesis_plus_gx pcsx_rearmed}"
FAILED=()

for tool in curl unzip; do
  command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done

# fetch <url> <file inside zip> <destination path>
fetch() {
  local url=$1 inner=$2 dest=$3 tmp
  tmp=$(mktemp -d)
  if curl -fsSL --retry 3 -o "$tmp/core.zip" "$url" && unzip -o -q "$tmp/core.zip" -d "$tmp" && [ -f "$tmp/$inner" ]; then
    mkdir -p "$(dirname "$dest")"
    mv -f "$tmp/$inner" "$dest"
    echo "  ok   $(basename "$dest")"
  else
    echo "  FAIL $url" >&2
    FAILED+=("$url")
  fi
  rm -rf "$tmp"
}

target=${1:-}
case "$target" in
  android)
    # arm64-v8a for phones/tablets and newer Fire TV; armeabi-v7a for Fire TV
    # sticks running 32-bit Fire OS.
    for abi in arm64-v8a armeabi-v7a; do
      echo "Android $abi"
      for c in $CORES; do
        # The "lib" prefix is required for Android to package the file.
        fetch "$BB/android/latest/$abi/${c}_libretro_android.so.zip" \
              "${c}_libretro_android.so" \
              "$APP/android/app/src/main/jniLibs/$abi/lib${c}_libretro_android.so"
      done
    done
    ;;
  ios)
    echo "iOS arm64"
    for c in $CORES; do
      name="${c}_libretro_ios.dylib"
      [ "$c" = "mgba" ] && name="mgba_libretro.dylib"   # buildbot names this one differently
      fetch "$BB/apple/ios-arm64/latest/$name.zip" "$name" "${DEST:-$APP/ios/cores}/$name"
    done
    ;;
  macos)
    arch=$(uname -m); [ "$arch" = "arm64" ] || arch=x86_64
    dest="${DEST:-$HOME/Library/Application Support/com.mttech.migplay/cores}"
    echo "macOS $arch -> $dest"
    for c in $CORES; do
      fetch "$BB/apple/osx/$arch/latest/${c}_libretro.dylib.zip" "${c}_libretro.dylib" "$dest/${c}_libretro.dylib"
      xattr -d com.apple.quarantine "$dest/${c}_libretro.dylib" 2>/dev/null || true
    done
    ;;
  linux)
    arch=$(uname -m); [ "$arch" = "aarch64" ] || arch=x86_64
    dest="${DEST:-${XDG_DATA_HOME:-$HOME/.local/share}/com.mttech.migplay/cores}"
    echo "Linux $arch -> $dest"
    for c in $CORES; do
      fetch "$BB/linux/$arch/latest/${c}_libretro.so.zip" "${c}_libretro.so" "$dest/${c}_libretro.so"
    done
    ;;
  windows)
    dest="${DEST:-${APPDATA:?run this from Git Bash on Windows}/com.mttech/migplay/cores}"
    echo "Windows x86_64 -> $dest"
    for c in $CORES; do
      fetch "$BB/windows/x86_64/latest/${c}_libretro.dll.zip" "${c}_libretro.dll" "$dest/${c}_libretro.dll"
    done
    ;;
  *)
    sed -n '2,15p' "$0"
    exit 2
    ;;
esac

if [ ${#FAILED[@]} -gt 0 ]; then
  echo "${#FAILED[@]} download(s) failed; those systems won't start until fixed." >&2
  exit 1
fi
echo "Done."
