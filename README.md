# MigPlay

A ROM emulator front end for **Android, Fire TV, iOS, macOS, Windows, Linux and web**, built in Flutter on top of [libretro](https://www.libretro.com/) emulation cores.

| System | Extensions | Core |
|---|---|---|
| NES / Famicom Disk System | .nes .unf .fds | FCEUmm |
| Game Boy / Color | .gb .gbc | Gambatte |
| Game Boy Advance | .gba | mGBA |
| SNES | .sfc .smc | Snes9x |
| Genesis / Mega Drive / Master System / Game Gear | .md .gen .smd .sms .gg | Genesis Plus GX |
| PlayStation | .cue+.bin .chd .pbp .m3u | PCSX-ReARMed |

`.zip` files are unpacked on import. Lone `.bin` files ask which system they belong to.

## How it fits together

```
 Flutter UI (lib/)                 one codebase, all 7 targets
   ├─ library, pause menu, touch pad, TV focus
   ├─ native targets ──► migplay_core (C, FFI plugin)
   │                       ├─ dlopen()s a libretro core
   │                       ├─ emulation thread, audio-paced (miniaudio)
   │                       ├─ converts frames to RGBA, Dart paints them each vsync
   │                       └─ input bitmask, save states, battery saves
   └─ web target ─────► EmulatorJS iframe (same libretro cores, built to WASM)
```

Dart never touches audio or the emulation loop, so a slow UI frame never stutters the game.

## Layout

```
app/                      Flutter app (runners for all platforms already generated and patched)
  lib/models/             consoles, games, RetroPad buttons
  lib/services/           ROM library, core download/lookup, app folders
  lib/screens/            library, native game screen, web game screen
  lib/widgets/            touch controller, pause menu
  lib/input/              keyboard / gamepad / TV-remote bindings
  ios/embed_cores.sh      Xcode build phase that embeds + signs cores
packages/migplay_core/    FFI plugin: the C libretro host
  src/migplay_core.c      the bridge (one file)
  test/headless.c         headless test harness
tool/fetch_cores.sh       downloads prebuilt cores per platform
tool/test_bridge.sh       builds the bridge headless and runs a ROM through a core
```

## Build and run

Requires Flutter 3.24+ (built and tested with 3.47). From `app/`, run `flutter pub get` once.

**macOS / Windows / Linux** — cores download automatically the first time you start a game of each system (from the libretro buildbot, into the app's support folder).
```
flutter run -d macos      # or windows / linux
```
Offline machine: `tool/fetch_cores.sh macos|linux|windows` first. On Linux you need `libgtk-3-dev`, `clang`, `ninja-build`, `pkg-config`. The macOS app is built without the App Sandbox so it can load downloaded cores (fine for personal use).

**Android phone/tablet and Fire TV** — cores are packaged inside the APK.
```
../tool/fetch_cores.sh android     # arm64-v8a + armeabi-v7a (32-bit Fire OS)
flutter build apk --release
adb install -r build/app/outputs/flutter-apk/app-release.apk
```
For Fire TV: enable *Developer options → ADB debugging* and *Install unknown apps*, then `adb connect <fire-tv-ip>` before `adb install`. The app shows up in the Fire TV launcher (leanback banner included).

**iOS (sideload)** — cores are embedded in the app bundle by an Xcode build phase that is already added.
```
../tool/fetch_cores.sh ios
open ios/Runner.xcworkspace     # set your Team under Signing, then Run on your device
```
iOS does not allow JIT, so PS1 runs on its interpreter there (set automatically); fine on recent iPhones, slower on old ones.

**Web**
```
flutter build web --release
cd build/web && python3 -m http.server 8080
```
Games run through EmulatorJS loaded from its CDN, so the page needs internet. ROMs stay in the browser tab; nothing is uploaded. PS1 on web needs single-file `.chd` or `.pbp`.

## Releases (GitHub Actions)

`.github/workflows/release.yml` builds all seven targets and publishes a GitHub Release when you push a version tag:

```
git tag v0.1.0
git push origin v0.1.0
```

A tag with a dash (`v0.2.0-beta.1`) becomes a pre-release. To test the pipeline without publishing, open **Actions → Release → Run workflow**; the builds appear as downloadable artifacts on that run.

| Asset | Contents |
|---|---|
| `RomRunner-<ver>-android.apk` | Phones, tablets and Fire TV (arm64 + 32-bit ARM), cores included |
| `RomRunner-<ver>-windows-x64.zip` | Unzip and run `migplay.exe`, cores included |
| `RomRunner-<ver>-linux-x64.tar.gz` | Extract and run `./migplay`, cores included |
| `RomRunner-<ver>-macos.zip` | Universal app, not notarized (right-click → Open the first time); cores download on first use |
| `RomRunner-<ver>-ios-unsigned.ipa` | Sign and install with Sideloadly or AltStore |
| `RomRunner-<ver>-web.zip` | Static site; serve it over HTTP(S) |
| `SHA256SUMS.txt` | Checksums |

**Android signing (do this once before the first real release).** Without it, each release APK is signed with a different throwaway key, and Android refuses to install it over the previous version. On a machine with a JDK and `gh` logged in, run `tool/android_signing.sh`. It creates `~/romrunner-release.jks` and stores it in the repo's Actions secrets. Keep that file backed up and out of the repo.

`.github/workflows/ci.yml` runs on every push and pull request: `flutter analyze` and `flutter test`, a warnings-as-errors compile of the C bridge, and a headless run of the bridge against the Gambatte core with the `cpu_instrs` test ROM.

## Controls

| | Keyboard | Gamepad (Android/Fire TV) | Fire TV remote |
|---|---|---|---|
| D-pad | Arrows | D-pad | D-pad |
| A / B | X / Z | right / bottom face button | Center = A |
| X / Y | S / A | top / left face button | — |
| L / R (L2 / R2) | Q / W (1 / 3) | shoulders / triggers | — |
| Start / Select | Enter / Backspace | Start / Select | Play-Pause / Menu |
| Pause menu | Esc | Guide | Back |
| Fast-forward (4×) | Tab | — | Fast-forward key |

Phones show a multi-touch pad (layout changes per system). It hides itself once a controller or keyboard is used; the pause menu brings it back.

## Saves and BIOS

- Battery saves (`.srm`) are written automatically every ~10 s and on exit.
- Four save-state slots per game in the pause menu.
- BIOS files are optional. Add them through **Add ROMs** like any game (`scph5501.bin`, `gba_bios.bin`, …) and they're installed into the system folder automatically. The ⓘ sheet shows every folder path.

## What was verified while building this

- The C bridge built and ran real cores headless on Linux: NES (FCEUmm), Game Boy (Gambatte — passes Blargg's `cpu_instrs`), GBA (mGBA), SNES (Snes9x), Genesis (Genesis Plus GX), PS1 (PCSX-ReARMed). Save/load state, threaded 60 fps pacing, 4× fast-forward, pause, and core unload/reload all checked.
- The Flutter app built for Linux and web; the Linux build was run end to end (library → game → pause menu).
- `flutter analyze` is clean and `flutter test` passes.
- All 48 core download URLs (6 cores × 8 platform/arch combos) return 200 on the buildbot.

Not verified (no hardware here): Android, Fire TV, iOS, macOS and Windows builds, and real audio output. The platform files were generated by `flutter create` and then patched, so they should build, but expect a first-run fix or two.

**PS1 note:** in the Linux test VM the PS1 core's dynarec crashed on homebrew `.exe` test programs, while its interpreter ran them fine. That's most likely the VM or the BIOS-less EXE path rather than real discs, but if PS1 games crash on a device, set the interpreter the same way iOS does (one line in `game_screen_native.dart`).

## Next steps worth doing

1. Gamepads on iOS/macOS/Windows/Linux (the `gamepads` package; Android/Fire TV already work).
2. Core options UI (region, palette, renderer settings) — the bridge already supports `mp_set_option`.
3. Box art via the libretro thumbnails repo, recent/favorites rows.
4. Rewind and shaders (CRT/LCD) — the frame painter is the hook for shaders.
5. Upload to the GPU via a texture instead of decoding an image per frame (only matters for high-res PS1 modes).

## Legal

Use ROMs, disc images and BIOS files you dumped from games and hardware you own. The libretro cores and EmulatorJS are GPL-licensed: personal and sideloaded use is fine, but publishing to an app store requires GPL compliance (source availability) and store review of emulators varies by platform.
