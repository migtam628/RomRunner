#!/bin/sh
# Xcode build phase: copies libretro cores from ios/cores/ into
# Runner.app/Frameworks and signs them with the app's identity.
# tool/fetch_cores.sh ios fills ios/cores/.
set -e
CORES_DIR="${SRCROOT}/cores"
DEST="${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}"
mkdir -p "$DEST"
for f in "$CORES_DIR"/*.dylib; do
  [ -e "$f" ] || continue
  name=$(basename "$f")
  cp -f "$f" "$DEST/$name"
  if [ -n "${EXPANDED_CODE_SIGN_IDENTITY}" ] && [ "${CODE_SIGNING_ALLOWED}" != "NO" ]; then
    codesign --force --sign "${EXPANDED_CODE_SIGN_IDENTITY}" --timestamp=none "$DEST/$name"
  fi
done
