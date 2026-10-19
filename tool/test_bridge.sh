#!/usr/bin/env bash
# Builds the native bridge headless (no audio) and runs a ROM through a core:
# loads it, runs frames, saves + restores a state and writes the last frame.
#
#   tool/test_bridge.sh path/to/core_libretro.so path/to/game.rom [frames]
#
# Output: frame.ppm in the current directory (open with any image viewer).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/packages/romrunner_core"
OUT="$(mktemp -d)/headless"
cc -O2 -Wall -DRR_NO_AUDIO "$SRC/src/romrunner_core.c" "$SRC/test/headless.c" -o "$OUT" -ldl -lpthread
"$OUT" "$1" "$2" frame.ppm "${3:-600}"
