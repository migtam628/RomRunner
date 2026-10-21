#!/usr/bin/env bash
# Downloads prebuilt libretro cores from the libretro buildbot and puts them
# where each RomRunner build expects them.
#
#   tool/fetch_cores.sh android   # -> app/android/app/src/main/jniLibs/<abi>/ (phones + Fire TV)
#   tool/fetch_cores.sh ios       # -> app/ios/cores/ (embedded by an Xcode build phase)
#   tool/fetch_cores.sh macos     # -> ~/Library/Application Support/com.mttech.romrunner/cores
#   tool/fetch_cores.sh linux     # -> ~/.local/share/com.mttech.romrunner/cores
#   tool/fetch_cores.sh windows   # -> %APPDATA%/com.mttech/RomRunner/cores (run from Git Bash)
#
# Desktop builds also download a missing core automatically on first launch
# of a game; the desktop targets here are for offline machines.
# Override the core list with: CORES="fceumm snes9x" tool/fetch_cores.sh android
# Override the destination (desktop and iOS targets) with: DEST=some/dir
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/app"
BB="https://buildbot.libretro.com/nightly"
# One per built-in system (see app/lib/models/console.dart). genesis_plus_gx
# also runs Sega CD; melondsds is the DS core (melonds on iOS).
CORES="${CORES:-fceumm gambatte mgba melondsds snes9x mednafen_vb genesis_plus_gx picodrive mednafen_pce_fast mednafen_ngp mednafen_wswan stella prosystem handy fbneo pcsx_rearmed}"
FAILED=()

for tool in curl unzip; do
  command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done

# fetch <url> <file inside zip> <destination path>
fetch() {
  local url=$1 inner=$2 dest=$3 tmp
  tmp=$(mktemp -d)
  # The buildbot sometimes drops connections mid-download (curl error 56),
  # which plain --retry doesn't cover: retry all errors, and the whole
  # download + unzip a few times in case a truncated zip slips through.
  local attempt ok=0
  for attempt in 1 2 3; do
    if curl -fsSL --connect-timeout 30 --retry 5 --retry-delay 3 --retry-all-errors -o "$tmp/core.zip" "$url" &&
       unzip -o -q "$tmp/core.zip" -d "$tmp" && [ -f "$tmp/$inner" ]; then
      ok=1
      break
    fi
    echo "  retry $(basename "$dest") (attempt $attempt failed)" >&2
    sleep $((attempt * 5))
  done
  if [ "$ok" = 1 ]; then
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
      [ "$c" = "melondsds" ] && name="melonds_libretro_ios.dylib"   # no melonDS DS build for iOS
      fetch "$BB/apple/ios-arm64/latest/$name.zip" "$name" "${DEST:-$APP/ios/cores}/$name"
    done
    ;;
  macos)
    arch=$(uname -m); [ "$arch" = "arm64" ] || arch=x86_64
    dest="${DEST:-$HOME/Library/Application Support/com.mttech.romrunner/cores}"
    echo "macOS $arch -> $dest"
    for c in $CORES; do
      fetch "$BB/apple/osx/$arch/latest/${c}_libretro.dylib.zip" "${c}_libretro.dylib" "$dest/${c}_libretro.dylib"
      xattr -d com.apple.quarantine "$dest/${c}_libretro.dylib" 2>/dev/null || true
    done
    ;;
  linux)
    arch=$(uname -m); [ "$arch" = "aarch64" ] || arch=x86_64
    dest="${DEST:-${XDG_DATA_HOME:-$HOME/.local/share}/com.mttech.romrunner/cores}"
    echo "Linux $arch -> $dest"
    for c in $CORES; do
      fetch "$BB/linux/$arch/latest/${c}_libretro.so.zip" "${c}_libretro.so" "$dest/${c}_libretro.so"
    done
    ;;
  windows)
    dest="${DEST:-${APPDATA:?run this from Git Bash on Windows}/com.mttech/RomRunner/cores}"
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
